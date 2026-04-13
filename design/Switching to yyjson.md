---
status: implemented
tags: [json, dependencies, allocators, hot-reload]
---

# Switching to yyjson

**Status: Under consideration.**

## Overview

The engine currently has a handwritten recursive-descent JSON parser at
`src/json.h` / `src/json.cc` (303 lines). It is parser-only, used internally
by `packer.cc` for the asset pipeline. The Lua-facing JSON API
(`G.filesystem.load_json` / `G.filesystem.save_json`) is declared in
`lua_filesystem.cc:37-57` but stubbed with `LUA_ERROR("Unimplemented")` —
shipping it would require a JSON encoder we do not have.

Two adjacent designs need JSON support and currently propose adding their
own:

- **Save and persistence** — was going to ship a custom binary serializer
  for Lua tables (~200 LOC). Folding it into a real JSON library removes
  the second format. See [Save and persistence](Save%20and%20persistence.md).
- **REPL and live interaction** — was going to write a "minimal
  recursive-descent parser (~200 lines)" for incoming WebSocket messages.
  Same library would handle both reads and writes.

This document proposes vendoring **yyjson** as the engine's single JSON
dependency, deleting the existing handwritten parser, and routing every
JSON path (asset packer, save store, REPL, Lua bindings) through it.

## Why yyjson

