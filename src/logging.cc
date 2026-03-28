#include "logging.h"

#include <cstdio>

namespace G {
namespace {

const char* LevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::kFatal:
      return "F";
    case LogLevel::kError:
      return "E";
    case LogLevel::kWarn:
      return "W";
    case LogLevel::kInfo:
      return "I";
    case LogLevel::kDebug:
      return "D";
    case LogLevel::kTrace:
      return "T";
  }
  return "?";
}

void DefaultLog(LogLevel level, const char* message) {
  fprintf(stdout, "%s %s\n", LevelToString(level), message);
}

[[noreturn]] void DefaultCrashHandler(const char* /*message*/) { std::abort(); }

LogSink g_LogSink = DefaultLog;

CrashHandler g_CrashHandler = DefaultCrashHandler;

}  // namespace

std::string_view TrimPath(std::string_view f) {
  if (f.empty()) return f;
  int p = f.size() - 1;
  while (p >= 0 && f[p] != '\\' && f[p] != '/') p--;
  return f.substr(p < 0 ? 0 : (p + 1));
}

LogSink GetLogSink() { return g_LogSink; }

void SetLogSink(LogSink sink) { g_LogSink = sink; }

void SetCrashHandler(CrashHandler handler) { g_CrashHandler = handler; }

[[noreturn]] void Crash(const char* message) {
  g_CrashHandler(message);
  std::abort();
}

#ifdef GAME_WITH_ASSERTS
void SetChannelLevel(LogChannel channel, LogLevel level) {
  g_channel_levels[static_cast<size_t>(channel)] = level;
}

LogLevel GetChannelLevel(LogChannel channel) {
  return g_channel_levels[static_cast<size_t>(channel)];
}
#endif

}  // namespace G
