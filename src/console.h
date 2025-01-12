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
#include "stringlib.h"
#include "thread.h"

namespace G {

class DebugConsole {
 public:
  DebugConsole(Allocator* allocator)
      : buffers_(allocator, kMaxLines), lines_(kMaxLines, allocator) {
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

  friend DebugConsole& StartDebugConsole();

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

  BlockAllocator<Linebuffer> buffers_;
  CircularBuffer<Linebuffer*> lines_;
  SDL_LogOutputFunction log_fn_;
  void* log_fn_userdata_;

  SDL_mutex* mu_ = nullptr;
};

}  // namespace G

#endif  // _GAME_CONSOLE_H
