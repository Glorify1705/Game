#pragma once
#ifndef _GAME_THREAD_POOL_H
#define _GAME_THREAD_POOL_H

#include <condition_variable>
#include <mutex>
#include <thread>

#include "array.h"
#include "circular_buffer.h"

namespace G {

class ThreadPool {
 public:
  ThreadPool(Allocator* allocator, std::size_t num_threads);

  ~ThreadPool();

  // Start all worker threads.
  void Start();

  // Signal all threads to exit.
  void Stop();

  // Wait for all threads to finish.
  void Wait();

  // Enqueue a work item.
  void Queue(int (*fn)(void*), void* userdata);

 private:
  struct Work {
    int (*fn)(void*);
    void* userdata;
  };

  int Loop(size_t index);

  FixedArray<std::thread> threads_;
  std::mutex mu_;
  std::condition_variable cv_;
  size_t num_threads_;
  static constexpr size_t kMaxFunctions = 4096;
  CircularBuffer<Work> work_;
  bool exit_ = false;
};

}  // namespace G

#endif  // _GAME_THREAD_POOL_H
