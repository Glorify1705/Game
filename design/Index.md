---
tags: [index]
---

# Design Document Index

## Implemented

| Document | Tags | Summary |
|----------|------|---------|
| [Animation system](Animation%20system.md) | gameplay, animation, lua-api | Timer-based tweens, easing curves, cooldowns, and springs |
| [CLI workflow](CLI%20workflow.md) | cli, workflow | `game init`, `run`, `package`, `stubs`, `clean` subcommands |
| [Camera system](Camera%20system.md) | camera, lua-api | 2D camera with follow, deadzone, bounds, shake, zoom, parallax |
| [Collision detection system](Collision%20detection%20system.md) | physics, collision | Box2D integration with collision callbacks and shape types |
| [Debug logging system](Debug%20logging%20system.md) | logging, debugging | Leveled logging with channels, compile-time filtering, custom sinks |
| [Debug printing consolidation](Debug%20printing%20consolidation.md) | debugging, strings | Unified AppendToString API for type-safe string formatting |
| [Draw call optimization](Draw%20call%20optimization.md) | renderer, performance | Redundant state filtering and texture dedup to reduce draw calls |
| [Engine comparison](Engine%20comparison.md) | reference, comparison | Feature gap analysis vs Love2D, high_impact, Anchor |
| [ErrorOr and TRY macro](ErrorOr%20and%20TRY%20macro.md) | error-handling, core | Result type with TRY macro for propagating errors without exceptions |
| [File watching and hot reload](File%20watching%20and%20hot%20reload.md) | hot-reload, filesystem | Background file watcher with debounced hot-reload |
| [Layer and canvas system](Layer%20and%20canvas%20system.md) | renderer, canvas | Off-screen render targets with blend modes and premultiplied alpha |
| [Lua API for LSP and LLMs](Lua%20API%20for%20LSP%20and%20LLMs.md) | lua, tooling, lsp | LuaLS stub generation with LuaCATS annotations and type registry |
| [Memory allocators for third-party libraries](Memory%20allocators%20for%20third-party%20libraries.md) | memory, allocators | SQLite memsys5 + mimalloc for Lua, carved from engine arena |
| [QOA audio format](QOA%20audio%20format.md) | audio, codec | QOA codec replacing OGG Vorbis for streaming audio |
| [SDL3 migration](SDL3%20migration.md) | sdl, migration, core | Migrated from SDL2-compat to native SDL3 |
| [Shader API Redesign](Shader%20API%20Redesign.md) | renderer, shaders | Silent-skip for missing uniforms, built-in g_ScreenSize/g_Time |
| [Single-file packaging](Single-file%20packaging.md) | packaging, assets | SQLite DB appended to binary with magic footer and custom VFS |
| [Stack traces on CHECK failure](Stack%20traces%20on%20CHECK%20failure.md) | debugging, logging | backward-cpp stack traces on CHECK/DCHECK failures and fatal signals |
| [Static analysis and linters](Static%20analysis%20and%20linters.md) | tooling, static-analysis | clang-tidy, clang-format, ASan/UBSan integration |
| [String library improvements](String%20library%20improvements.md) | core, strings | FixedStringBuffer, type-safe Append, path/string utilities |
| [Testgame1 demo improvements](Testgame1%20demo%20improvements.md) | demo, gameplay | Space Garbage! game with menus, animations, effects |
| [Thread pool and executor](Thread%20pool%20and%20executor.md) | threading, core | Executor interface with ThreadPool, Inline, and MainThread variants |
| [Timer and tween system](Timer%20and%20tween%20system.md) | gameplay, lua-api | Timers, tweens, easing functions, cooldowns, and springs |
| [Font system](Font%20system.md) | renderer, fonts | SDF atlas, alignment, wrapping, outlines, kerning, ANSI escapes |
| [SDF font rendering](SDF%20font%20rendering.md) | renderer, fonts, sdf | SDF generation, shader, caching, outline support |
| [Vendor all libraries](Vendor%20all%20libraries.md) | build, dependencies | All libraries vendored directly, no git submodules |
| [CPU sampling profiler](CPU%20sampling%20profiler.md) | profiling, performance, tooling | samply-based CPU profiling in devenv (`game-samply` script) |

## Partially Implemented

| Document | Tags | Summary |
|----------|------|---------|
| [Audio features](Audio%20features.md) | audio, lua-api | Pitch, looping, panning done; seek/tell, 3D audio, effects pending |
| [Physics system expansion](Physics%20system%20expansion.md) | physics, lua-api | Basic Box2D wrapper done; joints, shapes, filtering pending |
| [Profiling and tracing](Profiling%20and%20tracing.md) | profiling, performance | Chrome Tracing done; perf and pprof integration pending |
| [Renderer improvements](Renderer%20improvements.md) | renderer, graphics | Stencil/scissor/blend done; post-processing, lighting pending |
| [Sound hot reload](Sound%20hot%20reload.md) | audio, hot-reload | Basic reload works; per-asset incremental reload pending |

## In Review

