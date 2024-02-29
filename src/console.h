#pragma once
#ifndef _GAME_CONSOLE_H
#define _GAME_CONSOLE_H

#include <deque>
#include <string>

#include "SDL.h"
#include "allocators.h"
#include "array.h"
#include "circular_buffer.h"
#include "dictionary.h"
#include "strings.h"
#include "thread.h"

namespace G {

class DebugConsole {
 public:
  DebugConsole(Allocator* allocator)
      : allocator_(allocator),
        lines_(allocator),
        watcher_handles_(allocator),
        watcher_values_(allocator) {
    mu_ = SDL_CreateMutex();
    SDL_LogGetOutputFunction(&log_fn_, &log_fn_userdata_);
    SDL_LogSetOutputFunction(LogWithConsole, this);
  }
  ~DebugConsole() {
    SDL_LogSetOutputFunction(log_fn_, log_fn_userdata_);
    SDL_DestroyMutex(mu_);
  }

  template <typename... Ts>
  void Log(Ts... ts) {
    LockMutex l(mu_);
    FixedStringBuffer<kMaxLogLineLength> buf(std::forward<Ts>(ts)...);
    LogLine(buf.piece());
  }

  template <typename Fn>
  void ForAllLines(Fn&& fn) {
    LockMutex l(mu_);
    for (size_t i = 0; i < lines_.size(); ++i) {
      Linebuffer* buffer = lines_[i];
      fn(std::string_view(buffer->chars, buffer->len));
    }
  }

  template <typename... Ts>
  void AddWatcher(std::string_view key, Ts... ts) {
    LockMutex l(mu_);
    FixedStringBuffer<kMaxLogLineLength> buf(std::forward<Ts>(ts)...);
    if (Linebuffer * linebuffer; watcher_values_.Lookup(key, &linebuffer)) {
      CopyToBuffer(buf.piece(), linebuffer);
      return;
    }
    uint32_t handle = StringIntern(key);
    auto* linebuffer = New<Linebuffer>(allocator_);
    CopyToBuffer(buf.piece(), linebuffer);
    watcher_values_.Insert(key, linebuffer);
    watcher_handles_.Push(handle);
  }

  template <typename Fn>
  void ForAllWatchers(Fn&& fn) {
    LockMutex l(mu_);
    for (uint32_t handle : watcher_handles_) {
      std::string_view key = StringByHandle(handle);
      if (Linebuffer * val; watcher_values_.Lookup(key, &val)) {
        fn(key, std::string_view(val->chars, val->len));
      }
    }
  }

  static DebugConsole& Instance() {
    static DebugConsole console(SystemAllocator::Instance());
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

  Allocator* allocator_;
  StaticAllocator<2 * kMaxLines * sizeof(Linebuffer)> buffers_;
  CircularBuffer<Linebuffer*, kMaxLines> lines_;
  SDL_LogOutputFunction log_fn_;
  void* log_fn_userdata_;

  inline static constexpr size_t kMaxWatchers = 128;

  FixedArray<uint32_t, kMaxWatchers> watcher_handles_;
  Dictionary<Linebuffer*> watcher_values_;

  SDL_mutex* mu_ = nullptr;
};

}  // namespace G

#define WATCH_EXPR(str, ...)                                    \
  do {                                                          \
    G::DebugConsole::Instance().AddWatcher(str, ##__VA_ARGS__); \
  } while (0);
#define WATCH_VAR(var) WATCH_EXPR(#var, var)

#endif  // _GAME_CONSOLE_H