#pragma once
#ifndef _GAME_LOGGING_H
#define _GAME_LOGGING_H

#include <string_view>

#include "constants.h"
#include "stringlib.h"

namespace G {

// Log severity levels, ordered from most to least severe.
enum class LogLevel : uint8_t {
  kFatal,  // Unrecoverable - crash after logging
  kError,  // Wrong but recoverable
  kWarn,   // Suspicious but possibly fine
  kInfo,   // Normal operational messages
  kDebug,  // Developer diagnostics - compiled out in release
  kTrace,  // High-frequency diagnostics - compiled out in release
};

// Compile-time log level threshold. Levels above this are eliminated entirely.
#ifdef GAME_WITH_ASSERTS
inline constexpr LogLevel kCompileTimeLogLevel = LogLevel::kTrace;
#else
inline constexpr LogLevel kCompileTimeLogLevel = LogLevel::kInfo;
#endif

// Log channels for per-subsystem filtering.
enum class LogChannel : uint8_t {
  kGeneral,   // Default channel
  kGraphics,  // Rendering and GPU
  kPhysics,   // Physics simulation
  kAudio,     // Sound and music
  kInput,     // Input handling
  kAssets,    // Asset loading and management
  kLua,       // Lua scripting
  kCount,
};

#ifdef GAME_WITH_ASSERTS
// Runtime level per channel (debug builds only).
inline LogLevel g_channel_levels[static_cast<size_t>(LogChannel::kCount)] = {
    LogLevel::kTrace, LogLevel::kTrace, LogLevel::kTrace, LogLevel::kTrace,
    LogLevel::kTrace, LogLevel::kTrace, LogLevel::kTrace,
};

// Sets the runtime log level for a channel (debug builds only).
void SetChannelLevel(LogChannel channel, LogLevel level);

// Gets the runtime log level for a channel (debug builds only).
LogLevel GetChannelLevel(LogChannel channel);
#endif

using LogSink = void (*)(LogLevel /*lvl*/, const char* /*message*/);

// Sets the sink for log messages.
void SetLogSink(LogSink sink);

// Gets the function for logging messages.
LogSink GetLogSink();

using CrashHandler = void (*)(const char* message);

// Sets the sink for log messages.
void SetCrashHandler(CrashHandler sink);

// Crashes the binary.
[[noreturn]] void Crash(const char* message);

// Installs signal handlers for SIGSEGV, SIGBUS, SIGFPE, etc. that print a
// stack trace before crashing. No-op in release builds. Call once at startup.
void InstallSignalHandlers();

// Gets the function for logging messages.
LogSink SetCrashHandler();

// Trims the path to be the last part of the path (basename).
std::string_view TrimPath(std::string_view f);

// Logs a message at the given level with file and line information.
template <typename... T>
void LogAt(LogLevel level, std::string_view file, int line, T&&... ts) {
  FixedStringBuffer<kMaxLogLineLength> buf(kTruncating);
  buf.Append("[", TrimPath(file), ":", line, "] ");
  buf.Append<T...>(std::forward<T>(ts)...);
  GetLogSink()(level, buf.str());
}

template <typename... T>
[[noreturn]] void Crash(std::string_view file, int line, T&&... ts) {
  FixedStringBuffer<kMaxLogLineLength> buf(kTruncating);
  buf.Append("[", TrimPath(file), ":", line, "] ");
  buf.Append<T...>(std::forward<T>(ts)...);
  GetLogSink()(LogLevel::kFatal, buf.str());
  Crash(buf.str());
}

template <typename... T>
void Log(std::string_view file, int line, T&&... ts) {
  FixedStringBuffer<kMaxLogLineLength> buf(kTruncating);
  buf.Append("[", TrimPath(file), ":", line, "] ");
  buf.Append<T...>(std::forward<T>(ts)...);
  GetLogSink()(LogLevel::kInfo, buf.str());
}

struct OpenGLSourceLine {
  char file[kMaxPathLength] = {0};
  size_t line = 0;
  FixedStringBuffer<kMaxLogLineLength> buffer{kTruncating};
};

inline OpenGLSourceLine* GetOpenGLSourceLine() {
  static OpenGLSourceLine l;
  return &l;
}

template <typename... T>
inline void SetOpenGLLine([[maybe_unused]] const char* file,
                          [[maybe_unused]] size_t line,
                          [[maybe_unused]] T&&... ts) {
#ifdef GAME_WITH_ASSERTS
  auto* ptr = GetOpenGLSourceLine();
  std::memcpy(&ptr->file, file, strlen(file) + 1);
  ptr->buffer.Clear();
  ptr->buffer.Append<T...>(std::forward<T>(ts)...);
  ptr->line = line;
#endif
}

template <typename T>
[[nodiscard]] T InternalDieIfNull(const char* filename, int line,
                                  const char* expr, T&& t) {
  if (t == nullptr) {
    Crash(filename, line, expr, " is null");
  }
  return std::forward<T>(t);
}

}  // namespace G

#define CHECK(cond, ...) \
  if (!(cond)) ::G::Crash(__FILE__, __LINE__, #cond, " ", ##__VA_ARGS__)

#define DIE(...) ::G::Crash(__FILE__, __LINE__, ##__VA_ARGS__)

// Logs a message at the given level. Levels above kCompileTimeLogLevel are
// eliminated entirely by the compiler - no string literals, no formatting.
#define LOG_AT(level, ...)                                    \
  do {                                                        \
    if constexpr ((level) <= ::G::kCompileTimeLogLevel) {     \
      ::G::LogAt((level), __FILE__, __LINE__, ##__VA_ARGS__); \
    }                                                         \
  } while (0)

#define LOG(...) LOG_AT(::G::LogLevel::kInfo, __VA_ARGS__)
#define ELOG(...) LOG_AT(::G::LogLevel::kError, __VA_ARGS__)
#define WLOG(...) LOG_AT(::G::LogLevel::kWarn, __VA_ARGS__)
#define DLOG(...) LOG_AT(::G::LogLevel::kDebug, __VA_ARGS__)
#define TLOG(...) LOG_AT(::G::LogLevel::kTrace, __VA_ARGS__)

// Logs with per-subsystem channel filtering (runtime check, debug builds only).
#define CLOG(channel, level, ...)                                           \
  do {                                                                      \
    if constexpr ((level) <= ::G::kCompileTimeLogLevel) {                   \
      if ((level) <= ::G::g_channel_levels[static_cast<size_t>(channel)]) { \
        ::G::LogAt((level), __FILE__, __LINE__, ##__VA_ARGS__);             \
      }                                                                     \
    }                                                                       \
  } while (0)

#define DONOTSUBMIT LOG

#ifndef GAME_WITH_ASSERTS
#define DCHECK(expr, ...) (void)(expr)
#else
#define DCHECK(...) CHECK(__VA_ARGS__)
#endif

#define OPENGL_CALL(f, ...)                                \
  do {                                                     \
    ::G::SetOpenGLLine(__FILE__, __LINE__, ##__VA_ARGS__); \
    f;                                                     \
  } while (0)

#define NOTNULL(x) ::G::InternalDieIfNull(__FILE__, __LINE__, #x, (x))

#endif  // _GAME_LOGGING_H
