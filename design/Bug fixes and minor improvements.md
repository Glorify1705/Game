---
status: in-design
tags: [bugs, code-quality, testing]
---

# Bug Fixes and Minor Improvements

Tracking list for small fixes and improvements that don't warrant their own
design doc. Sourced from TASKS.md, TODO comments, and codebase audit.

## Bugs

No known bugs at this time. Previous items were fixed or the files were removed.

## Code Quality

- [x] Add `override` to `Physics::BeginContact` and `Physics::EndContact`
- [x] Add error handling to `ParseVersionFromString` in `src/config.cc`
- [x] Return errors gracefully from `LoadSprite`/`LoadSpritesheet` instead of
  CHECK-crashing (uses `ErrorOr<void>`, errors logged via `LoadFn::Load`)
- [ ] Change `uint8_t*` signatures in `src/packer.cc` to use `Slice` (has
  TODO at line 308)
- [x] Remove dead `G2` module in `assets/testgame1.lua` (file removed)
- [ ] Check arena allocation return values for null in critical paths (e.g.
  `BatchRenderer` constructor)
- [ ] Support ANSI escape codes properly in text measurement
  (`src/renderer.cc:1436`, has TODO)
- [ ] Replace fixed glyph array with hash map for Unicode support
  (`src/renderer.h:524`, has TODO)
- [ ] Decouple stub generation from the Lua class (`src/cmd_stubs.cc:42`,
  has TODO)
- [ ] Remove `-Wno-unused-parameter` from CMakeLists.txt and use
  `[[maybe_unused]]` or parameter comments where needed

## Allocator Instrumentation

- [ ] Add Valgrind annotations (`VALGRIND_MALLOCLIKE_BLOCK`,
  `VALGRIND_FREELIKE_BLOCK`, etc.) to custom allocators in `src/allocators.h`
  so Valgrind can track arena/pool allocations in debug builds. Gate behind a
  `GAME_WITH_VALGRIND` compile flag.
- [ ] Add ASan poisoning/unpoisoning (`__asan_poison_memory_region`,
  `__asan_unpoison_memory_region`) to arena and pool allocators so ASan can
  detect use-after-free within arenas and buffer overflows within pool blocks.
- [ ] Consider MSan annotations for uninitialized memory tracking in arenas.

## Robustness

- [ ] Wrap `require()` in `assets/main.lua` with `pcall` and validate returned
  module has `init`/`update`/`draw`
- [ ] Add nil checks after C++ API calls in Lua scripts (`G.assets.sprite_info`
  in `entity.lua`, `G.sound.add_source` in `testgame1.lua`)
- [ ] Add `SQLITE_BUSY` retry logic in `src/assets.cc` and `src/config.cc`
  instead of dying on transient errors

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
- [ ] Add tests for `bits.h` — `Log2`, `Align`, edge cases for `NextPow2`
- [ ] Add tests for `stringlib.cc` — linked into test binary but has no tests
- [ ] Add tests for `color.cc` — color space conversions
- [ ] Add integration tests for asset database loading
- [ ] Add integration tests for Lua VM initialization and script loading
