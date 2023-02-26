#pragma once
#ifndef _GAME_LOGGING_H
#define _GAME_LOGGING_H

#include <cstdlib>
#include <sstream>

#include "SDL.h"
#include "strings.h"

namespace internal_strings {

inline const char* ConsumePrefix(const char* buf, const char* str) {
  size_t i = 0;
  while (buf[i] != '\0' && str[i] != '\0' && buf[i] == str[i]) {
    i++;
  }
  if (str[i] == '\0') return &buf[i];
  return buf;
}

}  // namespace internal_strings

template <typename... T>
[[noreturn]] void Crash(const char* file, int line, T... ts) {
  StringBuffer<257> buf;
  buf.Append(std::forward<T>(ts)...);
  SDL_LogCritical(SDL_LOG_PRIORITY_INFO, "[%s:%d] %s",
                  internal_strings::ConsumePrefix(file, "src/"), line,
                  buf.str());
  SDL_TriggerBreakpoint();
  __builtin_trap();
}

template <typename... T>
void Log(const char* file, int line, T... ts) {
  StringBuffer<257> buf;
  buf.Append(std::forward<T>(ts)...);
  SDL_LogInfo(SDL_LOG_PRIORITY_INFO, "[%s:%d] %s",
              internal_strings::ConsumePrefix(file, "src/"), line, buf.str());
}

#define CHECK(cond, ...) \
  if (!(cond)) Crash(__FILE__, __LINE__, #cond, " ", ##__VA_ARGS__)

#define DIE(...) Crash(__FILE__, __LINE__, ##__VA_ARGS__)

#define LOG(...) Log(__FILE__, __LINE__, ##__VA_ARGS__)

#ifndef GAME_WITH_ASSERTS
#define DCHECK(...)
#else
#define DCHECK(...) CHECK(__VA_ARGS__)
#endif

#endif  // _GAME_LOGGING_H