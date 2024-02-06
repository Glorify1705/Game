#include "logging.h"
#include "thread_pool.h"

namespace G {

struct LockMutex {
  explicit LockMutex(SDL_mutex* mutex) : mu(mutex) { SDL_LockMutex(mu); }

  ~LockMutex() {
    if (mu) SDL_UnlockMutex(mu);
  }

  SDL_mutex* Release() {
    SDL_mutex* result = mu;
    SDL_UnlockMutex(mu);
    mu = nullptr;
    return result;
  }

  SDL_mutex* mu;
};

ThreadPool::ThreadPool(Allocator* allocator, size_t num_threads)
    : threads_(num_threads, allocator),
      user_data_(num_threads, allocator),
      work_(allocator) {
  for (size_t i = 0; i < num_threads; ++i) threads_[i] = nullptr;
}

ThreadPool::~ThreadPool() {
  {
    LockMutex l(mu_);
    exit_ = true;
  }
  SDL_CondBroadcast(cv_);
  for (size_t i = 0; i < threads_.size(); ++i) {
    int status;
    SDL_WaitThread(threads_[i], &status);
    CHECK(status == 0, "Abnormal termination of thread: ", i);
  }
  SDL_DestroyMutex(mu_);
  SDL_DestroyCond(idle_cv_);
  SDL_DestroyCond(cv_);
}

void ThreadPool::Queue(int (*fn)(void*), void* userdata) {
  {
    LockMutex l(mu_);
    Work work;
    work.fn = fn;
    work.userdata = userdata;
    work_.Push(work);
  }
  SDL_CondSignal(cv_);
}

void ThreadPool::Start() {
  CHECK(mu_ == nullptr, "Thread pool initialized twice");
  mu_ = SDL_CreateMutex();
  cv_ = SDL_CreateCond();
  for (size_t i = 0; i < threads_.size(); ++i) {
    threads_[i] = SDL_CreateThread(LoopFn, "Thread1", this);
  }
}

int ThreadPool::Loop(size_t /*index*/) {
  while (true) {
    LockMutex l(mu_);
    --inflight_;
    if (work_.empty() && inflight_ == 0) {
      SDL_CondBroadcast(idle_cv_);
    }
    if (exit_) return 0;
    if (work_.empty()) {
      SDL_CondWait(cv_, l.mu);
      // Here l.mu is reacquired.
      continue;
    }
    Work& fn = work_.Pop();
    int result = fn.fn(fn.userdata);
    if (result != 0) return result;
  }
  return 0;
}

void ThreadPool::Wait() {
  LockMutex l(mu_);
  SDL_CondWait(idle_cv_, mu_);
  // Here l.mu is reacquired.
}

}  // namespace G