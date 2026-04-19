#pragma once
#ifndef _GAME_CLOCK_H
#define _GAME_CLOCK_H

#include <chrono>

#include "logging.h"
#include "stringlib.h"

namespace G {

// Aliases for std::chrono types. Monotonic clock unaffected by system time.
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using Duration = Clock::duration;

// Current monotonic time.
Time Now();

// Current monotonic time as seconds (convenience for arithmetic).
double NowInSeconds();

// Convert a duration to seconds.
inline double ToSeconds(Duration d) {
  return std::chrono::duration<double>(d).count();
}

class LogTimer {
 public:
  using Buf = FixedStringBuffer<kMaxLogLineLength>;

  LogTimer(const char* file, int line, const char* func, Buf&& buf = {})
      : file_(file), line_(line), func_(func), start_(Now()) {
    auto s = buf.view();
    std::memcpy(buf_, s.data(), s.size());
    buf_[s.size()] = '\0';
  }

  ~LogTimer() {
    const double ms = ToSeconds(Now() - start_) * 1000.0;
    Log(file_, line_, buf_[0] == '\0' ? func_ : "", buf_, " elapsed ", ms,
        "ms");
  }

 private:
  const char* file_;
  int line_;
  const char* func_;

  char buf_[kMaxLogLineLength + 1];

  Time start_;
};

#define INTERNAL_ID_I1(x, y) x##y
#define INTERNAL_ID_I(x, y) INTERNAL_ID_I1(x, y)
#define INTERNAL_ID(x) INTERNAL_ID_I(x, __COUNTER__)

#define TIMER(...)                                                 \
  LogTimer INTERNAL_ID(t)(__FILE__, __LINE__, __PRETTY_FUNCTION__, \
                          LogTimer::Buf(__VA_ARGS__))

// Milliseconds elapsed since a given time point.
inline float ElapsedMs(Time since) {
  return static_cast<float>(ToSeconds(Now() - since) * 1000.0);
}

inline constexpr double TimeStepInSeconds() { return 1.0 / 60.0; }

}  // namespace G

#endif  // _GAME_CLOCK_H
