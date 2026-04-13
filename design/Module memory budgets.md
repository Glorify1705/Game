---
status: in-progress
tags: [memory, allocators, architecture, renderer]
---

# Module Memory Budgets

## Glossary

- **Memory budget**: A fixed upper bound on how much memory a subsystem is
  allowed to use. Exceeding the budget is a detectable error rather than silent
  corruption.
- **Arena allocator**: A linear bump allocator. Allocations are fast
  (pointer increment), but individual deallocations are not possible — you
  reset the entire arena at once. The engine's `ArenaAllocator` class.
- **Sub-arena**: An arena carved from a parent arena. The parent's pointer
  advances by the sub-arena's total size at creation time, giving the child a
  fixed region. Used today for `lua_allocator` and `frame_allocator`.
- **Watermark**: The peak memory usage observed during a run. Useful for
  right-sizing budgets.

## Problem

All engine subsystems share a single 4 GiB arena (`kEngineMemory` in
`game.cc:59`). Each subsystem receives a raw `Allocator*` pointer to this
shared arena and allocates freely from it. There are no per-module budgets,
no isolation, and no overflow detection.

The two exceptions are Lua (64 MiB dedicated sub-arena with mimalloc) and
the per-frame allocator (128 MiB sub-arena, reset each frame).

### Concrete issues

| Issue | Where | Impact |
|---|---|---|
| Batch renderer has fixed 16 MiB command buffer with no overflow check | `renderer.cc:46`, `AddCommand()` | Memory corruption / crash on complex frames |
| `FixedArray` overflow checked only by `DCHECK` (disabled in release) | `array.h` | Silent corruption in release builds |
| No per-module memory accounting | `game.cc` EngineModules | Can't tell which subsystem is using how much |
| One subsystem's growth can starve others | Shared arena | Hard to diagnose OOM — which module ran out? |
| Renderer pre-allocates huge fixed arrays regardless of game needs | `renderer.cc:839-851` | 1M sprites + 65K spritesheets allocated even for Pong |
| No way to tune memory for target platform | — | 4 GiB is fine on desktop, too much for web/mobile |

### The batch renderer crash

The immediate motivation: the batch renderer allocates a 16 MiB command buffer
(`kCommandMemory = 1 << 24`) and a 1M-entry command queue. `AddCommand()`
writes commands into this buffer via `memcpy` without checking whether
`pos_ + size` exceeds the buffer. A sufficiently complex frame (many draw
calls with distinct textures/transforms forcing flushes) can overflow the
buffer, causing memory corruption.

This is the crash the user has observed. Fixing it requires either:
- Bounds checking + graceful handling (flush and reset mid-frame), or
- Dynamic growth, or
- A large enough budget that overflow is practically impossible.

The first option is the right one — it fits the engine's philosophy of
explicit, bounded memory.

## Current memory map

Approximate layout of the 4 GiB arena after `EngineModules` construction:

```
0 GiB                                                    4 GiB
|================================================================|
| Game  | Engine modules (shared)     | Lua   | Frame  | Free   |
| obj   | Console, Sound, Renderer,   | 64 MiB| 128 MiB|        |
|       | Physics, Input, BatchRend,  |       |        |        |
|       | ThreadPool, HotReload, ...  |       |        |        |
|-------|-----------------------------+-------+--------+--------|
  ~1 KB   ~50-100 MiB (depends on      64 MiB  128 MiB  ~3.8 GiB
          loaded assets)
```

The shared region has no internal boundaries. A sprite-heavy game that loads
thousands of assets pushes the shared pointer forward, eating into free space
that other systems might need later.

### Per-module fixed limits (current)

