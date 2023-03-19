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

  LogTimer(const char* file, int line, const char* func, Buf&& buf = {})
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

inline constexpr double TimeStepInSeconds() { return 1.0 / 60.0; }

}  // namespace G

#endif  // _GAME_CLOCK_H