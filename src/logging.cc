#include <cstdio>

#include "logging.h"

namespace G {
namespace {

const char* LevelToString(LogLevel level) {
  switch (level) {
    case LOG_LEVEL_FATAL:
      return "F";
    case LOG_LEVEL_INFO:
      return "I";
    default:
      return "?";
  }
}

void DefaultLog(LogLevel level, const char* message) {
  fprintf(stdout, "%s %s\n", LevelToString(level), message);
}

LogSink g_LogSink = DefaultLog;

}  // namespace

LogSink GetLogSink() { return g_LogSink; }

void SetLogSink(LogSink sink) { g_LogSink = sink; }

}  // namespace G