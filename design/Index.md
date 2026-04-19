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

## In Progress

| Document | Tags | Summary | Status |
|----------|------|---------|--------|
| [Cross compilation](Cross%20compilation.md) | build, cross-compilation, packaging, sfx | MinGW cross-compile from Linux to Windows | Phases 0–2 done (toolchain, packaging, SFX, DLL copying); `--target` convenience flag and CI pending |
| [REPL and live interaction](REPL%20and%20live%20interaction.md) | debugging, lua, repl | TCP REPL server for live Lua evaluation | Phase 1 (TCP server + NDJSON eval) implemented on `worktree-repl-server` branch; not yet merged |

## Partially Implemented

| Document | Tags | Summary |
|----------|------|---------|
| [Audio features](Audio%20features.md) | audio, lua-api | Pitch, looping, panning done; seek/tell, 3D audio, effects pending |
| [Bug fixes and minor improvements](Bug%20fixes%20and%20minor%20improvements.md) | bugs, code-quality, testing | ASan-confirmed leaks (Box2D alloc mismatch, Canvas __gc, renderbuffer), logic bugs (has_mouse_focus flag), defensive fixes; some code quality done, platform watchers and test coverage pending |
| [CMake and CTest improvements](CMake%20and%20CTest%20improvements.md) | build, testing, cmake, ctest | Phases 1–2 done (preset fix, ctest execution, timeouts, labels, parallel); test file split and coverage expansion pending |
| [Physics system expansion](Physics%20system%20expansion.md) | physics, lua-api | Phase 1 mostly done (kinematic bodies, material properties, filtering, sensors, raycasting, per-body properties, world config); joints, advanced shapes, debug draw, deferred destruction pending |
| [Profiling and tracing](Profiling%20and%20tracing.md) | profiling, performance | Chrome Tracing done; perf and pprof are external devenv tools, not engine integration |
| [Renderer improvements](Renderer%20improvements.md) | renderer, graphics | Stencil/scissor/blend/primitives done; post-processing pipeline and lighting pending |
| [Sound hot reload](Sound%20hot%20reload.md) | audio, hot-reload | File change detection works; actual reload is coarse (StopAll on any audio change), per-asset incremental reload pending |

## In Design

| Document | Tags | Summary |
|----------|------|---------|
| [AI utilities](AI%20utilities.md) | gameplay, ai | Behavior trees, decision trees, and AI scaffolding |
| [Asset system improvements](Asset%20system%20improvements.md) | assets, packaging | ZIP archive + SQLite index for lazy loading and modding |
| [Debug UI](Debug%20UI.md) | debugging, ui, tooling | ImGui-based debug overlay with performance graphs, entity inspector, physics/collision visualization, log console |
| [LuaJIT Migration](LuaJIT%20Migration.md) | lua, performance | Migration from Lua 5.1 to LuaJIT with WASM fallback |
| [Module memory budgets](Module%20memory%20budgets.md) | memory, allocators, architecture | Only batch renderer overflow fix shipped; per-module sub-arenas, watermarks, and budget system not started |
| [Networking](Networking.md) | networking, multiplayer | ENet reliable UDP for client/server multiplayer |
| [Particle system](Particle%20system.md) | renderer, particles, lua-api | CPU particle system with PropertyRamp and instanced rendering |
| [Save and persistence](Save%20and%20persistence.md) | persistence, save, achievements, lua-api | Namespaced SQLite KV store for save data, settings, achievements |
| [Sound stream free list](Sound%20stream%20free%20list.md) | audio, memory | Free list allocator to prevent sound slot exhaustion |
| [Test input system](Test%20input%20system.md) | testing, input | Synthetic input injection for automated testing |
| [WebAssembly and cross-platform portability](WebAssembly%20and%20cross-platform%20portability.md) | wasm, portability | Emscripten/WASM support with main loop refactoring |

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
cross-compilation with SFX packaging.

### P0 — Finish what's started

Low effort, high return. Complete in-progress work to close gaps cheaply.

