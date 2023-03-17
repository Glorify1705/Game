#pragma once
#ifndef _GAME_CLOCK_H
#define _GAME_CLOCK_H

#include <cstdint>
#include <limits>
#include <string>

#include "array.h"
#include "logging.h"

namespace G {

double NowInSeconds();

class LogTimer {
 public:
  using Buf = StringBuffer<kMaxLogLineLength>;

  LogTimer(const char* file, int line, const char* func, Buf buf)
      : file_(file), line_(line), func_(func), start_(NowInSeconds()) {
    auto s = buf.piece();
    std::memcpy(buf_, s.data(), s.size());
    buf_[s.size()] = '\0';
  }

  ~LogTimer() {
    const double time = NowInSeconds() - start_;
    Log(file_, line_, buf_[0] == '\0' ? func_ : "", buf_, " elapsed ", time,
        "ms");
  }

 private:
  const char* file_;
  int line_;
  const char* func_;

  char buf_[kMaxLogLineLength + 1];

  double start_;
};

#define INTERNAL_ID_I1(x, y) x##y
#define INTERNAL_ID_I(x, y) INTERNAL_ID_I1(x, y)
#define INTERNAL_ID(x) INTERNAL_ID_I(x, __COUNTER__)

#define TIMER(...)                                                 \
  LogTimer INTERNAL_ID(t)(__FILE__, __LINE__, __PRETTY_FUNCTION__, \
                          LogTimer::Buf(__VA_ARGS__))

class Events {
 public:
  using QueueCall = void (*)(void*);
  void QueueAt(double t, QueueCall call, void* userdata) {
    timer_.Push(t);
    calls_.Push(call);
    userdata_.Push(userdata);
  }

  void QueueIn(double dt, QueueCall call, void* userdata) {
    QueueAt(t_ + dt, call, userdata);
  }

  void Fire(double dt) {
    t_ += dt;
    for (size_t i = 0; i < userdata_.size(); ++i) {
      if (timer_[i] > t_) continue;
      calls_[i](userdata_[i]);
      timer_[i] = 0;
    }
    size_t pos = 0;
    for (size_t i = 0; i < timer_.size(); ++i) {
      if (timer_[i] > t_) {
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
  FixedArray<double, 1024> timer_;
  FixedArray<QueueCall, 1024> calls_;
  FixedArray<void*, 1024> userdata_;
  double t_ = 0;
};

inline constexpr double TimeStepInSeconds() { return 1.0 / 60.0; }

}  // namespace G

#endif  // _GAME_CLOCK_H