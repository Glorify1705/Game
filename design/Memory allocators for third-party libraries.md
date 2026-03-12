# Memory Allocators for Third-Party Libraries

## Problem

The engine allocates a 4 GiB arena at startup (`StaticAllocator<Gigabytes(4)>` in `game.cc:60`) and routes all subsystem allocations through it. This gives deterministic memory behavior: after initialization, the engine's own code never calls `malloc` or `mmap`.

However, **Lua and SQLite3 break this guarantee**. Both are initialized with `SystemAllocator::Instance()` (Lua explicitly at `game.cc:175`, SQLite implicitly via its default `malloc`). Every `lua_pcall`, every `sqlite3_step`, every GC cycle, and every string interning operation goes through the system allocator, which calls `mmap`/`brk` unpredictably.

The goal is to make the **entire process** deterministic: allocate all memory at startup, then never request OS memory again during gameplay.

## Current Architecture

```
GameMain()
  |
  new StaticAllocator<4 GiB>        <-- single allocation from heap
  |
  +-- Game object (in arena)
  |     +-- EngineModules
  |           +-- Console(allocator)            arena
  |           +-- Filesystem(allocator)         arena
  |           +-- Shaders(allocator)            arena
  |           +-- BatchRenderer(allocator)      arena
  |           +-- Sound(allocator)              arena
  |           +-- Physics(allocator)            arena
  |           +-- Renderer(allocator)           arena
  |           +-- ThreadPool(allocator)         arena
  |           +-- frame_allocator(128 MiB)      arena, reset per frame
  |           +-- hotload_allocator(128 MiB)    arena, reset on reload
  |           |
  |           +-- Lua(SystemAllocator)          !! SYSTEM MALLOC !!
  |           +-- sqlite3 (default malloc)      !! SYSTEM MALLOC !!
```

Lua already has a custom allocator hook (`lua_newstate(&Lua::LuaAlloc, this)` at `lua.cc:582`), but it delegates to `SystemAllocator` which wraps `std::malloc`. SQLite has no custom allocator configured at all.

## Design Options

### Option A: mimalloc with Exclusive Arenas

