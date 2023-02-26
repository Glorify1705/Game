#pragma once
#ifndef _GAME_CLOCK_H
#define _GAME_CLOCK_H

#include <cstdint>
#include <limits>
#include <string>

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

inline constexpr double TimeStepInMillis() { return 60.0 / 1000.0; }

#endif  // _GAME_CLOCK_H