[yyjson](https://github.com/ibireme/yyjson) is a single-file C JSON library
(MIT licensed) that fits this codebase exceptionally well:

| Property | yyjson | Why it matters here |
|---|---|---|
| Single `.c` + `.h`, no build system intrusion | ✓ | Drops in next to `sqlite3.c` and `dr_wav.cc` with no FetchContent |
| Plain C, no STL, no exceptions | ✓ | Compatible with `-fno-exceptions -fno-rtti` |
| Custom allocator API (`yyjson_alc`) per call | ✓ | Routes through our `Allocator*` arenas — see below |
| Both reader and writer | ✓ | We need encoding for `save_json` and the save store; the existing parser only reads |
| Mutable document API for building output | ✓ | Same library handles REPL responses without a separate `EscapeJsonString` helper |
| Streaming safe — no global state | ✓ | Hot reload friendly (see below) |
| Fastest open-source JSON in published benchmarks | ✓ | Reader hits ~5 GB/s, writer ~3 GB/s; not the bottleneck for any plausible save file |
| No SIMD requirement | ✓ | Has SIMD fast paths but works fine without — important for the Web/WASM target |
| MIT license | ✓ | Compatible with the rest of the vendored libraries |
| ~12k LOC total (one source file, one header) | ✓ | Comparable to `dr_wav.cc` in surface area |

### Alternatives considered

| Library | Verdict |
|---|---|
| **sheredom/json.h** | Header-only, parser + writer, but ~3× slower than yyjson and lacks first-class custom allocators (uses a single user-supplied `void*` and `malloc`). Workable but strictly worse. |
| **jsmn** | Tokenizer only, no writer. Would still need a hand-rolled encoder — exactly what we're trying to avoid. |
| **cJSON** | Uses `malloc`/`free` directly with a global hook; the hook is process-wide, not per-call, which is awkward for arena-scoped parses. Also slower than yyjson. |
| **rapidjson** | C++, requires STL, exceptions optional but the natural API uses them. Not a fit for `-fno-exceptions -fno-rtti`. |
| **nlohmann/json** | C++ STL, exceptions, ~25k LOC of templates. Not a fit. |
| **Keep the existing handwritten parser** | Parser-only; we'd still need to write an encoder. Doesn't solve the REPL or save problems. |

yyjson is the only candidate that satisfies all of: real custom allocators,
no STL, no exceptions, single-file vendor, parser + writer, MIT license.

## How yyjson plugs into our allocator policy

This is the most important section of the doc. The engine's policy
(`design/Memory allocators for third-party libraries.md`) is that **no
subsystem touches the system allocator after startup**. yyjson supports
this directly via its `yyjson_alc` interface:

```c
typedef struct yyjson_alc {
    void *(*malloc)(void *ctx, size_t size);
    void *(*realloc)(void *ctx, void *ptr, size_t old_size, size_t size);
    void (*free)(void *ctx, void *ptr);
    void *ctx;
} yyjson_alc;
```

Critically, `realloc` receives `old_size`. That matches our `Allocator`
interface exactly:

```cpp
virtual void* Realloc(void* p, size_t old_size, size_t new_size, size_t align) = 0;
```

So the adapter is trivial:

```cpp
// src/json_alc.h
namespace G {

inline yyjson_alc MakeYyjsonAlc(Allocator* arena) {
  return yyjson_alc{
    .malloc = [](void* ctx, size_t size) -> void* {
      return static_cast<Allocator*>(ctx)->Alloc(size, kMaxAlign);
    },
    .realloc = [](void* ctx, void* ptr, size_t old_size, size_t size) -> void* {
      return static_cast<Allocator*>(ctx)->Realloc(ptr, old_size, size, kMaxAlign);
    },
    .free = [](void* ctx, void* ptr) {
      // No-op for arena allocators; the arena is reset wholesale.
      // For non-arena allocators we'd thread the size through, but every
      // current call site uses an arena, so we leave free as a no-op and
      // CHECK at startup that the supplied allocator is arena-shaped.
      (void)ctx; (void)ptr;
    },
    .ctx = arena,
  };
}

}  // namespace G
```

### Per-call arenas, not a long-lived heap

yyjson does not need a persistent allocator. Each `yyjson_read_opts` /
`yyjson_mut_doc_new` call takes its own `yyjson_alc`, and the resulting
document owns memory only until the caller is done with it. This maps
perfectly onto our existing arena pattern:

| Call site | Arena |
|---|---|
| Asset packer (`packer.cc`) | The packer's existing scratch arena, reset between input files |
| `G.filesystem.load_json` | The Lua call's `frame_allocator` slot, reset on the next frame |
| `G.filesystem.save_json` | Same |
| `Save::Set` / `Save::Get` | A small dedicated `save_scratch_arena` (a few hundred KB) inside the `Save` module, reset after each call |
| REPL message read | A `request_arena` inside the REPL module, reset after each NDJSON line |
| REPL response build | Same arena as the read |

Because every parse and every emit lives inside a scope that resets an
arena, yyjson's `free` callback is a no-op. yyjson never holds memory
across the boundary that resets the arena; the document is consumed (Lua
values pushed, blob bytes copied to SQLite) before the scope ends.

### What about the long-lived case?

There is no long-lived case. yyjson is only used as a transformation step:
JSON bytes → Lua values, or Lua values → JSON bytes. The persistent
representation lives elsewhere (Lua state, SQLite blob, network buffer).
This is exactly the pattern yyjson is designed for, and it's why arena
allocators are the right answer instead of dragging mimalloc into the JSON
path.

### Sanity check: no system malloc after startup

After this migration, a `LD_PRELOAD` malloc-tracker run during gameplay
should still show zero allocations from the JSON path, just like the rest
of the engine after the mimalloc/memsys5 work landed. The arenas are
carved from the existing 4 GiB `StaticAllocator`; yyjson never sees
`malloc`.

## Hot reload interaction

yyjson is **stateless**. There is no global init function, no thread-local
state, no cached parse table, no string interning pool that survives
between calls. Every API call takes its allocator and operates on values
the caller owns.

This means hot reload requires zero special handling for yyjson itself:

1. The Lua state is torn down and recreated. Any `yyjson_doc*` that was
   transiently held during a Lua call is long since freed (its arena was
   reset). Nothing to clean up.
2. The asset packer and REPL hold no yyjson state across reload — they
   parse, consume, and discard.
3. The `Save` module holds a SQLite handle across reload (per the existing
   design), but it does *not* hold a yyjson document across reload. Each
   `Get` re-parses the blob; each `Set` re-emits.

Compare this to the alternatives we considered:

- **sheredom/json.h** would also be fine here — it's stateless too.
- **cJSON** with its global allocator hook would be a nuisance: the hook
  is set once and survives reload (which is what we want), but it also
  applies to *every* JSON call across the process, so we couldn't have
  per-call arenas without thread-local trickery.
- **A C++ library with static initializers** would hand us a hot-reload
  hazard. yyjson has none.

The only hot-reload-relevant assertion is "no `yyjson_*` value outlives
the arena that allocated it." That's a code review check, not a runtime
mechanism, and the per-call-arena pattern makes it natural to enforce.

## What changes in the engine

### Add

- **`libraries/yyjson.c`** and **`libraries/yyjson.h`** — vendored from
  upstream at a pinned tag (currently `0.10.0`). Single source + header,
  added to `add_executable(...)` in `CMakeLists.txt` next to `sqlite3.c`
  and `dr_wav.cc`.
- **`src/json_alc.h`** — the `MakeYyjsonAlc` adapter shown above. Header
  only.
- **Compile-time options**: `-DYYJSON_DISABLE_NON_STANDARD=1` to reject
  comments and trailing commas (we want strict JSON for save data),
  `-DYYJSON_DISABLE_FAST_FP_CONV=0` to keep the fast double formatter on,
  `-DYYJSON_DISABLE_UTF8_VALIDATION=0` to keep validation on (cheap and
  catches save corruption).

### Modify

- **`src/packer.cc:12`** — change `#include "json.h"` to `#include "yyjson.h"`
  and rewrite the spritesheet parser. This is the only existing caller of
  `ParseJson` in the codebase, so it's a small focused change.
- **`src/lua_filesystem.cc:37-57`** — implement `load_json` and `save_json`
  for real, against yyjson. The signatures already exist; only the bodies
  change.
- **`CMakeLists.txt`** — add `libraries/yyjson.c` to the source list, add
  the `-DYYJSON_*` flags, optionally promote `libraries/` to a system
  include directory so yyjson's header is included with `<>`.
- **`design/REPL and live interaction.md`** — already updated in this
  change to point at yyjson.
- **`design/Save and persistence.md`** — already updated in this change to
  use JSON-encoded blobs.

### Delete

- **`src/json.h`** and **`src/json.cc`** — 303 lines of handwritten parser
  with no remaining callers after `packer.cc` is migrated.
- The `JsonValue` struct in `src/json.h` and any test cases referring to
  it. Replacement tests target yyjson's API directly.

### Tests

- **`tests/test.cc`** — new tests for the yyjson allocator adapter:
  - Round-trip a small object through `MakeYyjsonAlc(&arena)` and verify
    the parse succeeds and the values are correct
  - Parse a deliberately malformed input and verify the error path
    returns cleanly (no leaks on the arena, since it gets reset anyway)
  - Confirm the writer produces strict JSON (no trailing commas, no
    comments) under `YYJSON_DISABLE_NON_STANDARD`
  - Run a 1 MB random nested document through encode/decode/encode and
    assert byte-equality on the second emit (round-trip check)
- **Asset packer regression** — re-pack the existing test spritesheet
  fixtures and diff against the pre-migration output. The asset DB blobs
  should be byte-identical because the spritesheet metadata is the same;
  only the parser implementation changed.
- **Lua tests** — `assets/test_json.lua` exercising
  `G.filesystem.save_json(t)` → `G.filesystem.load_json(...)` round trip
  for the same value matrix used by the save tests.

### CMake snippet

Approximate change:

```cmake
# Existing single-file vendored libraries
target_sources(Game PRIVATE
  libraries/sqlite3.c
  libraries/dr_wav.cc
  libraries/yyjson.c        # new
  ...
)

target_compile_definitions(Game PRIVATE
  YYJSON_DISABLE_NON_STANDARD=1
  YYJSON_DISABLE_INCR_READER=1   # we never need incremental parsing
)
```

`yyjson.c` compiles with the engine's existing warnings and sanitizer
flags without modification — it's clean C99 with no GCC/MSVC-isms.

## Migration plan

1. **Vendor yyjson** at a pinned commit. Add `libraries/yyjson.c` /
   `libraries/yyjson.h` and the CMake hookup. Build, no callers yet.
2. **Add `src/json_alc.h`** with `MakeYyjsonAlc` and the arena-allocator
   sanity check. Add the allocator unit tests in `tests/test.cc`.
3. **Port `packer.cc`** off `ParseJson` and onto yyjson. Re-run the asset
   pipeline against existing fixtures, diff the resulting `.gamedb`. This
   is the only place we replace working code, so it gets its own commit.
4. **Implement `G.filesystem.load_json` / `save_json`** in
   `lua_filesystem.cc`. Add `assets/test_json.lua`.
5. **Delete `src/json.h` and `src/json.cc`.** No callers remain.
6. **Implement `Save` against yyjson** following the
   [Save and persistence](Save%20and%20persistence.md) plan. This depends
   on steps 1–5 being done first.
7. **Implement REPL message I/O against yyjson** when the REPL design lands.

Steps 1–5 stand alone and unblock both the save and REPL designs. They can
ship in a single PR or be split — the migration is small enough either
way.

## What this does NOT do

- **Does not introduce yyjson's incremental reader.** We disable it via
  `YYJSON_DISABLE_INCR_READER` because every JSON document we parse fits
  in memory (saves are bounded, asset metadata is bounded, REPL messages
  are line-delimited and small). Disabling it shrinks the binary slightly.
- **Does not change the asset DB blob format.** The packer still emits the
  same binary blobs; only the JSON-input side changes.
- **Does not change the engine's existing arena layout.** yyjson uses
  whichever arena the call site already has.
- **Does not introduce a new global allocator.** No `mi_heap_t`, no
  `SQLITE_CONFIG_MALLOC` equivalent for JSON. The per-call arena is the
  whole story.
- **Does not expose yyjson types in the engine's public C++ API.** The
  `Save`, `Filesystem`, and REPL modules continue to exchange Lua values
  and byte slices with their callers; yyjson is a private implementation
  detail.

## Open questions

1. **Should `MakeYyjsonAlc`'s `free` callback be a strict no-op, or
   should we route it through `Allocator::Dealloc`?** Arena allocators
   ignore Dealloc, so the no-op is correct for them. If we ever wire
   yyjson to a non-arena allocator, the no-op would leak. Option: assert
   at `MakeYyjsonAlc` time that the supplied allocator is an arena
   subclass, or track the size in a side table. Lean toward the assert.

2. **Vendor at a pinned tag or vendor head?** Pinned tag (`0.10.0`) is
   safer; head pulls in fixes faster. Pin at first, bump deliberately.

3. **Should we expose yyjson's mutable document API to Lua, or only the
   value-tree API?** The mutable API is faster for code that builds JSON
   programmatically (REPL responses, save encoder). The value-tree API is
   simpler. Use both internally; expose neither to Lua — Lua sees
   `load_json` / `save_json` and that's it.

4. **Compile-time vs runtime feature flags.** yyjson has many `#define`
   knobs (`YYJSON_DISABLE_*`). Decide once at vendor time and don't make
   them configurable; the engine doesn't ship multiple builds.

5. **What about `\u` escapes in keys for the save store?** Lua table keys
   are arbitrary byte strings (not necessarily UTF-8). yyjson rejects
   non-UTF-8 keys. The save serializer should either CHECK on non-UTF-8
   keys at `set` time (preferred — catches bugs early) or escape them.
   Lean toward CHECK; document that save keys must be valid UTF-8.

## References

- [yyjson on GitHub](https://github.com/ibireme/yyjson) — source, license,
  benchmarks
- [yyjson API documentation](https://ibireme.github.io/yyjson/doc/doxygen/html/)
- [Memory allocators for third-party libraries](Memory%20allocators%20for%20third-party%20libraries.md) — the policy this doc honors
- [Save and persistence](Save%20and%20persistence.md) — primary consumer
- [REPL and live interaction](REPL%20and%20live%20interaction.md) — secondary consumer
