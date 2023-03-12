#pragma once
#ifndef _GAME_CONSOLE_H
#define _GAME_CONSOLE_H

#include <deque>
#include <string>

#include "SDL.h"
#include "allocators.h"
#include "circular_buffer.h"
#include "map.h"
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

  void AddWatcher(std::string_view key, std::string_view value) {
    if (Linebuffer * buffer; watcher_values_.Lookup(key, &buffer)) {
      CopyToBuffer(value, buffer);
      return;
    }
    auto* buffer = buffers_->AllocArray<Linebuffer>(1);
    CopyToBuffer(value, buffer);
    auto inserted_key = watcher_values_.Insert(key, buffer);
    watcher_keys_.Push(inserted_key);
  }

  template <typename Fn>
  void ForAllWatchers(Fn&& fn) {
    for (size_t i = 0; i < watcher_keys_.size(); ++i) {
      const auto& key = watcher_keys_[i];
      if (Linebuffer * val; watcher_values_.Lookup(key, &val)) {
        fn(key, std::string_view(val->chars, val->len));
      }
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

  void CopyToBuffer(std::string_view text, Linebuffer* buffer) {
    const size_t length = std::min(kMaxLogLineLength, text.size());
    std::memcpy(buffer->chars, text.data(), length);
    buffer->len = length;
  }

  void LogLine(std::string_view text) {
    Linebuffer* buffer = nullptr;
    if (lines_.full()) {
      buffer = lines_.Pop();
    } else {
      buffer = buffers_->AllocArray<Linebuffer>(1);
    }
    CopyToBuffer(text, buffer);
    lines_.Push(buffer);
  }

  FixedArena<2 * kMaxLines * sizeof(Linebuffer), BumpAllocator> buffers_;
  FixedCircularBuffer<Linebuffer*, kMaxLines> lines_;
  SDL_LogOutputFunction log_fn_;
  void* log_fn_userdata_;

  inline static constexpr size_t kMaxWatchers = 128;

  FixedArray<std::string_view, kMaxWatchers> watcher_keys_;
  LookupTable<Linebuffer*> watcher_values_;
};

}  // namespace G

#endif  // _GAME_CONSOLE_H