| Module | Constant | Limit | Notes |
|---|---|---|---|
| BatchRenderer | `kCommandMemory` | 16 MiB | Command data buffer |
| BatchRenderer | `commands_` | 1M entries | Command queue |
| Renderer | `loaded_sprites_` | 1M entries | FixedArray |
| Renderer | `loaded_spritesheets_` | 65K entries | FixedArray |
| Renderer | `loaded_images_` | 1K entries | FixedArray |
| Renderer | `fonts_` | 512 entries | FixedArray |
| Sound | `kMaxStreams` | 128 | Embedded array |
| Sound | qoa/pcm samplers | 256 each | FixedArray |
| CollisionWorld | `kMaxColliders` | 4096 | Embedded |
| CollisionWorld | `kMaxTriggerPairs` | 1024 | Embedded |
| Timer | `kMaxTimers` | 256 | Embedded array |
| Console | `kMaxLines` | 1024 | Embedded |
| Lua | lua_allocator | 64 MiB | Sub-arena |
| Frame | frame_allocator | 128 MiB | Sub-arena, per-frame |
| Profiler | `kMaxEvents` | 1M | Ring buffer |
| Input | `kQueueSize` | 256 | Event queue |

These limits are scattered across headers with no central documentation.
Some are generous (1M sprites), some are tight (256 timers), and the batch
renderer's is the only one that can cause corruption.

## Design

### Core idea: per-module sub-arenas with watermark tracking

Give each major subsystem its own sub-arena carved from the 4 GiB pool.
Each sub-arena has a fixed budget. Subsystems allocate from their own arena
and cannot affect each other's memory.

```
4 GiB engine arena
|================================================================|
| Renderer | BatchRend | Sound | Physics | Lua    | Frame  | ... |
| 64 MiB   | 32 MiB    | 8 MiB | 16 MiB | 64 MiB | 128 MiB|    |
|----------|-----------|-------|---------|--------|--------|-----|
```

Each sub-arena tracks its **watermark** (peak `used_memory()`). At shutdown
or via the debug console, the engine reports:

```
Module memory watermarks:
  Renderer:      12.3 / 64.0 MiB (19%)
  BatchRenderer:  4.1 / 32.0 MiB (13%)
  Sound:          0.8 /  8.0 MiB (10%)
  Physics:        2.1 / 16.0 MiB (13%)
  Lua:           41.2 / 64.0 MiB (64%)
  Frame (peak):  18.7 / 128.0 MiB (15%)
```

This makes it obvious when a budget is too tight (high %) or wastefully large
(low %).

### Batch renderer overflow fix

The immediate crash fix, independent of the broader budget system:

1. **Add bounds checking to `AddCommand()`.** Before writing, check
   `pos_ + aligned_size <= kCommandMemory`. If it would overflow:
2. **Flush mid-frame.** Call `Render()` to submit the current batch to the
   GPU, then reset `pos_` and `commands_` count to zero. Continue recording.
3. **Track flush count.** Add a `flush_overflow` counter to `FrameStats` so
   it's visible in the debug overlay. Frequent overflow flushes indicate the
   budget should be increased.

This is how mature renderers handle command buffer limits — bgfx, sokol_gfx,
and Godot all flush-and-continue rather than crash or grow.

### Module arena construction

Modify `EngineModules` to create named sub-arenas:

```cpp
EngineModules(..., Allocator* allocator)
    : renderer_arena(allocator, Megabytes(64)),
      batch_renderer_arena(allocator, Megabytes(32)),
      sound_arena(allocator, Megabytes(8)),
      physics_arena(allocator, Megabytes(16)),
      // ... subsystems use their own arena ...
      batch_renderer(viewport, &shaders, &batch_renderer_arena),
      sound(channels, buffer_samples, &sound_arena),
      renderer(assets, &batch_renderer, db, &renderer_arena),
      physics(window_size, ppm, &physics_arena),
      // Lua and frame_allocator stay as-is (already have sub-arenas)
```

Each subsystem's constructor signature is unchanged (`Allocator*`), so internal
code doesn't need modification. The change is only in how the allocator is
created.

### Budget configuration

Define budgets in a central location, not scattered across constructors:

```cpp
// module_budgets.h
namespace G {
struct ModuleBudgets {
  size_t renderer = Megabytes(64);
  size_t batch_renderer = Megabytes(32);
  size_t sound = Megabytes(8);
  size_t physics = Megabytes(16);
  size_t collision = Megabytes(4);
  size_t input = Megabytes(1);
  size_t console = Megabytes(1);
  size_t lua = Megabytes(64);
  size_t frame = Megabytes(128);
  size_t hot_reload = Megabytes(4);
  // Total: ~322 MiB default, leaving ~3.7 GiB unallocated
};
}  // namespace G
```

