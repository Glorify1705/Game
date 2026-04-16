#include <atomic>

#include "executor.h"
#include "test_fixture.h"

namespace G {

TEST(InlineExecutorTest, SubmitRunsSynchronously) {
  InlineExecutor exec;
  int result = 0;
  Task task;
  task.fn = [](void* ud) {
    *static_cast<int*>(ud) = 42;
    return true;
  };
  task.userdata = &result;
  task.cleanup = nullptr;
  exec.Submit(&task);
  EXPECT_EQ(result, 42);
  EXPECT_EQ(task.state.load(), TaskState::kSucceeded);
}

TEST(InlineExecutorTest, FailedTaskState) {
  InlineExecutor exec;
  Task task;
  task.fn = [](void*) { return false; };
  task.userdata = nullptr;
  task.cleanup = nullptr;
  exec.Submit(&task);
  EXPECT_EQ(task.state.load(), TaskState::kFailed);
}

TEST(InlineExecutorTest, CleanupIsCalledOnFailure) {
  InlineExecutor exec;
  int cleanup_count = 0;
  Task task;
  task.fn = [](void*) { return false; };
  task.userdata = &cleanup_count;
  task.cleanup = [](void* ud) { ++*static_cast<int*>(ud); };
  exec.Submit(&task);
  EXPECT_EQ(cleanup_count, 1);
  EXPECT_EQ(task.state.load(), TaskState::kFailed);
}

TEST(InlineExecutorTest, CleanupIsCalled) {
  InlineExecutor exec;
  int cleanup_count = 0;
  Task task;
  task.fn = [](void*) { return true; };
  task.userdata = &cleanup_count;
  task.cleanup = [](void* ud) { ++*static_cast<int*>(ud); };
  exec.Submit(&task);
  EXPECT_EQ(cleanup_count, 1);
}

TEST(InlineExecutorTest, TryWaitAlwaysTrue) {
  InlineExecutor exec;
  Task task;
  task.fn = [](void*) { return true; };
  task.userdata = nullptr;
  task.cleanup = nullptr;
  exec.Submit(&task);
  EXPECT_TRUE(exec.TryWait(&task));
}

TEST(InlineExecutorTest, ParallelForRunsSequentially) {
  InlineExecutor exec;
  int sum = 0;
  exec.ParallelFor(
      10, 1,
      [](int start, int end, void* ctx) {
        auto* s = static_cast<int*>(ctx);
        for (int i = start; i < end; ++i) *s += i;
      },
      &sum);
  EXPECT_EQ(sum, 45);
}

class ThreadPoolExecutorTest : public BaseTest {};

TEST_F(ThreadPoolExecutorTest, SubmitAndWait) {
  ThreadPoolExecutor pool(alloc, 2);
  pool.Start();
  std::atomic<int> result{0};
  Task task;
  task.fn = [](void* ud) {
    static_cast<std::atomic<int>*>(ud)->store(42, std::memory_order_relaxed);
    return true;
  };
  task.userdata = &result;
  task.cleanup = nullptr;
  pool.Submit(&task);
  pool.Wait(&task);
  EXPECT_EQ(result.load(), 42);
  pool.Shutdown();
}

TEST_F(ThreadPoolExecutorTest, ParallelForSum) {
  ThreadPoolExecutor pool(alloc, 4);
  pool.Start();
  constexpr int kN = 10000;
  int data[kN];
  for (int i = 0; i < kN; ++i) data[i] = i;

  std::atomic<int> sum{0};
  struct Ctx {
    int* data;
    std::atomic<int>* sum;
  };
  Ctx ctx{data, &sum};

  pool.ParallelFor(
      kN, /*min_batch=*/100,
      [](int start, int end, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        int local = 0;
        for (int i = start; i < end; ++i) local += c->data[i];
        c->sum->fetch_add(local, std::memory_order_relaxed);
      },
      &ctx);
  EXPECT_EQ(sum.load(), kN * (kN - 1) / 2);
  pool.Shutdown();
}

TEST_F(ThreadPoolExecutorTest, ParallelForWithZeroCount) {
  ThreadPoolExecutor pool(alloc, 2);
  pool.Start();
  bool called = false;
  pool.ParallelFor(
      0, 1, [](int, int, void* ud) { *static_cast<bool*>(ud) = true; },
      &called);
  EXPECT_FALSE(called);
  pool.Shutdown();
}

TEST_F(ThreadPoolExecutorTest, NumDefaultThreads) {
  size_t n = ThreadPoolExecutor::NumDefaultThreads();
  EXPECT_GE(n, 1u);
}

}  // namespace G