| Document | Tags | Summary |
|----------|------|---------|
| [REPL and live interaction](REPL%20and%20live%20interaction.md) | debugging, lua, repl | TCP REPL server for live Lua evaluation (PR #46) |

## In Design

| Document | Tags | Summary |
|----------|------|---------|
| [AI utilities](AI%20utilities.md) | gameplay, ai | Behavior trees, decision trees, and AI scaffolding |
| [Asset system improvements](Asset%20system%20improvements.md) | assets, packaging | ZIP archive + SQLite index for lazy loading and modding |
| [Bug fixes and minor improvements](Bug%20fixes%20and%20minor%20improvements.md) | bugs, code-quality, testing | Tracking list for small fixes, TODOs, and missing tests |
| [Game module refactor](Game%20module%20refactor.md) | core, architecture, refactor, hot-reload | Split game.cc into Engine, HotReloadManager, and SDL init |
| [LuaJIT Migration](LuaJIT%20Migration.md) | lua, performance | Migration from Lua 5.1 to LuaJIT with WASM fallback |
| [Networking](Networking.md) | networking, multiplayer | ENet reliable UDP for client/server multiplayer |
| [Particle system](Particle%20system.md) | renderer, particles, lua-api | CPU particle system with PropertyRamp and instanced rendering |
| [Save and persistence](Save%20and%20persistence.md) | persistence, save, achievements, lua-api | Namespaced SQLite KV store for save data, settings, achievements |
| [Sound stream free list](Sound%20stream%20free%20list.md) | audio, memory | Free list allocator to prevent sound slot exhaustion |
| [Test input system](Test%20input%20system.md) | testing, input | Synthetic input injection for automated testing |
| [WebAssembly and cross-platform portability](WebAssembly%20and%20cross-platform%20portability.md) | wasm, portability | Emscripten/WASM support with main loop refactoring |

## Prioritized Roadmap

Based on the [Engine comparison](Engine%20comparison.md), prioritized by what
makes the engine stand out and what's needed to ship complete games.

**Engine differentiators** (things no comparison engine has): hot reload, Fennel
scripting, CLI tooling, SDF fonts, SQLite asset pipeline, type stubs for IDE,
arena memory management. The roadmap protects and extends these advantages.

### P0 — Finish what's started

Low effort, high return. These are partially implemented — completing them
closes gaps cheaply and adds polish.

| Document | Rationale |
|----------|-----------|
| [Physics system expansion](Physics%20system%20expansion.md) | Add sensors and raycasting. These unlock trigger zones, line-of-sight, and item pickups — needed for most game genres. |
| [Profiling and tracing](Profiling%20and%20tracing.md) | Chrome Tracing done. Add perf and pprof support for CPU profiling with standard Linux tooling. |
| [Bug fixes and minor improvements](Bug%20fixes%20and%20minor%20improvements.md) | Low-hanging fruit: override keywords, error handling TODOs, Lua script bugs, missing tests. |

### P1 — High-impact features

The biggest remaining gaps from the engine comparison.

| Feature | Rationale |
|---------|-----------|
| [Save and persistence](Save%20and%20persistence.md) | Namespaced SQLite KV store for save data, settings, and achievements. Carimbo is the only comparison engine with built-in persistence — shipping any non-trivial game requires this. |
| Math utilities | Lerp (especially framerate-independent), distance, angle, direction, noise. Used in virtually every game script. |
| Text layout | Word wrap and alignment (L/C/R). Required for dialogue, UI, menus. Love2D's `printf` with alignment is the model. |

### P2 — Strengthen differentiators

Invest in what already sets the engine apart.

| Document | Rationale |
|----------|-----------|
| [REPL and live interaction](REPL%20and%20live%20interaction.md) | Already in review (PR #46). Extends the hot-reload advantage into live debugging — no other comparison engine has this. |
| [Sound hot reload](Sound%20hot%20reload.md) | Per-asset incremental reload instead of killing all sounds. Makes the hot-reload story seamless across all asset types. |
| [Sound stream free list](Sound%20stream%20free%20list.md) | Prevents slot exhaustion during rapid sound effects. Small fix, prevents a real gameplay bug. |

### P3 — Platform expansion

Reaching more platforms multiplies the value of everything above.

| Document | Rationale |
|----------|-----------|
| [WebAssembly and cross-platform portability](WebAssembly%20and%20cross-platform%20portability.md) | Instant sharing via browser. high_impact and Anchor both have this — it's the most impactful platform gap. |

### P4 — Future

Valuable but not blocking. Build these when a specific game needs them.

| Document | Rationale |
|----------|-----------|
| [Particle system](Particle%20system.md) | Visual polish. Can be prototyped in Lua first. |
| [Networking](Networking.md) | Only needed for multiplayer games. |
| [AI utilities](AI%20utilities.md) | Only needed for games with AI agents. |
| [LuaJIT Migration](LuaJIT%20Migration.md) | Performance optimization. Current Lua 5.1 is adequate for most games. |
| [Asset system improvements](Asset%20system%20improvements.md) | Current SQLite system works. ZIP+index is an optimization for large games. |
| [Test input system](Test%20input%20system.md) | Automated testing infrastructure. Important long-term, not blocking. |
