#pragma once
#ifndef _GAME_LOGGING_H
#define _GAME_LOGGING_H

#include <cstdlib>
#include <sstream>

#include "strings.h"

namespace G {

enum LogLevel { LOG_LEVEL_INFO, LOG_LEVEL_FATAL };

using LogSink = void (*)(LogLevel /*lvl*/, const char* /*message*/);

inline constexpr size_t kMaxLogLineLength = 511;

// Sets the sink for log messages.
void SetLogSink(LogSink sink);

// Gets the function for logging messages.
LogSink GetLogSink();

using CrashHandler = void (*)(const char* message);

// Sets the sink for log messages.
void SetCrashHandler(CrashHandler sink);

// Crashes the binary.
[[noreturn]] void Crash(const char* message);

// Gets the function for logging messages.
LogSink SetCrashHandler();

template <typename... T>
[[noreturn]] void Crash(const char* file, int line, T... ts) {
  StringBuffer<kMaxLogLineLength> buf("[", file, ":", line, "] ");
  buf.Append(std::forward<T>(ts)...);
  GetLogSink()(LOG_LEVEL_FATAL, buf.str());
  Crash(buf.str());
}

template <typename... T>
void Log(const char* file, int line, T... ts) {
  StringBuffer<kMaxLogLineLength> buf("[", file, ":", line, "] ");
  buf.Append(std::forward<T>(ts)...);
  GetLogSink()(LOG_LEVEL_INFO, buf.str());
}

}  // namespace G

#define CHECK(cond, ...) \
  if (!(cond)) G::Crash(__FILE__, __LINE__, #cond, " ", ##__VA_ARGS__)

#define DIE(...) G::Crash(__FILE__, __LINE__, ##__VA_ARGS__)

#define LOG(...) G::Log(__FILE__, __LINE__, ##__VA_ARGS__)

#define GAME_WITH_ASSERTS true

#ifndef GAME_WITH_ASSERTS
#define DCHECK(...)
#else
#define DCHECK(...) CHECK(__VA_ARGS__)
#endif

#endif  // _GAME_LOGGING_H