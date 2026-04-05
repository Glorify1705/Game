#pragma once
#ifndef _GAME_EXECUTOR_H
#define _GAME_EXECUTOR_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "allocators.h"
#include "array.h"
#include "circular_buffer.h"

namespace G {

// Completion state for a Task.
enum class TaskState : uint8_t {
  kPending,    // Not yet started or still running.
  kSucceeded,  // fn returned true.
  kFailed,     // fn returned false.
};

// A unit of work submitted to an Executor.
// Allocated by the caller (stack, arena, or system allocator).
// Must remain valid until the executor signals completion.
struct Task {
  // Function to execute on a worker thread. Returns true on success, false on
  // failure.
  bool (*fn)(void* userdata);
  // Caller-provided context passed to fn.
  void* userdata;
  // Optional cleanup called after fn completes (regardless of success or
  // failure) on the same thread. May be null.
  void (*cleanup)(void* userdata);
  // Optional deadline. If non-zero, Wait() will log a warning when the
  // deadline is exceeded to help diagnose deadlocks.
  std::chrono::steady_clock::time_point deadline{};
  // Set by the executor when the task finishes.
  std::atomic<TaskState> state{TaskState::kPending};
};

// Abstract interface for running work. Subsystems accept Executor* the same
// way they accept Allocator*, decoupling work submission from threading
// strategy.
class Executor {
 public:
  virtual ~Executor() = default;

  // Submit a single task for asynchronous execution.
  virtual void Submit(Task* task) = 0;

  // Execute fn for index ranges in [0, count), split into batches of at least
  // min_batch. Blocks until all iterations complete.
  virtual void ParallelFor(int count, int min_batch,
                           void (*fn)(int start, int end, void* ctx),
                           void* ctx) = 0;

  // Block until a previously submitted task completes. Logs a warning if
  // the task's deadline is exceeded.
  virtual void Wait(Task* task) = 0;

  // Return true if the task has completed, false otherwise. Non-blocking.
  virtual bool TryWait(Task* task) = 0;
};

// Runs all work synchronously on the calling thread. Zero overhead.
// Useful for testing, debugging, and single-threaded builds.
class InlineExecutor : public Executor {
 public:
  void Submit(Task* task) override;
  void ParallelFor(int count, int min_batch,
                   void (*fn)(int start, int end, void* ctx),
                   void* ctx) override;
  void Wait(Task* task) override;
  bool TryWait(Task* task) override;
};

// Thread pool executor with per-thread queues and work stealing.
// Created once at engine startup. All subsystems share this single pool.
class ThreadPoolExecutor : public Executor {
 public:
  // Creates a pool with the given number of worker threads.
  // Use NumDefaultThreads() for a sensible default.
  ThreadPoolExecutor(Allocator* allocator, size_t num_threads);
  ~ThreadPoolExecutor();

  // Start all worker threads. Must be called before Submit/ParallelFor.
  void Start();

  // Signal all threads to stop and wait for them to finish.
  void Shutdown();

  // Returns max(1, hardware_concurrency - 1).
  static size_t NumDefaultThreads();

  // Executor interface.
  void Submit(Task* task) override;
  void ParallelFor(int count, int min_batch,
                   void (*fn)(int start, int end, void* ctx),
                   void* ctx) override;
  void Wait(Task* task) override;
  bool TryWait(Task* task) override;

  // Number of worker threads.
  size_t num_threads() const { return num_threads_; }

 private:
  void WorkerLoop(size_t index);
  bool TryRunOne(size_t index);
  bool HasPendingWork();

  // Per-thread work queues.
  struct WorkerQueue {
    CircularBuffer<Task*> queue;
    std::mutex mu;
  };

  size_t num_threads_;
  FixedArray<std::thread> threads_;
  FixedArray<WorkerQueue*> queues_;
  Allocator* allocator_;

  // Global notification for new work.
  std::mutex wake_mu_;
  std::condition_variable wake_cv_;
  bool exit_ = false;

  // Round-robin counter for Submit distribution.
  std::atomic<size_t> next_queue_{0};
};

}  // namespace G

#endif  // _GAME_EXECUTOR_H
