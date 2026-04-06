---
status: partially-implemented
tags: [profiling, performance]
---

# Profiling and Tracing

## Problem

We see occasional slow frames but have no way to diagnose them. The current profiling story is:

| Tool                           | What it tells us                                 | What it doesn't                                                 |
| ------------------------------ | ------------------------------------------------ | --------------------------------------------------------------- |
| `TIMER` macro (clock.h)        | Elapsed time for a scope, printed to log         | No timeline, no per-frame breakdown, no aggregation             |
| `Stats` class (stats.h)        | Min/max/avg/stdev/percentiles of frame time      | Which subsystem caused a spike, what happened inside that frame |
| Tab overlay (game.cc:584)      | FPS and Stats in debug mode                      | Only total frame time — not update vs render vs physics vs Lua  |
| gperftools/pprof (in devenv)   | Aggregate CPU hotspots across entire run         | Per-frame breakdown, frame-to-frame variation, GPU time         |
| Valgrind/Callgrind (in devenv) | Instruction counts, cache misses                 | Real-time use (15-60x slowdown), per-frame breakdown, GPU       |
| RenderDoc (in devenv)          | GPU state inspection for a single captured frame | Continuous monitoring, CPU-side breakdown                       |

None of these answer the question: **"Frame 4723 took 32ms — was it physics, Lua update, rendering, or the GL driver?"**

### What we need

1. **Per-frame timeline** — see every subsystem's contribution to each frame, as a flame graph.
2. **Spike identification** — find slow frames in a recording and drill into them.
3. **Low overhead** — usable during normal gameplay, not just synthetic benchmarks.
4. **Zero cost when disabled** — no runtime penalty in release builds.
5. **Minimal dependencies** — consistent with the engine's philosophy of simplicity and owning our code.

## How other engines handle this

### Love2D

No built-in profiler. Provides `love.graphics.getStats()` (draw calls, texture memory, canvas switches) and relies on community Lua libraries:

| Library | Approach |
|---------|----------|
| profile.lua | Statistical Lua function profiler — reset every ~100 frames for real-time view |
| AppleCake | Writes Chrome Tracing JSON, visualize with `chrome://tracing` or Perfetto UI |
| PROBE.lua | Real-time in-game overlay |

AppleCake's Chrome Tracing approach is the most relevant: trivial to implement, and the Perfetto viewer provides flame graphs, search, statistics, and cross-thread correlation for free.

### high_impact

No profiling whatsoever. The engine is ~4800 LOC of minimalist C. Any profiling uses platform tools (perf, Instruments).

### Anchor

Provides built-in FPS and draw call counters in the debug overlay. No frame-level profiler. Users profile with platform tools.

### Takeaway

