#pragma once
#ifndef _GAME_CLOCK_H
#define _GAME_CLOCK_H

#include <cstdint>
#include <limits>
#include <string>

#include "array.h"
#include "logging.h"

double NowInMillis();

class LogTimer {
 public:
  LogTimer(const char* file, int line, const char* func)
      : file_(file), line_(line), func_(func) {
    start_ = NowInMillis();
  }

  ~LogTimer() {
    const double time = NowInMillis() - start_;
    Log(file_, line_, func_, " elapsed ", time, "ms");
  }

 private:
  const char* file_;
  int line_;
  const char* func_;

  double start_;
};

#define TIMER(...) \
  LogTimer t##__COUNTER__(__FILE__, __LINE__, __PRETTY_FUNCTION__)

class Events {
 public:
  using QueueCall = void (*)(void*);
  void QueueAt(uint64_t frame, QueueCall call, void* userdata) {
    timer_.Push(frame);
    calls_.Push(call);
    userdata_.Push(userdata);
  }

  void QueueIn(uint64_t frame, QueueCall call, void* userdata) {
    QueueAt(frame_ + frame, call, userdata);
  }

  uint64_t frame() const { return frame_; }

  void Fire(double dt) {
    frame_++;
    t_ += dt;
    for (size_t i = 0; i < userdata_.size(); ++i) {
      if (timer_[i] > frame_) continue;
      calls_[i](userdata_[i]);
      timer_[i] = 0;
    }
    size_t pos = 0;
    for (size_t i = 0; i < timer_.size(); ++i) {
      if (timer_[i] > frame_) {
        std::swap(timer_[i], timer_[pos]);
        std::swap(calls_[i], calls_[pos]);
        std::swap(userdata_[i], userdata_[pos]);
        pos++;
      }
    }
    timer_.Resize(pos);
    calls_.Resize(pos);
    userdata_.Resize(pos);
  }

 private:
  FixedArray<uint64_t, 1024> timer_;
  FixedArray<QueueCall, 1024> calls_;
  FixedArray<void*, 1024> userdata_;
  uint64_t frame_ = 0;
  double t_ = 0;
};

inline constexpr double TimeStepInMillis() { return 60.0 / 1000.0; }

#endif  // _GAME_CLOCK_H