# Tasks

## Bugs

- [ ] Fix infinite recursion in `assets/vector2d.lua:39` — `Vec2.distance` calls itself instead of `Vec2.distance2`
- [ ] Fix `assets/random.lua:7` — uses deprecated Lua 5.0 `arg` table; use explicit parameter instead
- [ ] Fix `assets/random.lua:13` — `Random.non_deterministic()` references `self` but is not a method (missing colon)
- [x] Fix missing return in `src/sound.cc:143` — `StartChannel` falls off end without returning when source type is unrecognized
- [ ] Fix global variables in `assets/player.lua:4-5` — `FORCE` and `ANGLE_DELTA` pollute global namespace, should be `local`

## Code quality

- [ ] Add `override` to `Physics::BeginContact` and `Physics::EndContact` in `src/physics.h:36-37`
- [ ] Add error handling to `ParseVersionFromString` in `src/config.cc:18` (has TODO)
- [ ] Add error handling to `LoadImageFromMemory` in `src/renderer.cc:614` (has TODO)
- [ ] Add error handling to `LoadFontFromMemory` in `src/renderer.cc:633` (has TODO)
- [x] Add `glCheckFramebufferStatus` validation after framebuffer creation in `src/renderer.cc`
- [ ] Change `uint8_t*` signatures in `src/packer.cc:179` (has TODO)
- [ ] Remove dead `#if 0` block in `src/input.cc:106-111` or restore with text file loader
- [ ] Remove dead `G2` module in `assets/testgame1.lua:102-108`
- [ ] Check arena allocation return values for null in critical paths (e.g. `BatchRenderer` constructor)

## Robustness

- [ ] Wrap `require()` in `assets/main.lua` with `pcall` and validate returned module has `init`/`update`/`draw`
- [ ] Add nil checks after C++ API calls in Lua scripts (`G.assets.sprite_info` in `entity.lua`, `G.sound.add_source` in `testgame1.lua`)
- [ ] Add `SQLITE_BUSY` retry logic in `src/assets.cc` and `src/config.cc` instead of dying on transient errors
- [x] Warn when obtained SDL audio spec differs from requested spec in `src/game.cc:655`

## Build

- [X] ~~Fix CMake compatibility error with xxHash library~~ (replaced xxHash with rapidhash)
- [ ] Add `-fsanitize=undefined` (UBSan) alongside AddressSanitizer in test builds
- [x] Remove `-Wno-unused-parameter` and use `[[maybe_unused]]` where needed
- [ ] Enable AddressSanitizer for dev builds, not just tests
- [ ] Port inotify file watcher to cross-platform (`src/game.cc:124` has TODO)

## Lua API for LSP and LLMs

See `design/Lua API for LSP and LLMs.md` for full design.

- [x] Migrate all Lua libraries to `LuaApiFunction` registration
  - Switch `input`, `math`, `physics`, `system`, `clock`, `filesystem`, `data`, `assets`, `random` from bare `luaL_Reg` to `LuaApiFunction` with docstrings and argument names/descriptions
  - `graphics` and `sound` already use `LuaApiFunction`
- [x] Add type annotations to `LuaApiFunction` args and returns
  - Add a `type` field to `LuaApiFunctionArg` (e.g. `"number"`, `"string"`, `"vec2"`, `"boolean"`, `"physics_handle"`, `"rng"`)
  - Add typed return annotations to `LuaApiFunction`
  - Update all libraries to include type info
  - Requires changes to structs in `src/lua.h` and all `lua_*.cc` files
- [x] Auto-generate LuaLS stub file from `LuaApiFunction` metadata
  - Add a command that iterates all registered `LuaApiFunction` arrays and emits `definitions/game.lua` with LuaCATS annotations
  - Generate `.luarc.json` for the assets directory so LuaLS picks up stubs automatically
  - Handle overloads (e.g. `G.random.sample` with 1 or 3 args)
- [x] Design and implement type registration API for userdata metatables
  - Design a C++ API in `src/lua.h` / `src/lua.cc` for describing userdata types (vec2, vec3, vec4, mat2x2, mat3x3, mat4x4, byte_buffer, physics_handle, rng, sprite_asset)
  - Capture: metatable name, LuaLS type alias, fields (name + type), methods (name + params + returns), metamethods/operators, constructors (which `G.*` functions return this type)
  - Extend the stub generator to emit `---@class`, `---@field`, `---@operator`, and method annotations from this registry

## Performance

- [ ] Integrate https://github.com/wolfpld/tracy.

## Tests

- [ ] Add tests for `vec.h` — all vector types and operations (1305 lines, minimal coverage)
- [ ] Add tests for `mat.h` — all matrix types and operations (1359 lines, zero coverage)
- [x] Add tests for `circular_buffer.h` — push/pop, wraparound, capacity
- [ ] Add tests for `bits.h` — `Log2`, `Align`, edge cases for `NextPow2`
- [ ] Add tests for `stringlib.cc` — linked into test binary but has no tests
- [ ] Add tests for `color.cc` — color space conversions
- [ ] Add integration tests for asset database loading
- [ ] Add integration tests for Lua VM initialization and script loading
