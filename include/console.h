#pragma once
#ifndef _GAME_CONSOLE_H
#define _GAME_CONSOLE_H

#include <deque>
#include <string>

#include "SDL.h"
#include "allocators.h"
#include "array.h"
#include "circular_buffer.h"
#include "lookup_table.h"
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

  template <typename... Ts>
  void AddWatcher(std::string_view key, Ts... ts) {
    StringBuffer<kMaxLogLineLength> buf(std::forward<Ts>(ts)...);
    if (Linebuffer * linebuffer; watcher_values_.Lookup(key, &linebuffer)) {
      CopyToBuffer(buf.piece(), linebuffer);
      return;
    }
    auto* linebuffer = buffers_->AllocArray<Linebuffer>(1);
    CopyToBuffer(buf.piece(), linebuffer);
    auto inserted_key = watcher_values_.Insert(key, linebuffer);
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

  static DebugConsole& instance() {
    static DebugConsole console;
    return console;
  }

 private:
  inline static constexpr size_t kMaxLines = 1024;

  struct Linebuffer {
    size_t len;
    char chars[kMaxLogLineLength + 1];
  };

  void Log(int category, SDL_LogPriority priority, const char* message);

  static void LogWithConsole(void* userdata, int category,
                             SDL_LogPriority priority, const char* message);

  void CopyToBuffer(std::string_view text, Linebuffer* buffer);
  void LogLine(std::string_view text);

  FixedArena<2 * kMaxLines * sizeof(Linebuffer), BumpAllocator> buffers_;
  FixedCircularBuffer<Linebuffer*, kMaxLines> lines_;
  SDL_LogOutputFunction log_fn_;
  void* log_fn_userdata_;

  inline static constexpr size_t kMaxWatchers = 128;

  FixedArray<std::string_view, kMaxWatchers> watcher_keys_;
  LookupTable<Linebuffer*> watcher_values_;
};

}  // namespace G

#define WATCH_EXPR(str, ...)                                    \
  do {                                                          \
    G::DebugConsole::instance().AddWatcher(str, ##__VA_ARGS__); \
  } while (0);
#define WATCH_VAR(var) WATCH_EXPR(#var, var)

#endif  // _GAME_CONSOLE_H