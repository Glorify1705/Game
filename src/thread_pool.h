#pragma once
#ifndef _GAME_THREAD_POOL_H
#define _GAME_THREAD_POOL_H

#include "SDL_thread.h"
#include "array.h"
#include "circular_buffer.h"

namespace G {

class ThreadPool {
 public:
  ThreadPool(Allocator* allocator, std::size_t num_threads);

  ~ThreadPool();

  void Start();

  void Wait();

  void Queue(int (*fn)(void*), void* userdata);

  int Loop(size_t index);

 private:
  struct UserData {
    ThreadPool* self;
    std::size_t index;
  };
  struct Work {
    int (*fn)(void*);
    void* userdata;
  };

  static int LoopFn(void* data) {
    auto* in = static_cast<UserData*>(data);
    return in->self->Loop(in->index);
  }

  Allocator* allocator_ = nullptr;
  DynArray<SDL_Thread*> threads_;
  DynArray<UserData> user_data_;
  SDL_mutex* mu_ = nullptr;
  SDL_cond* cv_ = nullptr;
  SDL_cond* idle_cv_ = nullptr;
  int inflight_ = 0;
  static constexpr std::size_t kMaxFunctions = 4096;
  CircularBuffer<Work> work_;
  bool exit_ = false;
};

}  // namespace G

#endif  // _GAME_THREAD_POOL_H
