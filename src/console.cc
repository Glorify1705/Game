#include "console.h"

namespace G {
namespace {

constexpr const char* kPriorities[SDL_NUM_LOG_PRIORITIES] = {
    nullptr, "VERBOSE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};

}

void DebugConsole::Log(int category, SDL_LogPriority priority,
                       const char* message) {
  log_fn_(log_fn_userdata_, category, priority, message);
  Log(kPriorities[priority], " ", message);
}

void DebugConsole::LogWithConsole(void* userdata, int category,
                                  SDL_LogPriority priority,
                                  const char* message) {
  static_cast<DebugConsole*>(userdata)->Log(category, priority, message);
}

void DebugConsole::CopyToBuffer(std::string_view text, Linebuffer* buffer) {
  const size_t length = std::min(kMaxLogLineLength, text.size());
  std::memcpy(buffer->chars, text.data(), length);
  buffer->len = length;
}

void DebugConsole::LogLine(std::string_view text) {
  Linebuffer* buffer = nullptr;
  if (lines_.full()) {
    buffer = lines_.Pop();
  } else {
    buffer = buffers_->AllocArray<Linebuffer>(1);
  }
  CopyToBuffer(text, buffer);
  lines_.Push(buffer);
}

}  // namespace G