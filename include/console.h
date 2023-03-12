#pragma once
#ifndef _GAME_CONSOLE_H
#define _GAME_CONSOLE_H

#include <deque>
#include <string>

#include "SDL.h"
#include "allocators.h"
#include "strings.h"

namespace G {

class DebugConsole {
 public:
  DebugConsole() {
    SDL_LogGetOutputFunction(&log_fn_, &log_fn_userdata_);
    SDL_LogSetOutputFunction(LogWithConsole, this);
  }
  ~DebugConsole() { SDL_LogSetOutputFunction(log_fn_, log_fn_userdata_); }

  template <typename... Ts>
  void Log(Ts... ts) {
    StringBuffer<kMaxLogLineLength> buf(std::forward<Ts>(ts)...);
    LogLine(buf.piece());
  }

  template <typename Fn>
  void ForAllLines(Fn&& fn) {
    for (size_t i = 0; i < lines_.size(); ++i) {
      Linebuffer* buffer = lines_[i];
      fn(std::string_view(buffer->chars, buffer->len));
    }
  }

 private:
  inline static constexpr size_t kMaxLines = 1024;

  struct Linebuffer {
    size_t len = 0;
    char chars[kMaxLogLineLength + 1];
  };

  inline static constexpr const char* kPriorities[SDL_NUM_LOG_PRIORITIES] = {
      nullptr, "VERBOSE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};

  void Log(int category, SDL_LogPriority priority, const char* message) {
    log_fn_(log_fn_userdata_, category, priority, message);
    Log(kPriorities[priority], " ", message);
  }

  static void LogWithConsole(void* userdata, int category,
                             SDL_LogPriority priority, const char* message) {
    static_cast<DebugConsole*>(userdata)->Log(category, priority, message);
  }

  void LogLine(std::string_view text) {
    Linebuffer* buffer = nullptr;
    if (lines_.full()) {
      buffer = lines_.Pop();
    } else {
      buffer = buffers_->AllocArray<Linebuffer>(1);
    }
    const size_t length = std::min(kMaxLogLineLength, text.size());
    std::memcpy(buffer->chars, text.data(), length);
    buffer->len = length;
    lines_.Push(buffer);
  }

  FixedArena<kMaxLines * sizeof(Linebuffer), BumpAllocator> buffers_;
  FixedCircularBuffer<Linebuffer*, kMaxLines> lines_;
  SDL_LogOutputFunction log_fn_;
  void* log_fn_userdata_;
};

}  // namespace G

#endif  // _GAME_CONSOLE_H