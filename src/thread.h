#pragma once
#ifndef _GAME_THREAD_H
#define _GAME_THREAD_H

#include <chrono>
#include <mutex>
#include <thread>

namespace G {

// RAII lock guard for std::mutex.
struct LockMutex {
  explicit LockMutex(std::mutex& mutex) : mu_(mutex) { mu_.lock(); }

  ~LockMutex() { mu_.unlock(); }

  LockMutex(const LockMutex&) = delete;
  LockMutex& operator=(const LockMutex&) = delete;

 private:
  std::mutex& mu_;
};

// Sleep the current thread for the given number of milliseconds.
inline void SleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace G

#endif  // _GAME_THREAD_H