Later, this could be loaded from `conf.json` so game authors can tune budgets
for their specific game (e.g., a particle-heavy game might want a larger
renderer budget).

### Watermark tracking in ArenaAllocator

Add a single field to `ArenaAllocator`:

```cpp
class ArenaAllocator : public Allocator {
  // ... existing fields ...
  size_t watermark_ = 0;  // Peak used_memory()

  void* Alloc(size_t size, size_t align) override {
    // ... existing logic ...
    watermark_ = std::max(watermark_, used_memory());
    return result;
  }

  size_t watermark() const { return watermark_; }
};
```

Cost: one `max` per allocation. Negligible.

### Debug overlay integration

Add a memory section to the existing debug overlay (Tab key). Show per-module
usage and watermark alongside the existing frame stats:

```
Memory:
  Renderer:  12.3 / 64.0 MiB  [====              ] peak 14.1
  Batch:      4.1 / 32.0 MiB  [==                 ] peak  8.3
  Sound:      0.8 /  8.0 MiB  [=                  ] peak  1.2
  Lua:       41.2 / 64.0 MiB  [=============      ] peak 52.0
  Frame:     18.7 / 128.0 MiB [===                ] peak 31.4
```

### What about dynamic growth?

The engine's philosophy is explicit, bounded memory — no hidden `malloc` after
startup. Dynamic growth (reallocating a larger arena from the parent) would
violate this. If a module genuinely needs more memory than its budget, the
right response is:

1. A clear error message ("Renderer exceeded 64 MiB budget — increase
   `renderer` budget in conf.json").
2. The watermark report showing exactly how much was needed.

This is better than silent growth because it forces the developer to think
about memory, which matters for platforms with real constraints (web, consoles).

## Implementation order

1. **Batch renderer overflow fix** — immediate crash fix. Add bounds check
   to `AddCommand()`, flush-and-continue on overflow. Small, self-contained
   change.
2. **Watermark tracking** — add `watermark_` to `ArenaAllocator`. One field,
   one `max` call. Print watermarks at shutdown.
3. **Per-module sub-arenas** — change `EngineModules` constructor to create
   sub-arenas. Subsystem code unchanged (still receives `Allocator*`).
4. **Central budget definition** — extract magic numbers into `ModuleBudgets`
   struct.
5. **Debug overlay** — show live memory bars in the Tab debug view.
6. **conf.json integration** — let game authors override budgets (optional,
   only if needed).

## Alternatives considered

### Virtual memory with commit-on-demand

Reserve large virtual address ranges per module but only commit physical pages
as needed. This is how many game engines work on consoles.

Pro: No wasted physical memory. Each module gets a huge virtual range.
Con: Platform-specific (`VirtualAlloc`/`mmap` with `MAP_NORESERVE`). The
engine's arena allocator would need significant rework. Overkill for now.

Worth revisiting if the engine targets platforms with tight physical memory.

### Pool allocators per resource type

Instead of per-module arenas, use typed pool allocators (e.g., a sprite pool,
a texture pool). More memory-efficient for fixed-size objects.

Pro: Zero fragmentation for fixed-size resources.
Con: Adds complexity for a small gain. The current `FixedArray` approach is
already pool-like. Modules often need mixed allocation sizes (e.g., renderer
needs both small command entries and large texture data).

### Keep shared arena, add overflow checks everywhere

Just add bounds checking to all `FixedArray` and buffer operations without
creating per-module arenas.

Pro: Minimal code change.
Con: Doesn't solve the accounting problem (can't tell which module is using
how much). Doesn't prevent one module from starving another. Treats the
symptom, not the cause.

### Resize the command buffer

Just make `kCommandMemory` bigger (e.g., 64 MiB or 128 MiB).

Pro: One-line fix.
Con: Doesn't fix the underlying lack of bounds checking. Any fixed size can
be exceeded. The flush-and-continue approach handles any workload gracefully.
