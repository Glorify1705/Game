# Tasks

## Bugs

- [ ] 1. Fix infinite recursion in `assets/vector2d.lua:39` — `Vec2.distance` calls itself instead of `Vec2.distance2`
- [ ] 2. Fix `assets/random.lua:7` — uses deprecated Lua 5.0 `arg` table; use explicit parameter instead
- [ ] 3. Fix `assets/random.lua:13` — `Random.non_deterministic()` references `self` but is not a method (missing colon)
- [ ] 4. Fix missing return in `src/sound.cc:143` — `StartChannel` falls off end without returning when source type is unrecognized
- [ ] 5. Fix global variables in `assets/player.lua:4-5` — `FORCE` and `ANGLE_DELTA` pollute global namespace, should be `local`

## Code quality

- [ ] 6. Add `override` to `Physics::BeginContact` and `Physics::EndContact` in `src/physics.h:36-37`
- [ ] 7. Add error handling to `ParseVersionFromString` in `src/config.cc:18` (has TODO)
- [ ] 8. Add error handling to `LoadImageFromMemory` in `src/renderer.cc:614` (has TODO)
- [ ] 9. Add error handling to `LoadFontFromMemory` in `src/renderer.cc:633` (has TODO)
- [ ] 10. Add `glCheckFramebufferStatus` validation after framebuffer creation in `src/renderer.cc`
- [ ] 11. Change `uint8_t*` signatures in `src/packer.cc:179` (has TODO)
- [ ] 12. Remove dead `#if 0` block in `src/input.cc:106-111` or restore with text file loader
- [ ] 13. Remove dead `G2` module in `assets/testgame1.lua:102-108`
- [ ] 14. Check arena allocation return values for null in critical paths (e.g. `BatchRenderer` constructor)

## Robustness

- [ ] 15. Wrap `require()` in `assets/main.lua` with `pcall` and validate returned module has `init`/`update`/`draw`
- [ ] 16. Add nil checks after C++ API calls in Lua scripts (`G.assets.sprite_info` in `entity.lua`, `G.sound.add_source` in `testgame1.lua`)
- [ ] 17. Add `SQLITE_BUSY` retry logic in `src/assets.cc` and `src/config.cc` instead of dying on transient errors
- [ ] 18. Warn when obtained SDL audio spec differs from requested spec in `src/game.cc:655`

## Build

- [X] ~~19. Fix CMake compatibility error with xxHash library~~ (replaced xxHash with rapidhash)
- [ ] 20. Add `-fsanitize=undefined` (UBSan) alongside AddressSanitizer in test builds
- [ ] 21. Remove `-Wno-unused-parameter` and use `[[maybe_unused]]` where needed
- [ ] 22. Enable AddressSanitizer for dev builds, not just tests
- [ ] 23. Port inotify file watcher to cross-platform (`src/game.cc:124` has TODO)

## Tests

- [x] 24. Add tests for `vec.h` — all vector types and operations (1305 lines, minimal coverage)
- [ ] 25. Add tests for `mat.h` — all matrix types and operations (1359 lines, zero coverage)
- [ ] 26. Add tests for `circular_buffer.h` — push/pop, wraparound, capacity
- [ ] 27. Add tests for `bits.h` — `Log2`, `Align`, edge cases for `NextPow2`
- [ ] 28. Add tests for `stringlib.cc` — linked into test binary but has no tests
- [ ] 29. Add tests for `color.cc` — color space conversions
- [ ] 30. Add integration tests for asset database loading
- [ ] 31. Add integration tests for Lua VM initialization and script loading
