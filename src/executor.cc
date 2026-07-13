#include "executor.h"

#include "logging.h"
#include "stringlib.h"
#include "thread.h"

namespace G {

namespace {

void RunTask(Task* task) {
  bool ok = task->fn(task->userdata);
  if (task->cleanup) task->cleanup(task->userdata);
  task->state.store(ok ? TaskState::kSucceeded : TaskState::kFailed,
                    std::memory_order_release);
}

}  // namespace

// InlineExecutor

void InlineExecutor::Submit(Task* task) { RunTask(task); }

void InlineExecutor::ParallelFor(int count, int /*min_batch*/,
                                 void (*fn)(int start, int end, void* ctx),
                                 void* ctx) {
  if (count > 0) fn(0, count, ctx);
}

void InlineExecutor::Wait(Task* /*task*/) {}

bool InlineExecutor::TryWait(Task* /*task*/) { return true; }

// ThreadPoolExecutor

ThreadPoolExecutor::ThreadPoolExecutor(Allocator* allocator, size_t num_threads)
    : num_threads_(num_threads),
      threads_(num_threads, allocator),
      queues_(num_threads, allocator),
      allocator_(allocator) {
  for (size_t i = 0; i < num_threads; ++i) {
    auto* buf = allocator->Alloc(sizeof(WorkerQueue), alignof(WorkerQueue));
    CHECK(buf != nullptr, "Failed to allocate WorkerQueue");
    auto* q = ::new (buf)
        WorkerQueue{CircularBuffer<Task*>(/*size=*/4096, allocator), {}};
    queues_.Push(q);
  }
}

ThreadPoolExecutor::~ThreadPoolExecutor() {
  Shutdown();
  for (size_t i = 0; i < queues_.size(); ++i) {
    queues_[i]->~WorkerQueue();
    allocator_->Dealloc(queues_[i], sizeof(WorkerQueue));
  }
}

size_t ThreadPoolExecutor::NumDefaultThreads() {
  const size_t n = std::thread::hardware_concurrency();
  return n > 1 ? n - 1 : 1;
}

void ThreadPoolExecutor::Start() {
  LOG("Starting thread pool with ", num_threads_, " threads.");
  for (size_t i = 0; i < num_threads_; ++i) {
    threads_.Push(std::thread(&ThreadPoolExecutor::WorkerLoop, this, i));
  }
}

void ThreadPoolExecutor::Shutdown() {
  {
    LockMutex l(wake_mu_);
    if (exit_) return;
    exit_ = true;
  }
  wake_cv_.notify_all();
  for (size_t i = 0; i < threads_.size(); ++i) {
    if (threads_[i].joinable()) threads_[i].join();
  }
}

void ThreadPoolExecutor::Submit(Task* task) {
  // With no workers there is nothing to run the task, and the long-running
  // tasks submitted here (the hot-reload watcher) would hang if run inline.
  CHECK(num_threads_ > 0, "Submit requires worker threads");
  task->state.store(TaskState::kPending, std::memory_order_relaxed);
  size_t idx =
      next_queue_.fetch_add(1, std::memory_order_relaxed) % num_threads_;

  // Try each queue with try_lock, fall back to blocking on the target queue.
  for (size_t attempt = 0; attempt < num_threads_; ++attempt) {
    size_t q = (idx + attempt) % num_threads_;
    LockMutex lock = LockMutex::TryLock(queues_[q]->mu);
    if (lock.owns_lock()) {
      queues_[q]->queue.Push(task);
      wake_cv_.notify_one();
      return;
    }
  }
  // All try_locks failed; block on the original target.
  {
    LockMutex l(queues_[idx]->mu);
    queues_[idx]->queue.Push(task);
  }
  wake_cv_.notify_one();
}

void ThreadPoolExecutor::Wait(Task* task) {
  // Check deadline for deadlock diagnosis.
  const auto deadline = task->deadline;
  const bool has_deadline = deadline != Time{};
  bool warned = false;

  while (task->state.load(std::memory_order_acquire) == TaskState::kPending) {
    if (has_deadline && !warned && Clock::now() > deadline) {
      LOG("WARNING: task exceeded its deadline — possible deadlock");
      warned = true;
    }
    // Help process work while waiting.
    bool did_work = false;
    for (size_t i = 0; i < num_threads_; ++i) {
      if (TryRunOne(i)) {
        did_work = true;
        break;
      }
    }
    if (!did_work) std::this_thread::yield();
  }
}

bool ThreadPoolExecutor::TryWait(Task* task) {
  if (task->state.load(std::memory_order_acquire) != TaskState::kPending) {
    return true;
  }
  // Try to help with one item before giving up.
  for (size_t i = 0; i < num_threads_; ++i) {
    if (TryRunOne(i)) break;
  }
  return task->state.load(std::memory_order_acquire) != TaskState::kPending;
}

void ThreadPoolExecutor::ParallelFor(int count, int min_batch,
                                     void (*fn)(int start, int end, void* ctx),
                                     void* ctx) {
  if (count <= 0) return;
  if (count <= min_batch || num_threads_ == 0) {
    fn(0, count, ctx);
    return;
  }

  int num_batches = (count + min_batch - 1) / min_batch;
  int max_batches = static_cast<int>(num_threads_) + 1;
  if (num_batches > max_batches) num_batches = max_batches;
  int batch_size = (count + num_batches - 1) / num_batches;

  struct ParallelContext {
    void (*fn)(int start, int end, void* ctx);
    void* ctx;
    int count;
    int batch_size;
    std::atomic<int> next_batch;
    std::atomic<int> completed;
    int total_batches;
  };

  ParallelContext pctx;
  pctx.fn = fn;
  pctx.ctx = ctx;
  pctx.count = count;
  pctx.batch_size = batch_size;
  pctx.next_batch.store(0, std::memory_order_relaxed);
  pctx.completed.store(0, std::memory_order_relaxed);
  pctx.total_batches = num_batches;

  auto batch_worker = [](void* ud) {
    auto* p = static_cast<ParallelContext*>(ud);
    while (true) {
      int batch = p->next_batch.fetch_add(1, std::memory_order_relaxed);
      if (batch >= p->total_batches) break;
      int start = batch * p->batch_size;
      int end = start + p->batch_size;
      if (end > p->count) end = p->count;
      p->fn(start, end, p->ctx);
      p->completed.fetch_add(1, std::memory_order_release);
    }
    return true;
  };

  // Submit tasks to workers. Caller also participates.
  constexpr int kMaxTasks = 64;
  Task tasks[kMaxTasks];
  int tasks_to_submit = num_batches - 1;
  if (tasks_to_submit > kMaxTasks) tasks_to_submit = kMaxTasks;

  for (int i = 0; i < tasks_to_submit; ++i) {
    tasks[i].fn = batch_worker;
    tasks[i].userdata = &pctx;
    tasks[i].cleanup = nullptr;
    tasks[i].state.store(TaskState::kPending, std::memory_order_relaxed);

    size_t idx =
        next_queue_.fetch_add(1, std::memory_order_relaxed) % num_threads_;
    {
      LockMutex l(queues_[idx]->mu);
      queues_[idx]->queue.Push(&tasks[i]);
    }
  }
  wake_cv_.notify_all();

  // Caller thread also processes batches.
  batch_worker(&pctx);

  // Wait for all submitted tasks to complete. We check task.state rather than
  // the completed counter to ensure workers have fully finished with the
  // stack-allocated Task structs before we return.
  for (int i = 0; i < tasks_to_submit; ++i) {
    while (tasks[i].state.load(std::memory_order_acquire) ==
           TaskState::kPending) {
      bool did_work = false;
      for (size_t q = 0; q < num_threads_; ++q) {
        if (TryRunOne(q)) {
          did_work = true;
          break;
        }
      }
      if (!did_work) std::this_thread::yield();
    }
  }
}

bool ThreadPoolExecutor::TryRunOne(size_t index) {
  Task* task;
  {
    LockMutex lock = LockMutex::TryLock(queues_[index]->mu);
    if (!lock.owns_lock() || queues_[index]->queue.empty()) return false;
    task = queues_[index]->queue.Pop();
  }
  RunTask(task);
  return true;
}

bool ThreadPoolExecutor::HasPendingWork() {
  for (size_t i = 0; i < num_threads_; ++i) {
    LockMutex lock = LockMutex::TryLock(queues_[i]->mu);
    if (lock.owns_lock() && !queues_[i]->queue.empty()) return true;
  }
  return false;
}

void ThreadPoolExecutor::WorkerLoop(size_t index) {
  FixedStringBuffer<16> name("pool-", index);
  SetCurrentThreadName(name.str());
  LOG("Started worker thread ", name.str());

  while (true) {
    // Try own queue first, then steal from others.
    bool did_work = TryRunOne(index);
    if (!did_work) {
      for (size_t i = 1; i < num_threads_; ++i) {
        size_t other = (index + i) % num_threads_;
        if (TryRunOne(other)) {
          did_work = true;
          break;
        }
      }
    }

    if (!did_work) {
      std::unique_lock<std::mutex> lock(wake_mu_);
      if (exit_) return;
      wake_cv_.wait(lock, [this] { return exit_ || HasPendingWork(); });
      if (exit_) return;
    }
  }
}

}  // namespace G