Small 2D engines either have no profiling or have simple stat counters. None integrate a proper frame profiler. The Chrome Tracing approach (used by Love2D's AppleCake) hits the best effort-to-value ratio.

## Available tools already in devenv

### gperftools + pprof

Statistical sampling profiler. Sets a timer signal (default 100 Hz) that captures call stacks on each tick.

```bash
CPUPROFILE=profile.prof ./build/game run assets   # Profile entire run
pprof --http=:8080 ./build/game profile.prof       # Web UI with flame graph
```

Or programmatic control:
```cpp
#include <gperftools/profiler.h>
ProfilerStart("profile.prof");
// ... code to profile ...
ProfilerStop();
```

**Good for:** Finding aggregate CPU hotspots across thousands of frames — "physics takes 40% of total CPU time." **Bad for:** Per-frame analysis — at 100 Hz you get ~1.7 samples per 16.67ms frame, far too few for a breakdown. No timeline view, no frame boundaries, no GPU correlation.

### Valgrind / Callgrind

Instruction-level profiler via CPU emulation. Deterministic and reproducible but **15-60x slowdown** makes real-time gameplay impossible.

```bash
valgrind --tool=callgrind --instr-atstart=no ./build/game run assets
callgrind_control -i on   # Toggle instrumentation from another terminal
```

**Good for:** Cache analysis, algorithmic inefficiency, deterministic before/after comparison. **Bad for:** Anything that needs real-time frame rates.

### RenderDoc

GPU debugger. Captures a single frame and lets you inspect every draw call, texture, shader, and pipeline state.

**Good for:** GPU debugging (wrong texture, shader bug, state leak). **Bad for:** Continuous profiling or CPU-side analysis.

### Summary

| Tool | Real-time? | Per-frame? | GPU? | Memory? | Overhead |
|------|-----------|-----------|------|---------|----------|
| gperftools/pprof | No (file) | No | No | Heap only | ~1-5% |
| Callgrind | No (offline) | No | No | No | 15-60x |
| RenderDoc | Capture-based | Single frame | Yes (deep) | No | Per-capture |

None of these fill the "continuous per-frame timeline" gap.

## Design: Chrome Tracing JSON

### Why this approach

The [Chrome Trace Event format](https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU) is a simple JSON spec that [Perfetto UI](https://ui.perfetto.dev) and `chrome://tracing` can visualize as flame graphs. It's the right first step because:

- **~150 lines of C** to implement. No vendoring, no dependencies. Consistent with the engine's philosophy of owning our code (like QOI/QOA instead of vendoring libpng/libvorbis).
- **Perfetto UI is an excellent viewer** — flame graphs, zoom, search, statistics, cross-thread correlation, counter tracks. All free, runs in the browser.
- **Solves the actual problem** — "frame 4723 was slow, was it physics or Lua?" — which is what we need right now.
- **GPU timing can be added** by writing GL timer query results as trace events.
- **Memory/arena metrics can be added** as counter events in the same format.
- **Zero runtime cost when not recording** — the macros just check a boolean.
- **Trace files are portable artifacts** — can be shared, attached to bug reports, compared across commits, collected in CI.

### The trace event format

A trace file is a JSON array of event objects:

```json
{"traceEvents":[
  {"pid":1,"tid":1,"ph":"X","name":"Update","ts":87705,"dur":15200},
  {"pid":1,"tid":1,"ph":"X","name":"Physics::Update","ts":87705,"dur":8100},
  {"pid":1,"tid":1,"ph":"X","name":"Lua::Update","ts":95805,"dur":7100},
  {"pid":1,"tid":1,"ph":"X","name":"Render","ts":102905,"dur":1465},
  {"pid":1,"tid":1,"ph":"C","name":"Lua Memory","ts":104370,"args":{"KB":2048}},
  {"pid":1,"tid":1,"ph":"i","name":"FrameBoundary","ts":104370,"s":"g"}
]}
```

| Field | Meaning |
|-------|---------|
| `pid` | Process ID (always 1 for us) |
| `tid` | Thread ID |
| `ph` | Phase: `X` = complete event (has `dur`), `B`/`E` = begin/end pair, `C` = counter, `i` = instant |
| `name` | Zone name shown in the viewer |
| `ts` | Timestamp in **microseconds** from an arbitrary epoch |
| `dur` | Duration in microseconds (for `ph:"X"` events) |
| `args` | Arbitrary key-value metadata |

Complete events (`ph:"X"`) are the most efficient — one JSON object per zone instead of two. The viewer renders them identically to B/E pairs.

Counter events (`ph:"C"`) appear as line graphs below the timeline — perfect for Lua memory, arena usage, entity count, draw calls.

### Implementation: `src/profiler.h`

The profiler is a thin instrumentation layer. When recording is enabled, it appends events to a pre-allocated ring buffer. When recording stops (or on a hotkey), it flushes the buffer to a JSON file.

```cpp
#pragma once
#ifndef _GAME_PROFILER_H
#define _GAME_PROFILER_H

#include "clock.h"

namespace G {

// Maximum number of trace events buffered before oldest are overwritten.
// 1M events at ~64 bytes each = ~64MB. At 1000 events/frame and 60 FPS,
// this holds ~16 seconds of trace data.
inline constexpr size_t kMaxTraceEvents = 1 << 20;

struct TraceEvent {
  const char* name;       // Static string (zone name). Not owned.
  const char* category;   // Static string (optional grouping). Not owned.
  double ts;              // Timestamp in seconds (converted to us on flush).
  double dur;             // Duration in seconds (0 for non-complete events).
  uint32_t tid;           // Thread ID.
  uint8_t phase;          // 'X' = complete, 'B' = begin, 'E' = end,
                          // 'C' = counter, 'i' = instant.
  double counter_value;   // For phase == 'C'.
};

class Profiler {
 public:
  // Call once at startup. Allocates the event ring buffer.
  void Init(Allocator* allocator);

  // Start/stop recording. While not recording, all macros are no-ops
  // (one branch on a boolean).
  void StartRecording();
  void StopRecording();
  bool IsRecording() const { return recording_; }

  // Toggle recording on/off (for hotkey binding).
  void ToggleRecording();

  // Write buffered events to a JSON file. Called automatically by
  // StopRecording, or can be called manually.
  void FlushToFile(const char* path);

  // --- Internal API called by macros ---

  void PushEvent(const char* name, const char* category, double ts,
                 double dur, uint8_t phase);
  void PushCounter(const char* name, double ts, double value);

 private:
  TraceEvent* events_ = nullptr;
  size_t head_ = 0;        // Next write position (wraps around).
  size_t count_ = 0;       // Total events written (min of this and capacity).
  size_t capacity_ = 0;
  bool recording_ = false;
};

// Global profiler instance.
Profiler* GetProfiler();

// RAII zone guard. Records a complete event on destruction.
class ProfileZone {
 public:
  ProfileZone(const char* name, const char* category = "")
      : name_(name), category_(category), start_(NowInSeconds()) {}

  ~ProfileZone() {
    auto* p = GetProfiler();
    if (p->IsRecording()) {
      p->PushEvent(name_, category_, start_,
                   NowInSeconds() - start_, 'X');
    }
  }

 private:
  const char* name_;
  const char* category_;
  double start_;
};

}  // namespace G

// --- Public macros ---

#ifdef GAME_WITH_PROFILING

// CPU zone — times the enclosing scope. Name must be a string literal.
#define PROFILE_SCOPE_N(name) \
  ::G::ProfileZone INTERNAL_ID(_profile_zone_)(name)

// CPU zone named after the enclosing function.
#define PROFILE_SCOPE \
  ::G::ProfileZone INTERNAL_ID(_profile_zone_)(__FUNCTION__)

// Frame boundary marker (instant event).
#define PROFILE_FRAME \
  do { \
    auto* _p_ = ::G::GetProfiler(); \
    if (_p_->IsRecording()) \
      _p_->PushEvent("Frame", "", ::G::NowInSeconds(), 0, 'i'); \
  } while (0)

// Counter — track a numeric value over time.
#define PROFILE_COUNTER(name, value) \
  do { \
    auto* _p_ = ::G::GetProfiler(); \
    if (_p_->IsRecording()) \
      _p_->PushCounter(name, ::G::NowInSeconds(), \
                       static_cast<double>(value)); \
  } while (0)

#else

#define PROFILE_SCOPE_N(name)
#define PROFILE_SCOPE
#define PROFILE_FRAME
#define PROFILE_COUNTER(name, value)

#endif  // GAME_WITH_PROFILING

#endif  // _GAME_PROFILER_H
```

### Implementation: `src/profiler.cc`

```cpp
#include "profiler.h"

#include <cstdio>

#include "allocators.h"
#include "logging.h"
#include "thread.h"

namespace G {

static Profiler g_profiler;

Profiler* GetProfiler() { return &g_profiler; }

void Profiler::Init(Allocator* allocator) {
  events_ = allocator->NewArray<TraceEvent>(kMaxTraceEvents);
  capacity_ = kMaxTraceEvents;
  head_ = 0;
  count_ = 0;
}

void Profiler::StartRecording() {
  head_ = 0;
  count_ = 0;
  recording_ = true;
  LOG("Profiler: recording started");
}

void Profiler::StopRecording() {
  recording_ = false;
  LOG("Profiler: recording stopped (", count_, " events)");
}

void Profiler::ToggleRecording() {
  if (recording_) {
    StopRecording();
    FlushToFile("trace.json");
  } else {
    StartRecording();
  }
}

void Profiler::PushEvent(const char* name, const char* category,
                         double ts, double dur, uint8_t phase) {
  if (!recording_) return;
  auto& e = events_[head_ % capacity_];
  e.name = name;
  e.category = category;
  e.ts = ts;
  e.dur = dur;
  e.tid = CurrentThreadId();
  e.phase = phase;
  e.counter_value = 0;
  ++head_;
  if (count_ < capacity_) ++count_;
}

void Profiler::PushCounter(const char* name, double ts, double value) {
  if (!recording_) return;
  auto& e = events_[head_ % capacity_];
  e.name = name;
  e.category = "";
  e.ts = ts;
  e.dur = 0;
  e.tid = CurrentThreadId();
  e.phase = 'C';
  e.counter_value = value;
  ++head_;
  if (count_ < capacity_) ++count_;
}

void Profiler::FlushToFile(const char* path) {
  FILE* f = fopen(path, "w");
  if (!f) {
    LOG("Profiler: failed to open ", path);
    return;
  }
  fputs("{\"traceEvents\":[\n", f);

  // Find the epoch (earliest timestamp) for relative microsecond conversion.
  const size_t start = (count_ < capacity_) ? 0 : head_;
  double epoch = 1e18;
  for (size_t i = 0; i < count_; ++i) {
    const auto& e = events_[(start + i) % capacity_];
    if (e.ts < epoch) epoch = e.ts;
  }

  for (size_t i = 0; i < count_; ++i) {
    const auto& e = events_[(start + i) % capacity_];
    const double ts_us = (e.ts - epoch) * 1e6;
    if (i > 0) fputs(",\n", f);
    if (e.phase == 'C') {
      fprintf(f,
              "{\"pid\":1,\"tid\":%u,\"ph\":\"C\",\"name\":\"%s\","
              "\"ts\":%.1f,\"args\":{\"%s\":%.3f}}",
              e.tid, e.name, ts_us, e.name, e.counter_value);
    } else if (e.phase == 'i') {
      fprintf(f,
              "{\"pid\":1,\"tid\":%u,\"ph\":\"i\",\"name\":\"%s\","
              "\"ts\":%.1f,\"s\":\"g\"}",
              e.tid, e.name, ts_us);
    } else {
      fprintf(f,
              "{\"pid\":1,\"tid\":%u,\"ph\":\"%c\",\"name\":\"%s\","
              "\"cat\":\"%s\",\"ts\":%.1f,\"dur\":%.1f}",
              e.tid, e.phase, e.name, e.category, ts_us,
              e.dur * 1e6);
    }
  }

  fputs("\n]}\n", f);
  fclose(f);
  LOG("Profiler: wrote ", count_, " events to ", path);
}

}  // namespace G
```

### Key design decisions

**Ring buffer, not growable array.** The ring buffer has a fixed capacity (1M events = ~64MB). When full, oldest events are overwritten. This means the trace always contains the most recent N seconds of data, which is exactly what you want — you notice a spike, press the dump key, and the trace contains the spike plus context around it. No unbounded memory growth.

**`const char*` names, not copied strings.** Zone names are always string literals (`__FUNCTION__` or `"Physics::Update"`), so we store a pointer without copying. This keeps `TraceEvent` at 48 bytes and avoids any allocation per event.

**Seconds internally, microseconds on flush.** We use `NowInSeconds()` (which we already have) during recording to avoid conversion overhead, then convert to microseconds when writing JSON. The epoch subtraction prevents floating-point precision loss.

**`GAME_WITH_PROFILING` compile flag.** Separate from `GAME_WITH_ASSERTS` so profiling can be enabled in optimized builds. When off, all macros expand to nothing — zero cost.

**Single-threaded ring buffer.** The main game loop is single-threaded for the hot path (update + render). The file watcher and audio callback threads could also push events, but since we're using a non-atomic ring buffer, events from other threads may interleave unsafely. Two options:
1. Only instrument the main thread (good enough for frame diagnosis).
2. Use one ring buffer per thread (more complex, implement later if needed).

Option 1 is fine for now. The viewer shows `tid` per event, so multi-thread support just means the events appear on separate timeline rows.

### CMake integration

```cmake
option(ENABLE_PROFILING "Enable trace profiler instrumentation" OFF)

if(ENABLE_PROFILING)
    target_compile_definitions(Game PRIVATE GAME_WITH_PROFILING)
endif()
```

No new library target, no vendoring, no additional dependencies. Just a compile flag.

### Instrumentation plan

#### Frame boundaries and top-level phases

In the main loop (`game.cc:Run`):

```cpp
void Run() {
    // ...
    const auto frame_start = NowInSeconds();
    {
        PROFILE_SCOPE_N("StartFrame");
        e_->StartFrame();
    }
    {
        PROFILE_SCOPE_N("PollEvents");
        SDL_StartTextInput();
        for (SDL_Event event; SDL_PollEvent(&event);) {
            // ...
        }
    }
    {
        PROFILE_SCOPE_N("Update");
        while (accum >= kStep) {
            Update(t, kStep);
            t += kStep;
            accum -= kStep;
        }
    }
    {
        PROFILE_SCOPE_N("Render");
        Render();
    }
    PROFILE_FRAME;
    stats_.AddSample((NowInSeconds() - frame_start) * 1000.0);
}
```

#### Subsystem zones

Add `PROFILE_SCOPE;` at the top of each subsystem entry point:

| Subsystem | Where | Auto-generated zone name |
|-----------|-------|--------------------------|
| Physics step | `physics.cc: Update()` | `"Update"` (or use `PROFILE_SCOPE_N("Physics::Update")`) |
| Lua update | `lua.cc: Update()` | `"Update"` |
| Lua draw | `lua.cc: Draw()` | `"Draw"` |
| Batch render | `renderer.cc: Render()` | `"Render"` |
| Asset loading | `assets.cc: LoadAll()` | `"LoadAll"` |
| Hot reload | `game.cc: PendingChanges block` | `"HotReload"` |
| Sound callback | `game.cc: AudioCallback` | `"AudioCallback"` |

Since multiple subsystems have `Update()` or `Render()` functions, use `PROFILE_SCOPE_N` with explicit names to disambiguate in the flame graph.

#### Counters

Track key metrics as counter events — these appear as line graphs in Perfetto:

```cpp
// After frame_allocator.Reset():
PROFILE_COUNTER("Frame Arena (KB)", frame_allocator.used_memory() / 1024.0);
PROFILE_COUNTER("Lua Memory (KB)", e_->lua.MemoryUsage() / 1024.0);
```

#### Hotkey to toggle recording

Bind a key (e.g., F12) to toggle recording on/off:

```cpp
if (event.type == SDL_KEYDOWN &&
    event.key.keysym.scancode == SDL_SCANCODE_F12) {
    GetProfiler()->ToggleRecording();
}
```

Workflow:
1. Play the game normally (zero overhead — profiling macros check one bool).
2. Notice a hitch or slow frame.
3. Press F12 — starts recording.
4. Reproduce the slow frame.
5. Press F12 again — stops recording, writes `trace.json`.
6. Open `trace.json` in [Perfetto UI](https://ui.perfetto.dev) — zoom to the spike, see the flame graph.

Alternatively, auto-dump after a fixed number of frames or seconds.

#### GPU timing

Add GL timer queries to measure GPU-side work. This uses `GL_ARB_timer_query` (standard since OpenGL 3.3, which we require):

```cpp
class GpuTimer {
 public:
  void Init() {
    glGenQueries(2, queries_);
  }

  void Begin() {
    glQueryCounter(queries_[0], GL_TIMESTAMP);
  }

  void End() {
    glQueryCounter(queries_[1], GL_TIMESTAMP);
  }

  // Call next frame to read results without stalling.
  // Returns GPU time in seconds, or -1 if not yet available.
  double ReadResult() {
    GLint available = 0;
    glGetQueryObjectiv(queries_[1], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return -1.0;
    GLuint64 start, end;
    glGetQueryObjectui64v(queries_[0], GL_QUERY_RESULT, &start);
    glGetQueryObjectui64v(queries_[1], GL_QUERY_RESULT, &end);
    return static_cast<double>(end - start) / 1e9;
  }

 private:
  GLuint queries_[2];
};
```

GPU results from the previous frame are written as trace events on a "GPU" pseudo-thread. The one-frame delay is inherent to how GPU timer queries work — we never stall waiting for results.

This is lower priority than CPU zones. The CPU flame graph alone answers most "why was this frame slow?" questions. GPU timing is useful when you suspect you're GPU-bound (many draw calls, large textures, expensive shaders).

### devenv.nix changes

Add a build script for profiling-instrumented builds:

```nix
scripts."game-profile" = {
    exec = ''
        cmake -DENABLE_PROFILING=ON -G Ninja -S . -B build \
        && cmake --build build --target Game \
        && ./build/game run assets
    '';
};
```

No additional nix packages needed — the viewer is Perfetto UI in a browser.

### Example output in Perfetto

After pressing F12 twice (start/stop), opening `trace.json` in Perfetto shows:

```
Thread 1 (Main)
├─ Frame ─────────────────────────────────── 16.2ms
│  ├─ StartFrame ─────── 0.1ms
│  ├─ PollEvents ─────── 0.3ms
│  ├─ Update ─────────── 8.4ms
│  │  ├─ Physics::Update ── 2.1ms
│  │  └─ Lua::Update ────── 6.3ms  ← spike here
│  └─ Render ─────────── 7.4ms
│     ├─ Lua::Draw ──────── 5.2ms
│     └─ BatchRenderer ──── 2.2ms
├─ Frame ─────────────────────────────────── 15.8ms
│  └─ ...
```

Counter tracks below show Lua memory and arena usage over time, correlated with the frame timeline.

## Phase 2: Tracy integration

Once Phase 1 is working and we have experience with what metrics matter, [Tracy](https://github.com/wolfpld/tracy) replaces the Chrome Tracing backend with a real-time streaming profiler. The `PROFILE_*` callsites stay the same — only the macro implementations change.

### Why Tracy after Chrome Tracing

| Feature | Chrome Tracing (Phase 1) | Tracy (Phase 2) |
|---------|--------------------------|-----------------|
| Per-frame CPU timeline | Yes | Yes |
| Real-time streaming | No (file-based) | Yes (TCP to viewer) |
| GPU timeline | Manual (GL timer queries) | Built-in (`TracyGpuZone`) |
| Memory pool tracking | Counter events only | Per-allocation tracking with named pools |
| Lock contention | No | Yes (`TracyLockable`) |
| Overhead when not profiling | One bool check per macro | One atomic load per macro (~1ns) |
| Dependencies | None | Vendor ~200KB, LZ4, worker thread |
| Viewer | Browser (Perfetto UI) | Standalone app (must build or install) |
| Multi-thread safety | Manual (per-thread buffers) | Built-in (lock-free per-thread queues) |

The key upgrades are **real-time streaming** (see data live as you play, no stop-record-open-file cycle), **built-in GPU profiling** (replaces our manual `GpuTimer`), and **lock contention analysis** (which thread held the lock, how long others waited).

### Architecture

```
Game process                              Tracy Viewer (separate process)
┌──────────────────────────┐              ┌─────────────────────┐
│ Game threads              │              │                     │
│   ZoneScoped / FrameMark │              │  Timeline view      │
│   TracyGpuZone           │              │  Flame graph        │
│   TracyAlloc/Free        │              │  Memory plots       │
│         │                │              │  Lock analysis      │
│         ▼                │              │  Statistics         │
│ Lock-free per-thread     │   TCP:8086   │                     │
│ queues ──► Tracy worker  │─────────────►│                     │
│ thread (LZ4 compress)    │              │                     │
└──────────────────────────┘              └─────────────────────┘
```

The Tracy client is a library compiled into the game. A dedicated worker thread dequeues events from lock-free per-thread queues, LZ4-compresses them, and streams to the viewer over TCP. The viewer is a standalone application that visualizes the data in real time.

With `TRACY_ON_DEMAND`, each macro does one atomic load to check if a viewer is connected. If not, it returns immediately (~1ns). This means Tracy-instrumented builds run at full speed normally, and you only pay the profiling cost when you connect the viewer.

### Vendoring

Tracy is designed for vendoring. The client library is in the `public/` directory:

```
libraries/tracy/
  public/
    tracy/
      Tracy.hpp             # C++ API (ZoneScoped, FrameMark, etc.)
      TracyC.h              # C API (TracyCZone, etc.)
      TracyOpenGL.hpp       # GL timer query wrappers
    client/                 # Implementation files
    common/                 # Shared utilities
    TracyClient.cpp         # Single compilation unit — #includes everything
```

`TracyClient.cpp` is the only file we compile. Everything is guarded by `#ifdef TRACY_ENABLE` — when disabled, the entire client compiles to nothing.

### CMake integration

```cmake
option(ENABLE_TRACY "Enable Tracy profiler (replaces built-in trace profiler)" OFF)

if(ENABLE_TRACY)
    add_library(tracy STATIC libraries/tracy/public/TracyClient.cpp)
    target_include_directories(tracy PUBLIC libraries/tracy/public)
    target_compile_definitions(tracy PUBLIC
        TRACY_ENABLE
        TRACY_ON_DEMAND        # Only collect when viewer connected
        TRACY_ONLY_LOCALHOST   # Security: only accept local connections
    )
    target_link_libraries(Game PRIVATE tracy)
else()
    # Header-only stub — all macros expand to nothing
    add_library(tracy INTERFACE)
    target_include_directories(tracy INTERFACE libraries/tracy/public)
    target_link_libraries(Game PRIVATE tracy)
endif()
```

When `ENABLE_TRACY=OFF`, the tracy target is INTERFACE-only — zero code compiled, zero overhead.

### Compile-time flags

| Flag | Purpose |
|------|---------|
| `TRACY_ENABLE` | Master switch. Off = all macros are empty. |
| `TRACY_ON_DEMAND` | Only collect data when a viewer is connected. Without this, Tracy buffers from startup and can consume GBs of RAM. Critical for us. |
| `TRACY_ONLY_LOCALHOST` | Only accept connections from 127.0.0.1. Security measure. |
| `TRACY_NO_BROADCAST` | Disable UDP discovery broadcast. |
| `TRACY_NO_EXIT` | Keep client alive on exit until viewer disconnects (prevents data loss). |

### Migration: `src/profiler.h` changes

The `PROFILE_*` macros get a new implementation backed by Tracy instead of our ring buffer. All callsites remain unchanged.

```cpp
#ifdef GAME_WITH_TRACY

#include "tracy/Tracy.hpp"
#include "tracy/TracyOpenGL.hpp"

#define PROFILE_SCOPE       ZoneScoped
#define PROFILE_SCOPE_N(n)  ZoneScopedN(n)
#define PROFILE_FRAME       FrameMark

#define PROFILE_COUNTER(name, value) TracyPlot(name, static_cast<double>(value))

// New Tracy-only macros (not available in Chrome Tracing backend):
#define PROFILE_GPU_INIT    TracyGpuContext
#define PROFILE_GPU_ZONE(n) TracyGpuZone(n)
#define PROFILE_GPU_COLLECT TracyGpuCollect

#define PROFILE_ALLOC(ptr, size)         TracyAlloc(ptr, size)
#define PROFILE_FREE(ptr)                TracyFree(ptr)
#define PROFILE_ALLOC_N(ptr, size, name) TracyAllocN(ptr, size, name)
#define PROFILE_FREE_N(ptr, name)        TracyFreeN(ptr, name)

#define PROFILE_MSG(text, size)  TracyMessage(text, size)
#define PROFILE_MSG_L(literal)   TracyMessageL(literal)

#define PROFILE_THREAD_NAME(n)   tracy::SetThreadName(n)

#elif defined(GAME_WITH_PROFILING)
// ... existing Chrome Tracing implementation ...
#else
// ... empty macros ...
#endif
```

This three-way `#ifdef` means:
- `GAME_WITH_TRACY` → full Tracy backend (Phase 2).
- `GAME_WITH_PROFILING` → Chrome Tracing JSON backend (Phase 1).
- Neither → all macros empty (release builds).

### Tracy-specific instrumentation (beyond Phase 1)

These features become available once Tracy is integrated, on top of the existing `PROFILE_SCOPE` / `PROFILE_COUNTER` callsites:

#### GPU zones

Replace our manual `GpuTimer` class with Tracy's built-in GL timer query management:

```cpp
// Once, after gladLoadGLLoader:
PROFILE_GPU_INIT;

// In BatchRenderer::Render:
void BatchRenderer::Render(Allocator* scratch) {
    PROFILE_SCOPE;
    {
        PROFILE_GPU_ZONE("Draw Batched Geometry");
        // ... glDrawElements calls ...
    }
    {
        PROFILE_GPU_ZONE("Resolve MSAA");
        // ... glBlitFramebuffer ...
    }
}

// After SDL_GL_SwapWindow:
PROFILE_GPU_COLLECT;
```

Tracy pre-allocates a pool of 64K GL query objects and reads results from previous frames automatically — no GPU stalls, no manual double-buffering.

#### Memory tracking

Track arena allocations with named pools:

```cpp
// In ArenaAllocator::Alloc:
void* ArenaAllocator::Alloc(size_t size, size_t align) {
    // ... existing logic ...
    PROFILE_ALLOC_N(result, size, "Arena");
    return result;
}

// In ArenaAllocator::Reset:
void ArenaAllocator::Reset() {
    PROFILE_FREE_N(reinterpret_cast<void*>(beginning_), "Arena");
    // ... existing logic ...
}
```

The Tracy viewer shows memory allocation timelines per named pool — useful for spotting arena fragmentation or unexpected growth.

#### Lock contention

Instrument `LockMutex` to show lock acquisition time as zones:

```cpp
class LockMutex {
 public:
  LockMutex(SDL_mutex* mu) : mu_(mu) {
    PROFILE_SCOPE_N("LockMutex");
    SDL_LockMutex(mu_);
  }
  ~LockMutex() { SDL_UnlockMutex(mu_); }
 private:
  SDL_mutex* mu_;
};
```

For full contention analysis (which thread held the lock, how long others waited), Tracy's `TracyLockable` wrappers would need a custom SDL_mutex adapter — lower priority, only worth it if lock contention is a real problem.

#### Thread naming

```cpp
// In CheckChangedFiles thread:
PROFILE_THREAD_NAME("FileWatcher");

// In ThreadPool worker threads:
PROFILE_THREAD_NAME("Worker");
```

#### Lua profiling

Expose Tracy zones to Lua scripts:

```lua
G.profiler.begin_zone("AI Update")
-- ... AI logic ...
G.profiler.end_zone()
```

The C++ zones already cover the main frame phases — Lua code runs inside the `Lua::Update` and `Lua::Draw` zones. Per-Lua-function breakdown is a refinement for when you need to know which Lua function is slow.

### devenv.nix changes for Tracy

```nix
packages = with pkgs; [
    # ... existing packages ...
    tracy       # Tracy profiler viewer
];

scripts."game-tracy" = {
    exec = ''
        cmake -DENABLE_TRACY=ON -G Ninja -S . -B build \
        && cmake --build build --target Game \
        && ./build/game run assets
    '';
};
```

Workflow:
1. `game-tracy` — builds and runs with Tracy instrumentation.
2. In another terminal: `tracy` — launches the viewer, auto-discovers the running game.
3. Play the game. The viewer shows the flame graph in real time.
4. See a spike → click on it → drill down to the subsystem.

Or capture to a file: `tracy-capture -o capture.tracy` while the game runs, then `tracy capture.tracy` to analyze offline.

### Tracy overhead

| Configuration | Cost |
|---------------|------|
| `ENABLE_TRACY=OFF` | **Zero** — empty macros, tracy compiles to nothing |
| Tracy on, no viewer connected | **~1ns per macro** — one atomic load |
| Tracy on, viewer connected | **~15ns per zone** — 1000 zones/frame = 15us = <0.1% of 16.67ms budget |
| GPU zones | **~0** (async timer queries, results read from previous frame) |
| Callstack capture (`ZoneScopedS`) | **200-500ns per zone** — only when you need call stacks |

## Implementation plan

### Phase 1: Chrome Tracing — core tracing infrastructure

1. **Add `src/profiler.h` and `src/profiler.cc`** — `TraceEvent` ring buffer, `ProfileZone` RAII guard, `PROFILE_SCOPE` / `PROFILE_SCOPE_N` / `PROFILE_FRAME` / `PROFILE_COUNTER` macros, JSON flush.
2. **Add `ENABLE_PROFILING` CMake option** — defines `GAME_WITH_PROFILING`.
3. **Instrument the main loop** — `PROFILE_FRAME` after swap, `PROFILE_SCOPE_N` for StartFrame/PollEvents/Update/Render.
4. **Instrument subsystems** — `PROFILE_SCOPE_N` in `Physics::Update`, `Lua::Update`, `Lua::Draw`, `BatchRenderer::Render`.
5. **Add F12 hotkey** to toggle recording and flush `trace.json`.
6. **Add counter events** — Lua memory, frame arena usage.
7. **Add `game-profile` devenv script.**

This answers "why was frame N slow?" for most cases. Experiment with it and figure out what's missing.

### Phase 2: Tracy — real-time profiling

8. **Vendor Tracy** into `libraries/tracy/` — copy `public/` directory only (~200KB).
9. **Add `ENABLE_TRACY` CMake option** — builds TracyClient.cpp, defines `GAME_WITH_TRACY`.
10. **Update `src/profiler.h`** — three-way `#ifdef`: Tracy backend, Chrome Tracing backend, or empty macros. Existing `PROFILE_*` callsites unchanged.
11. **Add GPU zones** — `PROFILE_GPU_INIT` after GL init, `PROFILE_GPU_ZONE` in BatchRenderer::Render. Replaces manual GpuTimer.
12. **Add memory pool tracking** — `PROFILE_ALLOC_N` / `PROFILE_FREE_N` in ArenaAllocator.
13. **Add thread naming** — FileWatcher, ThreadPool workers.
14. **Add Tracy viewer to devenv.nix** and `game-tracy` script.

### Phase 3: Advanced instrumentation

15. **Lock contention tracking** — instrument `LockMutex` and audio callback mutex.
16. **Lua zone API** — `G.profiler.begin_zone("name")` / `G.profiler.end_zone()` for game scripts.
17. **Per-thread ring buffers** for Chrome Tracing backend (if still used alongside Tracy).

### What not to do

- **Don't replace gperftools/pprof** — they serve a different purpose (aggregate CPU hotspots). Keep them.
- **Don't replace Callgrind** — it's valuable for cache analysis and deterministic measurement.
- **Don't replace RenderDoc** — it's the right tool for GPU state debugging.
- **Don't replace the TIMER macro** — it's useful for quick one-off measurements in code you're actively editing.
- **Don't replace the Tab debug overlay** — it's the quick at-a-glance tool. The trace is for deep investigation.

## Overhead summary

| Configuration | Cost |
|---------------|------|
| `ENABLE_PROFILING=OFF` (default) | **Zero** — empty macros |
| Profiling enabled, not recording | **~1ns per macro** — one branch on a bool |
| Profiling enabled, recording | **~20-50ns per zone** — write to ring buffer, call `NowInSeconds()` twice |
| JSON flush (1M events) | **~200ms** — one-time I/O when recording stops, not on the hot path |