Use [mimalloc](https://github.com/microsoft/mimalloc) to create isolated, fixed-budget heaps for Lua and SQLite. mimalloc provides a first-class arena API that allows registering pre-allocated memory regions and binding heaps to them.

**How it works:**

1. At startup, carve regions from the 4 GiB arena for Lua and SQLite
2. Register each region as an exclusive mimalloc arena via `mi_manage_os_memory_ex`
3. Create a `mi_heap_t` bound to each arena via `mi_heap_new_in_arena`
4. Route Lua's `lua_Alloc` and SQLite's `sqlite3_mem_methods` through their respective heaps
5. Optionally set `mi_option_disallow_os_alloc` for a hard no-OS guarantee

```
StaticAllocator<4 GiB>
  |
  +-- Engine subsystems (ArenaAllocator, as today)
  |
  +-- sqlite_region (32 MiB) --> mi_manage_os_memory_ex(exclusive=true)
  |                                 --> mi_heap_new_in_arena()
  |                                     --> sqlite3_config(SQLITE_CONFIG_MALLOC, ...)
  |
  +-- lua_region (64 MiB) ------> mi_manage_os_memory_ex(exclusive=true)
                                    --> mi_heap_new_in_arena()
                                        --> lua_newstate(mi_alloc, ctx)
```

**Pros:**
- Full general-purpose allocator with excellent performance (10-25ns per op)
- Thread-safe heaps in v3 (future-proof for multithreaded Lua or SQLite)
- `mi_heap_destroy` for bulk deallocation at shutdown
- Per-heap statistics and ownership queries
- `mi_free` is heap-agnostic (no need to track which heap allocated what)
- Well-maintained (Microsoft, used in Azure/Bing/Unreal)

**Cons:**
- ~10k lines of C, ~150 KiB static library -- nontrivial dependency
- Arena metadata overhead: mimalloc needs some internal bookkeeping that *may* spill outside the arena (see [issue #937](https://github.com/microsoft/mimalloc/issues/937)); with 32+ MiB arenas this is negligible
- `mi_option_disallow_os_alloc` is global, affecting all heaps, not per-arena
- Overkill if the allocation patterns are simple

### Option B: SQLite's Built-in memsys5 + mimalloc for Lua Only

SQLite ships with `memsys5`, a buddy allocator that operates on a fixed buffer. Activate it via:

```c
static char sqlite_heap[8 * 1024 * 1024];
sqlite3_config(SQLITE_CONFIG_HEAP, sqlite_heap, sizeof(sqlite_heap), 64);
```

This eliminates SQLite's malloc usage entirely with zero external dependencies. Then use mimalloc (or even a simpler approach) only for Lua.

**Pros:**
- Zero-dependency solution for SQLite -- memsys5 is built into the amalgamation
- Battle-tested (it's how SQLite runs on embedded systems)
- Separate `SQLITE_CONFIG_PAGECACHE` pool for the page cache gives further control
- Simpler: no need to wire up `sqlite3_mem_methods`

**Cons:**
- memsys5 uses power-of-two sizing, so ~30% more internal fragmentation than mimalloc
- Need a larger budget (e.g. 16 MiB instead of 8 MiB) to account for fragmentation
- Still need a solution for Lua

### Option C: rpmalloc with Custom Memory Provider

Use [rpmalloc](https://github.com/mjansson/rpmalloc) (2 files, ~5k lines) with a custom `memory_map` callback that hands out chunks from a pre-allocated buffer.

**Pros:**
- Tiny footprint (2 files)
- Designed for games (originally from Epic/Rampant Pixels)
- `rpmalloc_heap_free_all` for bulk deallocation

**Cons:**
- No built-in arena concept -- must implement the fixed-pool memory provider manually
- Heap API is **not thread-safe** (caller must synchronize)
- Requires `RPMALLOC_FIRST_CLASS_HEAPS=1` define
- Requires per-thread `rpmalloc_thread_initialize()` calls
- Less actively maintained than mimalloc

### Option D: Custom Allocator (No External Dependency)

Extend the existing `Allocator` interface with a general-purpose allocator (e.g. TLSF or a slab allocator) operating on a fixed buffer carved from the 4 GiB arena.

**Pros:**
- No external dependency
- Full control over behavior
- Fits the existing `Allocator` interface naturally

**Cons:**
- Significant implementation effort for a production-quality general-purpose allocator
- Must handle coalescing, splitting, alignment, and realloc correctly
- Likely worse performance than mimalloc/rpmalloc without extensive optimization
- Bug-prone

## Recommendation: Option A+B Hybrid

Use **memsys5 for SQLite** and **mimalloc for Lua**. This combines the strengths of both:

- SQLite gets a zero-dependency, zero-configuration fixed heap via `SQLITE_CONFIG_HEAP`
- Lua gets a high-performance general-purpose allocator via mimalloc's arena-bound heaps
- Both operate on regions carved from the existing 4 GiB `StaticAllocator`
- No OS memory calls after startup

This is the right split because:
- **SQLite's allocation pattern is simple**: moderate number of allocations, predictable sizes (page cache, statement objects, B-tree nodes). memsys5's buddy allocator handles this well.
- **Lua's allocation pattern is complex**: many small objects (strings, tables, closures), frequent alloc/free churn from GC, realloc during table growth. This benefits from mimalloc's sophisticated free-list sharding and size classes.

## Implementation Plan

### Phase 1: SQLite on memsys5

1. **Compile SQLite with memsys5 enabled**: add `-DSQLITE_ENABLE_MEMSYS5` to `libraries/sqlite3.c` compilation
2. **Carve a region from the arena**: allocate a 16 MiB buffer from the main `StaticAllocator` before `sqlite3_open`
3. **Configure memsys5**: call `sqlite3_config(SQLITE_CONFIG_HEAP, buf, 16*1024*1024, 64)` before any SQLite initialization
4. **Optional page cache pool**: carve an additional 2 MiB for `SQLITE_CONFIG_PAGECACHE` with 4096-byte pages
5. **Verify**: run the asset packer and game, check `sqlite3_status(SQLITE_STATUS_MEMORY_USED, ...)` to confirm usage fits within budget

No changes to `Lua::Alloc`, `assets.cc`, or `packer.cc` -- SQLite's internal code path changes transparently.

### Phase 2: Lua on mimalloc

1. **Vendor mimalloc** as `libraries/mimalloc` (or use CMake FetchContent)
2. **Build config**: `MI_OVERRIDE=OFF`, `MI_BUILD_SHARED=OFF`, `MI_BUILD_TESTS=OFF`
3. **Create a `MimallocAllocator` class** implementing the `Allocator` interface:

```cpp
class MimallocAllocator final : public Allocator {
 public:
  MimallocAllocator(void* buffer, size_t size) {
    mi_manage_os_memory_ex(buffer, size,
                           /*committed=*/true, /*large=*/false,
                           /*zero=*/false, /*numa=*/-1,
                           /*exclusive=*/true, &arena_id_);
    heap_ = mi_heap_new_in_arena(arena_id_);
  }

  ~MimallocAllocator() override { mi_heap_destroy(heap_); }

  void* Alloc(size_t size, size_t align) override {
    return mi_heap_malloc_aligned(heap_, size, align);
  }

  void Dealloc(void* p, size_t) override { mi_free(p); }

  void* Realloc(void* p, size_t, size_t new_size, size_t align) override {
    return mi_heap_realloc_aligned(heap_, p, new_size, align);
  }

 private:
  mi_arena_id_t arena_id_;
  mi_heap_t* heap_;
};
```

4. **Replace `SystemAllocator::Instance()` in `game.cc:175`**:

```cpp
// Carve 64 MiB from the arena for Lua
auto* lua_mem = allocator->Alloc(Megabytes(64), kMaxAlign);
auto* lua_allocator = allocator->New<MimallocAllocator>(lua_mem, Megabytes(64));

// Pass to Lua instead of SystemAllocator
lua(argc, argv, db, db_assets, lua_allocator),
```

5. **Verify**: profile `Lua::MemoryUsage()` and mimalloc stats to right-size the budget

### Phase 3: Hard No-OS Guarantee (Optional)

After both SQLite and Lua are on fixed heaps:

1. Set `mi_option_set(mi_option_disallow_os_alloc, 1)` after arena registration
2. Audit for any remaining `malloc` calls (SDL callbacks, stb libraries, etc.)
3. Consider overriding `malloc` globally with mimalloc (`MI_OVERRIDE=ON`) and registering a single large arena for everything, so even SDL and stb libraries are covered

This is optional because SDL itself allocates (event queue, audio buffers) and overriding its allocator is more invasive.

## Memory Budget Sizing

| Subsystem | Budget | Rationale |
|-----------|--------|-----------|
| SQLite heap (memsys5) | 16 MiB | Asset DB is read-mostly; peak is during `packer.cc` asset processing. memsys5 fragmentation needs ~2x headroom. |
| SQLite page cache | 2 MiB | 512 pages x 4096 bytes. Covers the working set for asset lookups. |
| Lua (mimalloc arena) | 64 MiB | Scripts, tables, closures, GC overhead. Profile to right-size. `Lua::MemoryUsage()` gives live tracking. |
| Engine arena | Remainder of 4 GiB | All other subsystems, frame allocator, hot-reload scratch. |

These are starting points. The right approach is:
1. Instrument peak usage with generous budgets
2. Run the game through representative scenarios (loading, gameplay, hot-reload)
3. Set budgets to 2x observed peak
4. Add `DCHECK` assertions in the allocators when usage exceeds 75% budget (early warning)

## Lua-Specific Considerations

### GC Interaction

Lua's garbage collector frequency is proportional to allocation rate. With a fixed memory budget, if Lua approaches the limit, the GC must collect aggressively or scripts fail with out-of-memory. Strategies:

- **Tune GC parameters**: `lua_gc(L, LUA_GCINC, pause, stepmul, stepsize)` controls incremental GC pacing. Lower pause values = more aggressive collection.
- **Per-frame GC step**: the engine already calls `RunGc()`. Consider calling `lua_gc(L, LUA_GCSTEP, N)` each frame instead of a full collect, spreading GC work across frames.
- **Monitor headroom**: if `Lua::MemoryUsage()` exceeds 50% of budget, increase GC aggressiveness dynamically.

### Hot Reload

When scripts are hot-reloaded (`Lua::LoadLibraries` at `lua.cc:580`), the entire Lua state is destroyed and recreated (`lua_close` + `lua_newstate`). With mimalloc, `mi_heap_destroy` will bulk-free all Lua allocations. The new `lua_newstate` allocates from the same arena, which is now empty. This is clean and efficient.

### Allocation Patterns

Lua 5.x allocations are predominantly:
- **Strings**: 32 + length bytes. Short strings (<40 chars) are interned. Game scripts have many short identifier strings.
- **Tables**: 56 bytes base + 16 bytes per array element + 40 bytes per hash element. Tables are the primary data structure.
- **Closures**: 32 + 8N bytes (N upvalues). Every function call creates or reuses closures.
- **GC metadata**: proportional to live object count.

Peak allocation typically happens during script loading (compiling, loading prototypes). Steady-state is lower but bursty (entity creation/destruction).

## SQLite-Specific Considerations

### memsys5 Behavior

memsys5 is a buddy allocator: it splits power-of-two blocks. A 100-byte allocation uses a 128-byte block. This means ~20-30% overhead for typical SQLite allocation patterns.

The minimum allocation size parameter (3rd argument to `SQLITE_CONFIG_HEAP`) controls the smallest block. For game use, 64 bytes is a good floor -- SQLite's smallest allocations are around 32-64 bytes.

When the pool is exhausted, SQLite returns `SQLITE_NOMEM`. The engine should `DCHECK` on this.

### Page Cache Pool

`SQLITE_CONFIG_PAGECACHE` provides a separate fast pool for database pages. When this pool is exhausted, SQLite falls back to the general allocator (memsys5 in our case). For a read-mostly asset database, 512 pages (2 MiB at 4096 bytes/page) is generous.

### Thread Safety

SQLite's default threading mode is `SQLITE_THREADSAFE=1` (serialized). memsys5 is protected by SQLite's internal mutex. No additional synchronization needed.

## Monitoring and Debugging

### SQLite Memory Stats

```cpp
int current, peak;
sqlite3_status(SQLITE_STATUS_MEMORY_USED, &current, &peak, 0);
// current = bytes currently allocated
// peak = high-water mark
```

Expose these through the debug console.

### Lua Memory Stats

Already tracked via `Lua::MemoryUsage()` and `Lua::AllocatorStats()`. With mimalloc, additionally available:

```cpp
mi_heap_visit_blocks(heap, true, [](const mi_heap_t*, const mi_heap_area_t* area,
                                     void*, size_t, void*) {
    // area->used, area->committed, area->blocks, etc.
    return true;
}, nullptr);
```

### Debug Assertions

Add `DCHECK`s in the allocator wrappers:
- Lua: assert `nsize == 0 || result != nullptr` (allocation must not fail silently)
- SQLite: check return values of `sqlite3_config` calls
- Both: log warnings when usage exceeds 75% of budget

## File Changes

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add mimalloc subdirectory, link to Game target |
| `libraries/mimalloc/` | Vendor mimalloc (or use FetchContent) |
| `src/allocators.h` | Add `MimallocAllocator` class |
| `src/game.cc` | Carve regions, init memsys5 before sqlite3_open, create MimallocAllocator for Lua |
| `libraries/sqlite3.c` | Add `#define SQLITE_ENABLE_MEMSYS5` |
| No changes needed | `src/lua.cc`, `src/lua.h`, `src/assets.cc`, `src/packer.cc` -- transparent |

## Open Questions

- **Budget sizing**: need to profile actual peak usage under representative load before committing to numbers. Start generous, tighten later.
- **SDL/stb allocations**: these still use system malloc. Is this acceptable, or should we pursue `MI_OVERRIDE=ON` to capture everything?
- **4 GiB StaticAllocator**: with 64+16+2 MiB carved out for Lua/SQLite, this leaves ~3.9 GiB for the engine. Is this enough, or should the total be increased?
- **mimalloc version**: v3 (latest) has the best heap API but is newer. v2.x is more battle-tested. Pin to a specific release.
