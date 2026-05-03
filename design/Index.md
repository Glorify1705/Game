---
tags: [index]
---

# Design Document Index

## Implemented

| Document | Tags | Summary |
|----------|------|---------|
| [Animation system](Animation%20system.md) | gameplay, animation, lua-api | Timer-based tweens, easing curves, cooldowns, and springs |
| [Asset conversion tools](Asset%20conversion%20tools.md) | assets, cli, tools | `game convert` and `game atlas` CLI commands for image/audio conversion and sprite packing |
| [CLI workflow](CLI%20workflow.md) | cli, workflow | `game init`, `run`, `package`, `stubs`, `clean` subcommands |
| [Camera system](Camera%20system.md) | camera, lua-api | 2D camera with follow, deadzone, bounds, shake, zoom, parallax |
| [Collision detection system](Collision%20detection%20system.md) | physics, collision | Box2D integration with collision callbacks and shape types |
| [CPU sampling profiler](CPU%20sampling%20profiler.md) | profiling, performance, tooling | samply-based CPU profiling in devenv (`game-samply` script) |
| [Debug logging system](Debug%20logging%20system.md) | logging, debugging | Leveled logging with channels, compile-time filtering, custom sinks |
| [Debug printing consolidation](Debug%20printing%20consolidation.md) | debugging, strings | Unified AppendToString API for type-safe string formatting |
| [Debug UI](Debug%20UI.md) | debugging, ui, tooling, renderer | ImGui-based debug overlay with 12 panels, REPL, text editor, frame breakdown |
| [Draw call optimization](Draw%20call%20optimization.md) | renderer, performance | Redundant state filtering and texture dedup to reduce draw calls |
| [Engine comparison](Engine%20comparison.md) | reference, comparison | Feature gap analysis vs Love2D, high_impact, Anchor, Carimbo, Raylib, libGDX |
| [ErrorOr and TRY macro](ErrorOr%20and%20TRY%20macro.md) | error-handling, core | Result type with TRY macro for propagating errors without exceptions |
| [File watching and hot reload](File%20watching%20and%20hot%20reload.md) | hot-reload, filesystem | Background file watcher with debounced hot-reload |
| [Filesystem API](Filesystem%20API.md) | filesystem, lua-api, json | G.filesystem (slurp/spit/stat/exists) and G.json (encode/decode) Lua APIs |
| [Font system](Font%20system.md) | renderer, fonts | SDF atlas, alignment, wrapping, outlines, kerning, ANSI escapes |
| [Game module refactor](Game%20module%20refactor.md) | core, architecture, refactor | Split game.cc into engine.h/cc, sdl_init, hot_reload; dissolved Game class |
| [Layer and canvas system](Layer%20and%20canvas%20system.md) | renderer, canvas | Off-screen render targets with blend modes and premultiplied alpha |
| [Lua API for LSP and LLMs](Lua%20API%20for%20LSP%20and%20LLMs.md) | lua, tooling, lsp | LuaLS stub generation with LuaCATS annotations and type registry |
| [Memory allocators for third-party libraries](Memory%20allocators%20for%20third-party%20libraries.md) | memory, allocators | SQLite memsys5 + mimalloc for Lua, carved from engine arena |
| [Networking](Networking.md) | networking, multiplayer | ENet reliable UDP for client/server multiplayer (PR #77) |
| [Sound stream free list](Sound%20stream%20free%20list.md) | audio, memory | Ownership-based stream slot reclamation (auto-free for fire-and-forget, managed for Lua handles) |
| [QOA audio format](QOA%20audio%20format.md) | audio, codec | QOA codec replacing OGG Vorbis for streaming audio |
| [SDL3 migration](SDL3%20migration.md) | sdl, migration, core | Full SDL2-to-SDL3 API migration: events, audio stream, window, gamepad, IO, threading |
| [SDF font rendering](SDF%20font%20rendering.md) | renderer, fonts, sdf | SDF generation, shader, caching, outline support |
| [Shader API Redesign](Shader%20API%20Redesign.md) | renderer, shaders | Silent-skip for missing uniforms, built-in g_ScreenSize/g_Time |
| [Single-file packaging](Single-file%20packaging.md) | packaging, assets | SQLite DB appended to binary with magic footer and custom VFS |
| [Stack traces on CHECK failure](Stack%20traces%20on%20CHECK%20failure.md) | debugging, logging | backward-cpp stack traces on CHECK/DCHECK failures and fatal signals |
| [Static analysis and linters](Static%20analysis%20and%20linters.md) | tooling, static-analysis | clang-tidy, clang-format, ASan/UBSan integration |
| [String library improvements](String%20library%20improvements.md) | core, strings | FixedStringBuffer, type-safe Append, path/string utilities |
| [Switching to yyjson](Switching%20to%20yyjson.md) | json, dependencies | Replaced handwritten JSON parser with yyjson, exposed via G.json |
| [Testgame1 demo improvements](Testgame1%20demo%20improvements.md) | demo, gameplay | Space Garbage! game with menus, animations, effects |
| [Thread pool and executor](Thread%20pool%20and%20executor.md) | threading, core | Executor interface with ThreadPool, Inline, and MainThread variants |
| [Timer and tween system](Timer%20and%20tween%20system.md) | gameplay, lua-api | Timers, tweens, easing functions, cooldowns, and springs |
| [Vendor all libraries](Vendor%20all%20libraries.md) | build, dependencies | All libraries vendored directly, no git submodules |
| [Particle system](Particle%20system.md) | renderer, particles, lua-api | CPU particle system with SoA layout, PropertyRamp/ColorRamp, instanced rendering, G.particles Lua API |
| [Save and persistence](Save%20and%20persistence.md) | persistence, save, lua-api | Namespaced SQLite KV store (`G.save.*`) for save data, settings; platform save dirs, JSON serialization (PR #84) |
| [Scene and state management](Scene%20and%20state%20management.md) | scenes, state, lua-api, gameplay | G.scene API with switch/push/pop, lifecycle hooks, deferred transitions |

## In Progress

| Document | Tags | Summary | Status |
|----------|------|---------|--------|
| [Cross compilation](Cross%20compilation.md) | build, cross-compilation, packaging, sfx | MinGW cross-compile Linux→Windows, osxcross for macOS | Phases 0–2 done; macOS CI done (GL 4.1 downgrade, osxcross, GitHub Actions); `--target` convenience flag and CI release builds pending |
| [REPL and live interaction](REPL%20and%20live%20interaction.md) | debugging, lua, repl | TCP REPL server for live Lua evaluation | Phase 1 (TCP server + NDJSON eval) implemented on `worktree-repl-server` branch; not yet merged |

## Partially Implemented

| Document | Tags | Summary |
|----------|------|---------|
| [Audio features](Audio%20features.md) | audio, lua-api | Pitch, looping, panning done; seek/tell, 3D audio, effects pending |
| [Bug fixes and minor improvements](Bug%20fixes%20and%20minor%20improvements.md) | bugs, code-quality, testing | ASan-confirmed leaks (Box2D alloc mismatch, Canvas __gc, renderbuffer), logic bugs (has_mouse_focus flag), defensive fixes; some code quality done, platform watchers and test coverage pending |
| [CMake and CTest improvements](CMake%20and%20CTest%20improvements.md) | build, testing, cmake, ctest | Phases 1–2 done (preset fix, ctest, timeouts, labels, parallel, test file split); coverage expansion pending (color.cc, stats.cc, xml.cc, qoa.cc) |
| [Physics system expansion](Physics%20system%20expansion.md) | physics, lua-api | Phases 1-2 done (bodies, properties, filtering, sensors, raycasting, world config, six joint types with handle API, debug draw for bodies and joints); advanced shapes (polygon, edge, chain), deferred destruction pending |
| [Profiling and tracing](Profiling%20and%20tracing.md) | profiling, performance | Chrome Tracing done; perf and pprof are external devenv tools, not engine integration |
| [Renderer improvements](Renderer%20improvements.md) | renderer, graphics | Stencil/scissor/blend/primitives done; post-processing pipeline and lighting pending |
| [Sound hot reload](Sound%20hot%20reload.md) | audio, hot-reload | File change detection works; actual reload is coarse (StopAll on any audio change), per-asset incremental reload pending |
| [Test input system](Test%20input%20system.md) | testing, input | Phase 1 done: coroutine `G.test.*` API with synthetic input injection, `--test` flag. Headless mode, record/replay, screenshot diffing pending |

## In Design

| Document | Tags | Summary |
|----------|------|---------|
| [AI utilities](AI%20utilities.md) | gameplay, ai | Behavior trees, decision trees, and AI scaffolding |
| [Asset system improvements](Asset%20system%20improvements.md) | assets, packaging | ZIP archive + SQLite index for lazy loading and modding |
| [LuaJIT Migration](LuaJIT%20Migration.md) | lua, performance | Migration from Lua 5.1 to LuaJIT with WASM fallback |
| [Module memory budgets](Module%20memory%20budgets.md) | memory, allocators, architecture | Only batch renderer overflow fix shipped; per-module sub-arenas, watermarks, and budget system not started |
| [Multiplatform support](Multiplatform%20support.md) | wasm, android, ios, portability | WASM, Android, and iOS support: shader precompiler, touch input, lifecycle events, build toolchains |

## Reference

| Document | Tags | Summary |
|----------|------|---------|
| [BYTEPATH and SNKRX porting analysis](BYTEPATH%20and%20SNKRX%20porting%20analysis.md) | reference, love2d, porting | Gap analysis of Love2D APIs needed to port two real games |
| [Potential features](Potential%20features.md) | parking-lot, ideas | Features discussed but deferred until a real use case justifies them |

## Prioritized Roadmap

Based on the [Engine comparison](Engine%20comparison.md) (six reference engines:
Love2D, high_impact, Anchor, Carimbo, Raylib, libGDX), prioritized by what makes
the engine stand out and what's needed to ship complete games.

**Engine differentiators** (things no comparison engine has): hot reload, Fennel
scripting, CLI tooling, SDF fonts, SQLite asset pipeline, type stubs for IDE,
arena memory management, vendored SDL3 source build, Linux-to-Windows
cross-compilation with SFX packaging, Debug UI with REPL, ENet networking.

### P0 — Finish what's started

Low effort, high return. Complete in-progress work to close gaps cheaply.

| Document | Rationale |
|----------|-----------|
| [Physics system expansion](Physics%20system%20expansion.md) | Phases 1-2 done (bodies, properties, filtering, sensors, raycasting, joints, debug draw). Next: advanced shapes (polygon, edge, chain) and deferred destruction. |
| [Bug fixes and minor improvements](Bug%20fixes%20and%20minor%20improvements.md) | Low-hanging fruit: error handling TODOs, platform file watchers, missing tests, allocator instrumentation. |
| [CMake and CTest improvements](CMake%20and%20CTest%20improvements.md) | Phases 1–2 done, test files split (201 tests across 9 files). Next: expand test coverage to color.cc, stats.cc, xml.cc, qoa.cc. |

### P1 — High-impact missing features

The biggest remaining gaps vs other engines. Required to ship non-trivial games.

| Feature                                             | Rationale                                                                                                                                                                                | Status |
| --------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ |
| ~~[Save and persistence](Save%20and%20persistence.md)~~ | ~~SQLite KV store, platform save dirs.~~ | **Done** (PR #84). CLI tooling and `set_bytes`/`get_bytes` pending. |
| ~~[Test input system](Test%20input%20system.md)~~   | ~~Automated testing via synthetic input.~~ | **Phase 1 done.** Headless mode, record/replay pending. |
| ~~Math utilities~~                                  | ~~Lerp, distance, angle, direction. Used in virtually every game script.~~ | **Done.** G.math now has lerp, inverse_lerp, remap, smoothstep, sign, round, distance, angle, direction, radians, degrees; vec2 has rotate, reflect, project, perpendicular. |
| Input action binding                                | Abstract action mapping (buttons to "jump"/"shoot"), rebinding, hold detection. high_impact and Anchor support this.                                                                     | Not started |

### P2 — Strengthen differentiators

Invest in what already sets the engine apart.

| Document | Rationale |
|----------|-----------|
| [Cross compilation](Cross%20compilation.md) | Phases 0–2 done, macOS CI done. Remaining: `--target` convenience flag (Phase 3) and CI release builds (Phase 4). |
| [REPL and live interaction](REPL%20and%20live%20interaction.md) | Phase 1 on branch. Extends hot-reload into live debugging — no comparison engine has this. |
| [Sound hot reload](Sound%20hot%20reload.md) | Per-asset incremental reload. Current implementation stops all sounds on any change. |

### P3 — Platform expansion

Reaching more platforms multiplies the value of everything above.

| Document | Rationale |
|----------|-----------|
| [Multiplatform support](Multiplatform%20support.md) | WASM, Android, iOS. Instant sharing via browser is the biggest platform gap (5 of 6 comparison engines ship to web). Mobile extends reach further. |

### P4 — Future

Valuable but not blocking. Build these when a specific game needs them.

| Document                                                      | Rationale                                                                                                        |
| ------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| Tilemap system                                                | Essential for platformers/RPGs. libGDX has Tiled/TMX import; high_impact has slope collision. No design doc yet. |
| Drawing primitives                                            | Ellipses, arcs, rounded rects, polygons, gradients. Raylib is the reference.                                     |
| [Module memory budgets](Module%20memory%20budgets.md)         | Per-module sub-arenas and watermarks. Only needed before targeting memory-constrained platforms (web, mobile).    |
| [AI utilities](AI%20utilities.md)                             | Only needed for games with AI agents.                                                                            |
| [LuaJIT Migration](LuaJIT%20Migration.md)                    | Performance optimization. Current Lua 5.1 is adequate for most games.                                            |
| [Asset system improvements](Asset%20system%20improvements.md) | Current SQLite system works. ZIP+index is an optimization for large games.                                       |
