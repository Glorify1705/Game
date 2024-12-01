#include "thread_pool.h"

#include "logging.h"
#include "thread.h"

namespace G {

ThreadPool::ThreadPool(Allocator* allocator, size_t num_threads)
    : threads_(num_threads, allocator),
      user_data_(num_threads, allocator),
      num_threads_(num_threads),
      work_(kMaxFunctions, allocator) {}

ThreadPool::~ThreadPool() {
  Stop();
  SDL_DestroyMutex(mu_);
  SDL_DestroyCond(cv_);
}

void ThreadPool::Queue(int (*fn)(void*), void* userdata) {
  CHECK(mu_ != nullptr, "Thread pool not initialized");
  {
    LockMutex l(mu_);
    Work work = {fn, userdata};
    work_.Push(work);
  }
  SDL_CondSignal(cv_);
}

void ThreadPool::Start() {
  LOG("Starting thread pool with ", num_threads_, " threads.");
  CHECK(mu_ == nullptr, "Thread pool initialized twice");
  mu_ = SDL_CreateMutex();
  cv_ = SDL_CreateCond();
  LockMutex l(mu_);
  for (size_t i = 0; i < num_threads_; ++i) {
    FixedStringBuffer<32> thread_name("Thread", i);
    user_data_.Push(UserData{this, i});
    threads_.Push(
        SDL_CreateThread(LoopFn, thread_name.str(), &user_data_.back()));
  }
}

int ThreadPool::Loop(size_t index) {
  LOG("Started thread ", index);
  while (true) {
    SDL_LockMutex(mu_);
    while (work_.empty() && !exit_) {
      SDL_CondWait(cv_, mu_);
    }
    if (exit_) {
      SDL_UnlockMutex(mu_);
      return 0;
    }
    Work fn = work_.Pop();
    SDL_UnlockMutex(mu_);
    int result = fn.fn(fn.userdata);
    if (result != 0) {
      SDL_UnlockMutex(mu_);
      return result;
    }
  }
  return 0;
}

void ThreadPool::Wait() {
  for (size_t i = 0; i < threads_.size(); ++i) {
    int status;
    SDL_WaitThread(threads_[i], &status);
    CHECK(status == 0, "Abnormal termination of thread: ", i);
  }
}

void ThreadPool::Stop() {
  LOG("Stopping all threads");
  {
    LockMutex l(mu_);
    exit_ = true;
  }
  SDL_CondBroadcast(cv_);
}

}  // namespace G