| Document | Rationale |
|----------|-----------|
| [Physics system expansion](Physics%20system%20expansion.md) | Phase 1 (bodies, properties, filtering, sensors, raycasting) is done. Next: joints (Phase 2) — the killer feature that justifies Box2D over the simpler collision system. |
| [Bug fixes and minor improvements](Bug%20fixes%20and%20minor%20improvements.md) | Low-hanging fruit: error handling TODOs, platform file watchers, missing tests, allocator instrumentation. |
| [CMake and CTest improvements](CMake%20and%20CTest%20improvements.md) | Phases 1–2 done. Next: split test.cc into per-subsystem files, expand test coverage to mat.h, color.cc, stats.cc, etc. |

### P1 — High-impact missing features

The biggest remaining gaps vs other engines. Required to ship non-trivial games.

| Feature                                             | Rationale                                                                                                                                                                                |
| --------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [Save and persistence](Save%20and%20persistence.md) | Can't ship any game with progression without this. SQLite KV store, platform save dirs, achievements. Only Carimbo has built-in achievements.                                            |
| [Test input system](Test%20input%20system.md)       | Automated testing via synthetic input. Raylib and libGDX both support this — strongest cross-engine signal for testability. Pairs with CI.                                               |
| Math utilities                                      | Lerp, distance, angle, direction, noise. Used in virtually every game script.                                                                                                            |
| Scene/state management                              | Scene lifecycle (init/update/draw/cleanup), deferred switching, per-scene resource cleanup. Reduces boilerplate for menu/gameplay/pause states. Carimbo's SceneManager is the reference. |
| Input action binding                                | Abstract action mapping (buttons to "jump"/"shoot"), rebinding, hold detection. high_impact and Anchor support this.                                                                     |

### P2 — Strengthen differentiators

Invest in what already sets the engine apart.

| Document | Rationale |
|----------|-----------|
| [Cross compilation](Cross%20compilation.md) | Phases 0–2 done. Remaining: `--target` convenience flag (Phase 3) and CI release builds (Phase 4). |
| [REPL and live interaction](REPL%20and%20live%20interaction.md) | Phase 1 on branch. Extends hot-reload into live debugging — no comparison engine has this. |
| [Sound hot reload](Sound%20hot%20reload.md) | Per-asset incremental reload. Current implementation stops all sounds on any change. |
| [Sound stream free list](Sound%20stream%20free%20list.md) | Prevents slot exhaustion during rapid sound effects. Small fix, real gameplay bug. |

### P3 — Platform expansion

Reaching more platforms multiplies the value of everything above.

| Document | Rationale |
|----------|-----------|
| [WebAssembly and cross-platform portability](WebAssembly%20and%20cross-platform%20portability.md) | Instant sharing via browser. 5 of 6 comparison engines ship to web — biggest remaining platform gap. |

### P4 — Future

Valuable but not blocking. Build these when a specific game needs them.

| Document                                                      | Rationale                                                                                                        |
| ------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| [Particle system](Particle%20system.md)                       | Visual polish. Can be prototyped in Lua first.                                                                   |
| Tilemap system                                                | Essential for platformers/RPGs. libGDX has Tiled/TMX import; high_impact has slope collision. No design doc yet. |
| Drawing primitives                                            | Ellipses, arcs, rounded rects, polygons, gradients. Raylib is the reference.                                     |
| [Module memory budgets](Module%20memory%20budgets.md)         | Per-module sub-arenas and watermarks. Only needed before targeting memory-constrained platforms (web, mobile).    |
| [Networking](Networking.md)                                   | Only needed for multiplayer games.                                                                               |
| [AI utilities](AI%20utilities.md)                             | Only needed for games with AI agents.                                                                            |
| [LuaJIT Migration](LuaJIT%20Migration.md)                    | Performance optimization. Current Lua 5.1 is adequate for most games.                                            |
| [Asset system improvements](Asset%20system%20improvements.md) | Current SQLite system works. ZIP+index is an optimization for large games.                                       |
