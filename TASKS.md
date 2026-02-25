# Tasks

## Bugs

- [ ] Fix infinite recursion in `assets/vector2d.lua:39` — `Vec2.distance` calls itself instead of `Vec2.distance2`
- [ ] Fix `assets/random.lua:7` — uses deprecated Lua 5.0 `arg` table; use explicit parameter instead
- [ ] Fix `assets/random.lua:13` — `Random.non_deterministic()` references `self` but is not a method (missing colon)
- [ ] Fix missing return in `src/sound.cc:143` — `StartChannel` falls off end without returning when source type is unrecognized
- [ ] Fix global variables in `assets/player.lua:4-5` — `FORCE` and `ANGLE_DELTA` pollute global namespace, should be `local`

## Code quality

- [ ] Add `override` to `Physics::BeginContact` and `Physics::EndContact` in `src/physics.h:36-37`
- [ ] Add error handling to `ParseVersionFromString` in `src/config.cc:18` (has TODO)
- [ ] Add error handling to `LoadImageFromMemory` in `src/renderer.cc:614` (has TODO)
- [ ] Add error handling to `LoadFontFromMemory` in `src/renderer.cc:633` (has TODO)
- [ ] Add `glCheckFramebufferStatus` validation after framebuffer creation in `src/renderer.cc`
- [ ] Change `uint8_t*` signatures in `src/packer.cc:179` (has TODO)
- [ ] Remove dead `#if 0` block in `src/input.cc:106-111` or restore with text file loader
- [ ] Remove dead `G2` module in `assets/testgame1.lua:102-108`
- [ ] Check arena allocation return values for null in critical paths (e.g. `BatchRenderer` constructor)

## Robustness

- [ ] Wrap `require()` in `assets/main.lua` with `pcall` and validate returned module has `init`/`update`/`draw`
- [ ] Add nil checks after C++ API calls in Lua scripts (`G.assets.sprite_info` in `entity.lua`, `G.sound.add_source` in `testgame1.lua`)
- [ ] Add `SQLITE_BUSY` retry logic in `src/assets.cc` and `src/config.cc` instead of dying on transient errors
- [ ] Warn when obtained SDL audio spec differs from requested spec in `src/game.cc:655`

## Build

- [X] ~~Fix CMake compatibility error with xxHash library~~ (replaced xxHash with rapidhash)
- [ ] Add `-fsanitize=undefined` (UBSan) alongside AddressSanitizer in test builds
- [ ] Remove `-Wno-unused-parameter` and use `[[maybe_unused]]` where needed
- [ ] Enable AddressSanitizer for dev builds, not just tests
- [ ] Port inotify file watcher to cross-platform (`src/game.cc:124` has TODO)

## Tests

- [ ] Add tests for `vec.h` — all vector types and operations (1305 lines, minimal coverage)
- [ ] Add tests for `mat.h` — all matrix types and operations (1359 lines, zero coverage)
- [x] Add tests for `circular_buffer.h` — push/pop, wraparound, capacity
- [ ] Add tests for `bits.h` — `Log2`, `Align`, edge cases for `NextPow2`
- [ ] Add tests for `stringlib.cc` — linked into test binary but has no tests
- [ ] Add tests for `color.cc` — color space conversions
- [ ] Add integration tests for asset database loading
- [ ] Add integration tests for Lua VM initialization and script loading
