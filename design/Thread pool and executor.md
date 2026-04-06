---
status: implemented
tags: [threading, core]
---

# Thread Pool and Executor

## Glossary

- **Thread pool**: A fixed set of OS threads created once at startup. Work items
  are submitted to a queue; idle threads pick them up. Avoids the cost of
  creating and destroying threads per task.
- **Executor**: An abstract interface that accepts work. Concrete
  implementations include a thread pool (runs work in parallel), a
  single-thread executor (runs work sequentially on one thread), or an inline
  executor (runs work immediately on the caller's thread). Code that submits
  work programs against the executor interface, not a specific implementation.
- **Work stealing**: When a thread's own queue is empty, it takes ("steals")
  work from another thread's queue. Keeps all threads busy without a central
  queue bottleneck.
- **Parallel-for**: A pattern where a single logical loop (`for i in 0..N`) is
  split across multiple threads. Each thread processes a contiguous range
  `[start, end)`. Sometimes called a "group task" (Godot) or "task set"
  (enkiTS).
- **Batch size / min range**: The minimum number of iterations assigned to one
  thread in a parallel-for. Too small = overhead dominates; too large =
  poor load balance. Typical values: 16–256 depending on per-item cost.
- **Pinned task**: A task that must run on a specific thread (e.g. the main
  thread, or a dedicated I/O thread). Useful when the work touches
  thread-unsafe resources like OpenGL or the Lua VM.
- **Task dependency**: A relationship where task B cannot start until task A
  completes. Expressed as a handle or ID. Enables building DAGs (directed
  acyclic graphs) of work.
- **Fire-and-forget**: Submitting a task without waiting for its result. The
  caller never blocks. Useful for logging, telemetry, or cleanup work.

## Problem

The engine creates threads in two uncoordinated places:

**1. `EngineModules` thread pool** (`game.cc:191`)

A `ThreadPool` with 4 hardcoded threads is created at engine startup. Only one
of the four threads ever receives work: a single long-running file-watcher loop
(`CheckChangedFiles`). The other 3 threads sit idle for the entire lifetime of
the engine, each consuming an OS thread and its associated kernel resources.

**2. `DbPacker` bare threads** (`packer.cc:805–811`)

The asset packer creates raw `std::thread` objects every time
`ProcessDeferredItems()` is called. Thread count is based on CPU core count.
These threads are joined and destroyed before the function returns, then
recreated the next time packaging runs (which happens on every hot-reload).
This means during hot-reload, the engine briefly has 4 pool threads + N packer
threads + the SDL audio callback thread + the main thread all running
simultaneously.

### Concrete issues

| Issue | Where | Impact |
|---|---|---|
| 3 of 4 pool threads are permanently idle | `game.cc:191` | Wasted OS resources |
| Thread count (4) is hardcoded, not tuned to hardware | `game.cc:191` | Underutilizes many-core machines, oversubscribes 2-core machines |
| Packer creates/destroys threads on every hot-reload | `packer.cc:805` | Thread creation overhead, potential oversubscription |
| Packer threads are invisible to the pool | `packer.cc:805` | No global accounting of thread count |
| No executor abstraction | everywhere | Systems are coupled to a specific threading strategy; hard to test, hard to change |
| `ThreadPool::Queue` only accepts `int (*fn)(void*)` | `thread_pool.h:30` | Requires static functions + void pointer casting; no type safety, no result handling |
| No parallel-for primitive | — | Packer reimplements work distribution with atomics each time |
| No way to run a task on the main thread from a worker | — | Would be useful for hot-reload asset loading (OpenGL, Lua are main-thread-only) |

## Engine survey

### Love2D

Love2D exposes raw threads to Lua scripts. Each thread gets its own Lua VM.
Communication happens through `Channel` objects (typed message queues with
`push`/`pop`/`demand` methods). There is no thread pool, no task scheduler, and
no parallel-for. Threads are manually created and managed.

The key lesson from Love2D is that **Lua states are not thread-safe**. Any
multi-threading design must keep Lua on a single thread or use separate VMs
with message passing.

Love2D's model is simple but offers no scheduling, no work balancing, and no
way to avoid oversubscription.

### Godot 4

Godot provides a global singleton `WorkerThreadPool`. Thread count defaults to
the number of CPU cores. The pool has a dual-queue system: high-priority and
low-priority, with low-priority tasks throttled to a configurable subset of
threads to prevent starvation.

Two task types:
- **Regular tasks**: execute once on one worker thread.
- **Group tasks**: a callable invoked N times across workers (parallel-for).
  Workers claim indices via atomic `fetch_add`, similar to our packer pattern.

Tasks return an ID. Every task **must** be waited on eventually — the wait call
doubles as resource cleanup. This coupling of scheduling and memory management
is simple but means you cannot fire-and-forget.

Key design points:
- Pre-allocated task storage avoids runtime allocation.
- Binary priority (high/low) — simple but limiting.
- No pluggable executor; the singleton IS the scheduler.

### Unity Job System

Unity maintains a single native thread pool (C++) shared between engine
internals and user C# code. Worker count equals CPU core count, not
configurable.

Jobs are value-type structs implementing interfaces:
- `IJob`: single execution on one worker.
- `IJobParallelFor`: `Execute(int index)` called N times across workers, with a
  configurable batch size controlling how many iterations each thread gets.

Data is **copied** into the job struct (memcpy), eliminating shared-state races
by construction. A safety system uses `[ReadOnly]`/`[WriteOnly]` attributes to
detect data races at schedule time.

`Schedule()` returns a `JobHandle`. Passing one handle to another job's
`Schedule()` creates a dependency chain. `JobHandle.CombineDependencies()`
merges multiple handles for fan-in.

Allocator tiers for job data:
- `Allocator.Temp` — frame-scoped (analogous to our `frame_allocator`).
- `Allocator.TempJob` — must be freed within 4 frames.
- `Allocator.Persistent` — manual lifetime (analogous to our system allocator).

The batch-size parallel-for and the allocator-tier concept map well to our
engine.

### Unreal Engine 5

UE5 has two overlapping systems:
- **TaskGraph** (legacy): verbose, class-based tasks with explicit `DoTask()`.
- **UE::Tasks** (modern): lightweight lambda-based API.

Both share a pool of unnamed worker threads (core-count-based) plus **named
threads** for engine-critical work: `GameThread`, `RenderingThread`,
`RHIThread`, `AudioThread`. Named threads are a useful concept — they are
dedicated threads for subsystems that need sequential, low-latency processing.

Modern API:
- `Launch(location, callable)` — fire-and-forget.
- `Launch(location, callable, Prerequisites(handle))` — with dependencies.
- `FPipe` — a sequential chain where tasks are guaranteed to run one after
  another (useful for ordered async work like load → decode → upload).

UE5 is massively over-engineered for a small engine, but the named-thread and
pipe patterns are worth noting.

### enkiTS (Doug Binks)

A lightweight C/C++ task scheduler designed for game engines. Used in
production in the Avoyd voxel engine.

- Creates `N-1` worker threads (main thread participates as a worker).
- Per-thread wait-free queues with work stealing.
- `ITaskSet` with `ExecuteRange(start, end, threadnum)` for parallel-for.
  `m_MinRange` controls batch size.
- `IPinnedTask` for tasks that must run on a specific thread.
- Up to 5 configurable priority levels.
- **First-class custom allocator support** via config struct:
  ```cpp
  config.customAllocator.alloc = MyAllocFunc;
  config.customAllocator.free = MyFreeFunc;
  config.customAllocator.userData = &myAllocatorContext;
  ```
- **Zero heap allocations** during scheduling after initialization.
- Full C API (`TaskScheduler_c.h`) for use from non-C++ code.
- Task objects must outlive their execution (stack or arena allocated).

enkiTS is the closest match to our engine's constraints: explicit allocators, no
STL, game-engine-oriented, small codebase (~3k lines).

### BS::thread_pool

Popular header-only C++ thread pool. Clean API (`submit_task`, `detach_task`,
`submit_loop`, `wait`), optional priority support (-128 to +127). However, it
relies entirely on STL (`std::function`, `std::future`, `std::deque`). No
custom allocator support. **Not compatible** with our no-STL, explicit-allocator
constraints.

### Sean Parent's task system

A minimal (~80 line) reference implementation from the "Better Code:
Concurrency" talk. Per-thread queues with round-robin distribution and
try-lock-based work stealing. The pattern is elegant: instead of blocking on a
mutex, both push and pop attempt a non-blocking `try_lock`; if contended, they
move to the next queue, eliminating convoy effects.

Not directly usable (all STL), but the architectural pattern — per-thread
queues, round-robin submission, try-lock stealing — is the right skeleton to
implement on top of our own data structures.

## Summary table

| Feature | Love2D | Godot 4 | Unity | UE5 | enkiTS | BS::pool | Sean Parent |
|---|---|---|---|---|---|---|---|
| Architecture | Raw threads | Global singleton | Global pool | Named + worker | Per-thread queues | Single pool | Per-thread queues |
| Parallel-for | No | Group tasks | IJobParallelFor | ParallelFor | ITaskSet | submit_loop | No |
| Priorities | No | Binary (hi/lo) | No | Enum | Up to 5 | -128..+127 | No |
| Dependencies | No | Wait-for-ID | JobHandle chain | Prerequisites | Yes | No | No |
| Work stealing | No | Atomic index | Yes | Yes | Yes (per-thread) | No (shared) | Yes (try-lock) |
| Custom allocators | N/A | Internal only | NativeContainer | No | **Yes** | No | No |
| Zero-alloc scheduling | N/A | No | No | No | **Yes** | No | No |
| Pinned tasks | N/A | No | No | Named threads | **Yes** | No | No |
| Fire-and-forget | N/A | No (must wait) | No (must wait) | Yes | No (must wait) | Yes | Yes |
| LOC | — | ~2000 | — | Massive | ~3000 | 536 | ~80 |

## Design

### Executor interface

The central abstraction is an `Executor` — a thing that can run work. All
subsystems that need concurrency accept an `Executor*` parameter, the same way
they accept an `Allocator*`. This decouples the work-submission site from the
threading strategy.

#### Why `ParallelFor` instead of submit + barrier?

A barrier (or "wait for all pending tasks") is a coarser tool: you submit N
tasks individually, then block until every task in the pool is done. This has
two problems:

1. **No isolation.** If another subsystem also has tasks in flight, the barrier
   waits for those too — or worse, you need separate barrier groups, which adds
   complexity equivalent to `ParallelFor` anyway.
2. **Boilerplate.** Every call site must manually split a range into batches,
   submit each batch as a separate task, and then wait. `ParallelFor` is
   exactly this pattern factored into one call.

`ParallelFor` is the right primitive because our main use case (the packer) is
literally a parallel loop over work items. `Submit` + `Wait` covers the
long-running task case (file watcher). Together they handle everything we need
without a separate barrier concept.

```cpp
// A unit of work. Allocated by the caller (stack, arena, or system allocator).
// Must remain valid until the executor signals completion.
struct Task {
  void (*fn)(void* userdata);   // Function to execute.
  void* userdata;               // Caller-provided context.
  void (*cleanup)(void* userdata);  // Optional cleanup (may be null).
                                    // Called after fn completes, on the
                                    // same thread that ran fn.
};

// Abstract executor interface.
class Executor {
 public:
  virtual ~Executor() = default;

  // Submit a single task for execution.
  virtual void Submit(Task* task) = 0;

  // Execute fn(i) for i in [0, count), split into batches of at least
  // min_batch. Blocks until all iterations complete.
  virtual void ParallelFor(int count, int min_batch,
                           void (*fn)(int start, int end, void* ctx),
                           void* ctx) = 0;

  // Wait for a specific submitted task to complete.
  virtual void Wait(Task* task) = 0;
};
```

The `Task` struct uses a function pointer + `void*` userdata pattern (no
`std::function`, no allocation, no inheritance). The optional `cleanup` pointer
lets callers release resources when the task finishes, without requiring the
submitter to call `Wait` just to clean up:

```cpp
struct MyWorkData {
  int input;
  int result;
};

MyWorkData data = {.input = 42};
Task task;
task.fn = [](void* ud) {
  auto* d = static_cast<MyWorkData*>(ud);
  d->result = ExpensiveComputation(d->input);
};
task.userdata = &data;
task.cleanup = nullptr;  // No cleanup needed; data is stack-allocated.
executor->Submit(&task);
executor->Wait(&task);
// data.result is now available.
```

For fire-and-forget tasks that own heap resources:

```cpp
auto* ctx = allocator->New<ExpensiveContext>(...);
Task task;
task.fn = [](void* ud) {
  auto* c = static_cast<ExpensiveContext*>(ud);
  c->DoWork();
};
task.userdata = ctx;
task.cleanup = [](void* ud) {
  // Called on the worker thread after fn returns.
  auto* c = static_cast<ExpensiveContext*>(ud);
  c->allocator->Delete(c);
};
executor->Submit(&task);
// No Wait needed — cleanup frees the context.
```

### Concrete executors

**`ThreadPoolExecutor`** — the primary production executor.

- Created once at engine startup with `Allocator*` and thread count (default:
  `hardware_concurrency() - 1`, minimum 1).
- Per-thread work queues (using `CircularBuffer`) with work stealing.
- Workers try their own queue first, then steal from others (Sean Parent
  pattern with try-lock).
- `ParallelFor` splits the range into batches, submits them, and spins on a
  completion counter (no extra allocation beyond stack-local batch descriptors).
- The executor tracks completion state internally (per-task atomic flag in a
  pool-managed array). The `Task` struct itself has no atomic — it is a plain
  data struct owned by the caller.

**`InlineExecutor`** — runs work immediately on the calling thread.

- `Submit(task)` calls `task->fn(task)` synchronously.
- `ParallelFor` runs the loop sequentially.
- Zero overhead. Useful for debugging, testing, and single-threaded builds.
- Makes it trivial to write unit tests for concurrent code without actually
  spawning threads.

**`MainThreadExecutor`** — queues work to be drained on the main thread.

- `Submit(task)` pushes to a queue.
- `DrainPending()` called once per frame from the main loop, executes all
  queued tasks.
- Used for work that must touch main-thread-only resources (OpenGL, Lua, SDL
  window).
- Workers can submit follow-up tasks here (e.g., "decode image on worker,
  upload texture on main thread").

### Task lifecycle

```
caller fills Task { fn, userdata, cleanup }
  │
  ├─ Submit(task)  ──►  executor places in queue
  │                       │
  │                       ▼
  │                     worker dequeues, calls task->fn(task->userdata)
  │                       │
  │                       ▼
  │                     if task->cleanup: task->cleanup(task->userdata)
  │                       │
  │                       ▼
  │                     executor marks task complete (internal flag)
  │
  ├─ Wait(task)    ──►  spins / yields until complete
  │
  └─ caller reads results from userdata, frees Task if needed
```

For `ParallelFor`, the lifecycle is simpler — it blocks until all iterations
are done, so the caller never sees individual task handles.

### Thread count policy

The current hardcoded `4` is replaced with a policy:

```
pool_threads = max(1, hardware_concurrency() - 1)
```

The `-1` accounts for the main thread, which is always doing work (game loop,
rendering, Lua). On a 4-core machine this gives 3 workers; on a 2-core machine,
1 worker; on a 1-core machine, 1 worker (never zero).

This can be overridden via a config option for debugging:
`--threads=N`.

### Migration plan

#### Phase 1: Executor interface + ThreadPoolExecutor

1. Define `Executor`, `Task`, `ThreadPoolExecutor`, `InlineExecutor` in new
   files `executor.h` / `executor.cc`.
2. `ThreadPoolExecutor` replaces the current `ThreadPool` class. Same
   allocator-aware construction, same `Start()`/`Stop()` lifecycle, but with
   the `Executor` interface.
3. `EngineModules` creates one `ThreadPoolExecutor` at startup with
   `hardware_concurrency() - 1` threads. This is the **single global pool**.
4. The file watcher (`CheckChangedFiles`) becomes a `Task` submitted to the
   pool — same behavior, but now it is one of potentially many tasks rather than
   the sole consumer of a 4-thread pool.

#### Phase 2: Migrate the packer

1. `DbPacker` accepts an `Executor*` parameter instead of creating its own
   threads.
2. `ProcessDeferredItems()` calls `executor->ParallelFor(deferred_.size(),
   /*min_batch=*/1, ProcessWorkItems, &ctx)` instead of manually spawning and
   joining threads.
3. The per-thread scratch buffer allocation moves to the `ParallelFor` callback
   using the thread index parameter.
4. During hot-reload, the packer reuses the global pool threads instead of
   creating new ones. **Note**: the file watcher task will be occupying one
   worker, so `N-1` workers are available for encoding. This is fine; the file
   watcher is usually sleeping.

#### Phase 3: MainThreadExecutor

1. Add `MainThreadExecutor` for work that must run on the main thread.
2. Wire `DrainPending()` into the main loop (after event processing, before
   `Update()`).
3. Use case: hot-reload currently calls `WriteAssetsToDb` on the background
   thread, then signals the main thread via an atomic flag, and the main thread
   reloads Lua/textures. With `MainThreadExecutor`, the background worker can
   directly submit "reload these assets" tasks to the main thread queue, making
   the handoff explicit.

#### Phase 4: Pass executor to subsystems

1. Subsystems that may benefit from concurrency in the future (particle system,
   collision broad-phase, renderer sort) accept an `Executor*` parameter.
2. During development and testing, pass `InlineExecutor` to keep behavior
   single-threaded and deterministic.
3. Switch to `ThreadPoolExecutor` when the concurrent implementation is ready
   and tested.

### What changes for each file

| File | Current | After |
|---|---|---|
| `thread_pool.h/cc` | `ThreadPool` class with raw queue | Replaced by `ThreadPoolExecutor` implementing `Executor` |
| `thread.h` | `LockMutex`, `SleepMs` | Unchanged (still useful utilities) |
| `game.cc` | Creates `ThreadPool(allocator, 4)`, queues file watcher | Creates `ThreadPoolExecutor(allocator, hardware_concurrency()-1)`, submits file watcher as `Task` |
| `packer.cc` | Creates bare `std::thread` array, atomic work distribution | Accepts `Executor*`, calls `ParallelFor` |
| New: `executor.h` | — | `Executor` interface, `Task` struct, `InlineExecutor` |
| New: `executor.cc` | — | `ThreadPoolExecutor`, `MainThreadExecutor` implementations |

### Memory model

- `ThreadPoolExecutor` allocates its thread array and per-thread queues once at
  startup using the provided `Allocator*`. No further allocations during
  scheduling.
- `Task` structs are caller-allocated. The executor never allocates tasks. This
  matches the engine's explicit-allocator philosophy: whoever creates the work
  decides where it lives in memory.
- `ParallelFor` uses stack-local batch descriptors (no heap allocation).
- Per-thread scratch arenas (like the packer's `kScratchArenaSize` buffers) are
  the caller's responsibility. The `ParallelFor` callback receives a thread
  index to select the right scratch buffer.

### Synchronization

- Per-thread queues use a mutex with try-lock for work stealing (Sean Parent
  pattern). No contention on the common path (own queue).
- Task completion is tracked by the executor internally (e.g., an atomic flag
  per slot in a pre-allocated array). `Wait()` spins with
  `std::this_thread::yield()`. For short tasks this is cheaper than a
  condition variable. For long tasks, the caller should do other work between
  `Submit` and `Wait`.
- `ParallelFor` uses an `std::atomic<int>` completion counter. When it reaches
  the expected count, the caller unblocks.
- `MainThreadExecutor` queue is protected by a single mutex (only contention is
  worker→main, which is infrequent).

### Testing

- `InlineExecutor` makes all concurrent code testable in single-threaded
  GoogleTest without flakiness.
- `ThreadPoolExecutor` can be tested with `ParallelFor` on known data (e.g.,
  sum an array in parallel, verify result).
- Thread sanitizer (TSan) should be added as a build option alongside ASan/UBSan
  to catch data races in CI.

### Decisions

1. **No nested parallelism.** A `ParallelFor` callback must not call
   `ParallelFor`. This avoids deadlocks where all workers are blocked waiting
   for nested work that no free worker can pick up. TODO: revisit in Phase 2
   if a real use case appears, but it is likely not worth the complexity.

2. **No task dependencies / DAGs.** `MainThreadExecutor` + explicit `Wait()`
   covers the immediate needs. Not worth the complexity for Phase 1.

3. **File watcher stays as a long-running task.** The `while(!stopped)` loop
   with internal sleeps is simple and works. It occupies one worker thread, but
   the worker is sleeping most of the time.

4. **No priority levels for Phase 1.** Single FIFO queue per worker. All tasks
   are equal. Add priorities later if needed.

5. **Thread naming in Phase 1.** Each worker thread is named `"pool-0"`,
   `"pool-1"`, etc. for debugger and profiler visibility. SDL3 only supports
   naming threads created with `SDL_CreateThread`, not `std::thread`, so we use
   platform APIs directly: `pthread_setname_np` on Linux/macOS,
   `SetThreadDescription` on Windows. This is a one-liner per platform at the
   top of the worker loop.

## References

- [enkiTS — Internals of a lightweight task scheduler](https://www.enkisoftware.com/devlogpost-20150905-1-Internals-of-a-lightweight-task-scheduler)
- [enkiTS GitHub](https://github.com/dougbinks/enkiTS)
- [Sean Parent — Better Code: Concurrency](https://sean-parent.stlab.cc/presentations/2016-08-08-concurrency/2016-08-08-concurrency.pdf)
- [Godot WorkerThreadPool](https://docs.godotengine.org/en/stable/classes/class_workerthreadpool.html)
- [Unity Job System](https://docs.unity3d.com/6000.3/Documentation/Manual/job-system-overview.html)
- [UE5 Tasks Systems](https://dev.epicgames.com/documentation/en-us/unreal-engine/tasks-systems-in-unreal-engine)
- [BS::thread_pool](https://github.com/bshoshany/thread-pool)
- [Google Marl](https://github.com/google/marl)
