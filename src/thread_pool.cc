#include "thread_pool.h"

#include "logging.h"
#include "thread.h"

namespace G {

ThreadPool::ThreadPool(Allocator* allocator, size_t num_threads)
    : threads_(num_threads, allocator),
      num_threads_(num_threads),
      work_(kMaxFunctions, allocator) {}

ThreadPool::~ThreadPool() = default;

void ThreadPool::Queue(int (*fn)(void*), void* userdata) {
  {
    LockMutex l(mu_);
    Work work = {fn, userdata};
    work_.Push(work);
  }
  cv_.notify_one();
}

void ThreadPool::Start() {
  LOG("Starting thread pool with ", num_threads_, " threads.");
  LockMutex l(mu_);
  for (size_t i = 0; i < num_threads_; ++i) {
    threads_.Push(std::thread(&ThreadPool::Loop, this, i));
  }
}

int ThreadPool::Loop(size_t index) {
  LOG("Started thread ", index);
  while (true) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return !work_.empty() || exit_; });
    if (exit_) return 0;
    Work fn = work_.Pop();
    lock.unlock();
    int result = fn.fn(fn.userdata);
    if (result != 0) return result;
  }
  return 0;
}

void ThreadPool::Wait() {
  for (size_t i = 0; i < threads_.size(); ++i) {
    if (threads_[i].joinable()) threads_[i].join();
  }
}

void ThreadPool::Stop() {
  LOG("Stopping all threads");
  {
    LockMutex l(mu_);
    exit_ = true;
  }
  cv_.notify_all();
}

}  // namespace G
