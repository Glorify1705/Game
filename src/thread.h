#pragma once
#ifndef _GAME_THREAD_H
#define _GAME_THREAD_H

#include <mutex>

namespace G {

// RAII lock guard for std::mutex. Equivalent to std::lock_guard but
// keeps the existing name used throughout the codebase.
struct LockMutex {
  explicit LockMutex(std::mutex& mutex) : mu_(mutex) { mu_.lock(); }

  ~LockMutex() { mu_.unlock(); }

  LockMutex(const LockMutex&) = delete;
  LockMutex& operator=(const LockMutex&) = delete;

 private:
  std::mutex& mu_;
};

}  // namespace G

#endif  // _GAME_THREAD_H
