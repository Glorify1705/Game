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

  void Stop();

  void Wait();

  void Queue(int (*fn)(void*), void* userdata);

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

  int Loop(size_t index);

  FixedArray<SDL_Thread*> threads_;
  FixedArray<UserData> user_data_;
  SDL_mutex* mu_ = nullptr;
  SDL_cond* cv_ = nullptr;
  size_t num_threads_;
  static constexpr size_t kMaxFunctions = 4096;
  CircularBuffer<Work> work_;
  bool exit_ = false;
};

}  // namespace G

#endif  // _GAME_THREAD_POOL_H
