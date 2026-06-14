---
status: partially-implemented
tags: [bugs, code-quality, testing]
---

# Bug Fixes and Minor Improvements

Tracking list for small fixes and improvements that don't warrant their own
design doc. Sourced from TASKS.md, TODO comments, and codebase audit.

## Bugs

### Memory leaks (confirmed by ASan)

- [x] **Box2D allocator/free mismatch** (`src/physics.cc:80-90`). The `b2World`
  member was constructed in the member initializer list *before*
  `b2SetAllocator` was called. Fixed by moving allocator setup into a helper
  called from the initializer list.
- [x] **Canvas `__gc` missing** (`src/lua_graphics.cc:1174-1198`). Canvas
  userdata had no `__gc` metamethod; GPU resources (FBO + texture) leaked on
  GC. Fixed by adding a `__gc` entry to `kCanvasMethods`.
- [x] **Renderbuffer never deleted** (`src/renderer.cc:262, 294-305, 319-332`).
  `depth_buffer_` was never destroyed in `SetViewport()` or
  `~BatchRenderer()`. Fixed by adding `glDeleteRenderbuffers` in both places.
- [ ] **SDL TLS leak on PulseAudio thread** — ASan reports ~130 bytes leaked in
  `SDL_SetTLS_REAL` / `SDL_GetErrBuf` on the PulseAudio callback thread.
  Likely an SDL3 bug or missing cleanup for thread-local error buffers. Low
  priority; track upstream.

### Logic bugs

- [x] **`has_mouse_focus` checks wrong flag** (`src/lua_graphics.cc:1170`).
  Used `SDL_WINDOW_INPUT_FOCUS` instead of `SDL_WINDOW_MOUSE_FOCUS`. Fixed.
- [x] **Dealloc of nullptr in QOI encode path** (`src/image.cc:301-302`).
  Removed the dead `Dealloc(nullptr, size)` call.

### Defensive / robustness

- [x] **Unchecked `ftell` return** (`src/config.cc:101`). Added error checks
  for `fseek`, `ftell`, and `fread`, plus DEFER for file and buffer cleanup.
- [x] **Unchecked `fwrite` in `CopyFile`** (`src/platform.cc:192`). Added
  fwrite return check and switched to DEFER for file handles.
- [x] **Missing `DEFER` for `FILE*` in `WriteFile`** (`src/platform.cc:175`).
  Added DEFER.
- [x] **Missing `DEFER` for `FILE*` in `Profiler::Flush`**
  (`src/profiler.cc:85-129`). Added DEFER.
- [x] **`strcpy` without bounds check** (`src/input.cc:37`). Replaced with
  `snprintf`.

## Code Quality

- [x] Add `override` to `Physics::BeginContact` and `Physics::EndContact`
- [x] Add error handling to `ParseVersionFromString` in `src/config.cc`
- [x] Return errors gracefully from `LoadSprite`/`LoadSpritesheet` instead of
  CHECK-crashing (uses `ErrorOr<void>`, errors logged via `LoadFn::Load`)
- [x] Change `uint8_t*` signatures in `src/packer.cc` to use `Slice` (TODO
  removed, already addressed)
- [x] Remove dead `G2` module in `assets/testgame1.lua` (file removed)
- [x] Check arena allocation return values for null in critical paths:
  BatchRenderer command buffer, screenshot framebuffer, thread pool
  worker queue, asset loading, particle pool, image encoding, config
  loading, file reading, particle instance rendering
- [ ] Support ANSI escape codes properly in text measurement
  (`src/renderer.cc:1520`, has TODO)
- [ ] Replace fixed glyph array with hash map for Unicode support
  (`src/renderer.h:593`, has TODO)
- [x] Decouple stub generation from the Lua class (TODO removed, likely done)
- [x] Remove `-Wno-unused-parameter` from CMakeLists.txt and use
  parameter comments (`/*name*/`) where needed
- [x] Fix `object_buffers` array size mismatch in `~BatchRenderer`
  (`src/renderer.cc:320`): changed `std::array<GLuint, 4>` to
  `std::array<GLuint, 3>`

## Allocator Instrumentation

- [x] Add Valgrind annotations (`VALGRIND_MALLOCLIKE_BLOCK`,
  `VALGRIND_FREELIKE_BLOCK`, `VALGRIND_MAKE_MEM_NOACCESS`,
  `VALGRIND_MAKE_MEM_UNDEFINED`) to ArenaAllocator and BlockAllocator.
  Gated behind `__has_include(<valgrind/memcheck.h>)` — no compile flag
  needed.
- [x] Add ASan poisoning/unpoisoning (`__asan_poison_memory_region`,
  `__asan_unpoison_memory_region`) to arena and pool allocators so ASan can
  detect use-after-free within arenas and buffer overflows within pool blocks.
  (Implemented in `src/allocators.h` — `ASAN_POISON_MEMORY_REGION` /
  `ASAN_UNPOISON_MEMORY_REGION` macros used in `ArenaAllocator`,
  `PoolAllocator`, and `BlockPool`.)
- [x] Add MSan annotations (`__msan_allocated_memory`) to ArenaAllocator
  and BlockAllocator so MSan can detect reads of uninitialized arena memory.
  Gated behind `__has_feature(memory_sanitizer)`.

## Robustness

- [ ] Wrap `require()` in `assets/main.lua` with `pcall` and validate returned
  module has `init`/`update`/`draw`
- [ ] Add nil checks after C++ API calls in Lua scripts (`G.assets.sprite_info`
  in `entity.lua`, `G.sound.add_source` in `testgame1.lua`)
- [x] Add `SQLITE_BUSY` retry logic — added `sqlite3_busy_timeout(1s)` to all
  `sqlite3_open` sites (cmd_run.cc, cmd_package.cc, save.cc); changed
  `Save::Has` from `bool` to `ErrorOr<bool>` to surface transient errors

## Platform

- [ ] Implement Windows file watcher (`ReadDirectoryChangesW` + IOCP) in
  `src/file_watcher.cc:235`
- [ ] Implement macOS file watcher (`FSEvents`) in `src/file_watcher.cc:279`
- [ ] Replace `system("7z ...")` in `cmd_package.cc --sfx` with a portable
  solution. Current implementation shells out to `7z` and uses sh syntax
  (`cd && ... >/dev/null`) which only works on Linux. Options: write a custom
  SFX stub that reads zip/tar (eliminates 7z and SFX stub download
  dependencies), use a single-header zip library (e.g. miniz), or use
  `subprocess.h` for portable process spawning.

## Tests

- [ ] Add tests for `vec.h` — all vector types and operations (1305 lines,
  minimal coverage)
- [ ] Add tests for `mat.h` — all matrix types and operations (1359 lines,
  zero coverage)
- [x] Add tests for `bits.h` — `Log2`, `Align`, edge cases for `NextPow2`
  (8 tests in `test_bits.cc`)
- [x] Add tests for `stringlib.cc` — HasPrefix, HasSuffix, ConsumePrefix,
  ConsumeSuffix, PrintDouble (14 tests in `test_stringlib.cc`)
- [x] Add tests for `color.cc` — ColorFromTable lookups, error cases,
  ToFloat, static constructors (8 tests in `test_color.cc`). Also fixed
  duplicate color table entries overwriting standard red/green/blue.
- [ ] Add integration tests for asset database loading
- [ ] Add integration tests for Lua VM initialization and script loading
