#pragma once
#ifndef _GAME_THREAD_H
#define _GAME_THREAD_H

#include <chrono>
#include <mutex>
#include <thread>

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace G {

// Set the OS-level name for the calling thread (visible in
// debuggers/profilers).
inline void SetCurrentThreadName(const char* name) {
#if defined(__linux__)
  pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
  pthread_setname_np(name);
#elif defined(_WIN32)
  wchar_t wname[16];
  size_t i = 0;
  for (; name[i] && i < 15; ++i) wname[i] = static_cast<wchar_t>(name[i]);
  wname[i] = 0;
  SetThreadDescription(GetCurrentThread(), wname);
#endif
}

// RAII lock guard for std::mutex.
struct LockMutex {
  explicit LockMutex(std::mutex& mutex) : mu_(mutex), locked_(true) {
    mu_.lock();
  }

  ~LockMutex() {
    if (locked_) mu_.unlock();
  }

  // Try to acquire the lock without blocking. Returns a LockMutex that may
  // or may not own the lock — check owns_lock() before accessing shared state.
  static LockMutex TryLock(std::mutex& mutex) {
    return LockMutex(mutex, std::try_to_lock);
  }

  // Whether this guard holds the lock.
  bool owns_lock() const { return locked_; }

  // Access the underlying mutex (e.g. for condition_variable::wait).
  std::mutex& mutex() { return mu_; }

  LockMutex(const LockMutex&) = delete;
  LockMutex& operator=(const LockMutex&) = delete;

 private:
  LockMutex(std::mutex& mutex, std::try_to_lock_t)
      : mu_(mutex), locked_(mu_.try_lock()) {}

  std::mutex& mu_;
  bool locked_;
};

// Sleep the current thread for the given number of milliseconds.
inline void SleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace G

#endif  // _GAME_THREAD_H
