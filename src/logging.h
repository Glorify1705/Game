#pragma once
#ifndef _GAME_LOGGING_H
#define _GAME_LOGGING_H

#include <cstdlib>
#include <sstream>
#include <string_view>

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

// Trims the path to be the last part of the path (basename).
std::string_view TrimPath(std::string_view f);

template <typename... T>
[[noreturn]] void Crash(std::string_view file, int line, T&&... ts) {
  FixedStringBuffer<kMaxLogLineLength> buf("[", TrimPath(file), ":", line,
                                           "] ");
  buf.Append<T...>(std::forward<T>(ts)...);
  GetLogSink()(LOG_LEVEL_FATAL, buf.str());
  Crash(buf.str());
}

template <typename... T>
void Log(std::string_view file, int line, T&&... ts) {
  FixedStringBuffer<kMaxLogLineLength> buf("[", TrimPath(file), ":", line,
                                           "] ");
  buf.Append<T...>(std::forward<T>(ts)...);
  GetLogSink()(LOG_LEVEL_INFO, buf.str());
}

}  // namespace G

#define CHECK(cond, ...) \
  if (!(cond)) G::Crash(__FILE__, __LINE__, #cond, " ", ##__VA_ARGS__)

#define DIE(...) G::Crash(__FILE__, __LINE__, ##__VA_ARGS__)

#define LOG(...) G::Log(__FILE__, __LINE__, ##__VA_ARGS__)

#ifndef GAME_WITH_ASSERTS
#define DCHECK(expr, ...) expr
#else
#define DCHECK(...) CHECK(__VA_ARGS__)
#endif

#endif  // _GAME_LOGGING_H