---
status: in-design
tags: [wasm, portability]
---

# WebAssembly and Cross-Platform Portability

## Motivation

The engine currently targets Linux (primary) with stubbed Windows and macOS support. Both high_impact and Anchor — our closest comparison engines — ship to Windows, macOS, Linux, and Web (WASM). Web/WASM is listed as a Tier 3 gap in the engine comparison doc, but it's arguably the most impactful platform gap: it enables instant sharing, zero-install playtesting, and distribution on itch.io as a browser game. This document explores what it would take to make the engine portable to WASM and other platforms, and how other 2D engines have solved the same problem.

## How other engines handle it

### Sokol (most architecturally relevant)

Sokol is a set of single-file C headers for cross-platform development. WASM was a primary design motivation.

**Architecture**: Three layers — `sokol_app.h` (platform), `sokol_gfx.h` (graphics), `sokol_glue.h` (bridge). The graphics layer has compile-time backend selection via preprocessor defines:

| Define | Backend |
|---|---|
| `SOKOL_GLCORE` | Desktop OpenGL 4.1+ |
| `SOKOL_GLES3` | OpenGL ES 3.0 (mobile/web) |
| `SOKOL_D3D11` | Direct3D 11 (Windows) |
| `SOKOL_METAL` | Metal (macOS/iOS) |
| `SOKOL_WGPU` | WebGPU |

Application code is identical across all targets — only build flags change. A build-time shader cross-compiler (`sokol-shdc`) converts annotated GLSL into platform-specific shader code (GLSL, HLSL, MSL, SPIR-V, WGSL).

**Key insight**: Define a narrow, low-level graphics API that maps naturally onto both desktop OpenGL and WebGL, with compile-time backend selection and build-time shader translation.

### Raylib

First-class WASM support via Emscripten. Build steps:

1. Compile each module with `emcc -Os -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2`.
2. Link with `--use-port=sdl2` (or GLFW) and `--preload-file assets/`.
3. Output: `.html` + `.js` + `.wasm` + `.data`.

Two main loop strategies: refactor into a `UpdateDrawFrame()` callback (recommended, zero overhead), or use `-sASYNCIFY` to keep the standard while-loop (simpler but ~50% code size overhead). Uses OpenGL ES 2.0 / WebGL 1 for maximum compatibility. CMake integration via Emscripten's toolchain file.

### high_impact (direct comparison target)

Uses Sokol under the hood (sokol_app, sokol_gfx, sokol_audio). This gives it free WASM support — the same code compiles to native and web. Renders via OpenGL or WebGL 2 depending on target. Audio uses QOA format, decoded at runtime.

### Anchor (direct comparison target)

Uses SDL2 + OpenGL 3.3 / WebGL 2. Compiles to WASM via Emscripten with `--use-port=sdl2`. Uses miniaudio for sound. The codebase has `#ifdef __EMSCRIPTEN__` guards for main loop refactoring and asset loading differences.

### Godot

Most mature HTML5/WASM export among open-source engines. Key decisions:

- **Single-threaded by default** (as of 4.3): avoids `SharedArrayBuffer` COOP/COEP header requirement, which causes compatibility issues with many hosting platforms (including itch.io).
- **Direct Web Audio API calls** instead of Emscripten's SDL audio wrapper — lower latency.
- **WASM SIMD** for performance-critical paths (~1.5-2x gains).
- Build size: a stripped Godot 2D web export compresses to ~2.4 MB with Brotli.

### Love2D

No official WASM support. Community ports (love.js, love-web-builder) compile the C++ engine via Emscripten but must fall back to standard Lua 5.1 because **LuaJIT cannot compile to WASM** — its core VM is hand-tuned assembly. This is directly relevant to our `LuaJIT Migration.md` — if we migrate to LuaJIT for desktop, we need a Lua 5.1 fallback path for WASM.

### Macroquad/miniquad (Rust)

Gold standard for "WASM-first" design. Notably, miniquad does **not use Emscripten** — it compiles Rust to `wasm32-unknown-unknown` and uses a small JavaScript loader (`gl.js`) that populates the WASM import table with WebGL calls. This produces tiny output (no Emscripten runtime bloat). Not directly applicable to our C++ codebase, but the architecture is instructive: minimize the JS-WASM boundary to primitive types and buffer pointers.

### Bevy (Rust)

Uses `wasm-bindgen` for JS interop and `wgpu` (WebGPU) for rendering in the browser. Its parallel ECS falls back to single-threaded on WASM. Uses Trunk as a build tool for WASM targets.

## Current engine audit

### What ports cleanly

| Component | Status | Notes |
|---|---|---|
| C++17 | Fully supported | Emscripten uses Clang; C++17 has zero issues |
| `-fno-exceptions -fno-rtti` | Ideal for WASM | Reduces output by ~15% (Box2D benchmarks) |
| SDL2 | Emscripten port available | `--use-port=sdl2` handles everything |
| Lua 5.1 | Works via Emscripten | Pure C, compiles cleanly |
| Box2D | Works via Emscripten | Pure C, no platform deps |
| SQLite3 | Works via Emscripten | Frequently compiled to WASM; well-tested path |
| mimalloc | Supported | Emscripten has native support via `-sMALLOC=mimalloc` |
| stb libs | Work via Emscripten | Single-file C headers, no platform deps |
| pugixml | Works via Emscripten | Pure C++ |
| double-conversion | Works via Emscripten | Pure C++ |
| nlohmann/json | Works via Emscripten | Header-only C++ |

### What needs adaptation

#### 1. Main loop (blocking)

The current game loop in `game.cc:506-557` is a blocking `for(;;)` loop:

```cpp
void Run() {
    SDL_PauseAudioDevice(audio_device_, 0);
    double last_frame = NowInSeconds();
    // ...
    for (;;) {
        // poll events, update, render
        SDL_GL_SwapWindow(window_);
    }
}
```

Browsers use cooperative multitasking — an infinite loop freezes the tab. This must be refactored into a single-frame step function called via `emscripten_set_main_loop`.

**Approach**: Extract the loop body into a `StepFrame()` method on `Game`, then:

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
static Game* g_game = nullptr;
static void EmscriptenMainLoop() { g_game->StepFrame(); }
void Game::Run() {
    g_game = this;
    emscripten_set_main_loop(EmscriptenMainLoop, 0, 1);
}
#else
void Game::Run() {
    for (;;) {
        if (!StepFrame()) return;
    }
}
#endif
```

The state variables (`last_frame`, `t`, `accum`) move from local variables to `Game` members. This is a clean refactor that improves the code even for native targets.

#### 2. OpenGL 4.6 shaders (blocking)

All engine shaders use `#version 460 core` (`shaders.cc:9,51,67,81,113,136`). The OpenGL context requests version 4.6 (`game.cc:692-694`). WebGL 2 corresponds to OpenGL ES 3.0 — `#version 300 es`.

**Key differences**:

| Feature | OpenGL 4.6 core | OpenGL ES 3.0 / WebGL 2 |
|---|---|---|
| Shader version | `#version 460 core` | `#version 300 es` |
| Precision qualifiers | Optional | Required for `float` in fragment shaders |
| `layout(location=N)` | Yes | Yes |
| `in`/`out` qualifiers | Yes | Yes |
| `texture()` function | Yes | Yes |
| Geometry shaders | Yes | No |
| Compute shaders | Yes | No |
| `glLineWidth` | Works | Ignored by browsers |
| Debug output | Yes (`GL_KHR_debug`) | No |
| MSAA | Yes | Yes (WebGL 2) |

Our shaders use only basic features (vertex transforms, texture sampling, per-vertex color, SDF font rendering) that are fully available in OpenGL ES 3.0. No geometry shaders, no compute, no tessellation.

**Approach**: Maintain two shader versions — the existing `#version 460 core` for desktop and `#version 300 es` equivalents for WASM. The differences are small:

```glsl
// Desktop (current)
#version 460 core
out vec4 frag_color;
// ...

// WASM/ES
#version 300 es
precision mediump float;
out vec4 frag_color;
// ...
```

Options (in order of preference):

1. **Preprocessor selection at compile time**: Define shaders with a `SHADER_VERSION_HEADER` macro that expands to the right version string and precision qualifiers. Simplest, zero runtime cost.

2. **Build-time shader cross-compilation** (sokol-shdc approach): Write shaders in annotated GLSL, cross-compile to GLSL 460, GLSL ES 300, and potentially HLSL/MSL. More infrastructure but enables future Metal/Vulkan backends.

3. **Drop to OpenGL 3.3 + ES 3.0 common subset everywhere**: `#version 330 core` on desktop, `#version 300 es` on WASM. Since our shaders don't use any 4.6-specific features, this is feasible but loses debug output support on desktop.

User-supplied custom shaders (loaded from `.vert`/`.frag` asset files) present an additional challenge — either document that game developers must provide both versions, or provide a simple translation pass (version line replacement + precision qualifier injection).

#### 3. GLAD loader (blocking)

GLAD (`libraries/glad.cc`) loads desktop OpenGL function pointers. On Emscripten, OpenGL ES functions are provided directly — GLAD is unnecessary and won't work.

**Approach**: Conditionally exclude GLAD on Emscripten builds:

```cpp
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include "libraries/glad.h"
#endif
```

The GLAD loader call in `CreateOpenglContext` (`gladLoadGLLoader`) is skipped on Emscripten. GLAD feature-detection macros (`GLAD_GL_VERSION_4_3`, `GLAD_GL_KHR_debug`) need `#ifndef __EMSCRIPTEN__` guards.

#### 4. Memory budget (important)

The engine allocates a 4 GB arena (`game.cc:54`):

```cpp
constexpr size_t kEngineMemory = Gigabytes(4);
```

32-bit WASM has a 4 GB theoretical address space limit. In practice, browsers cap usable memory lower — many mobile browsers fail above 1 GB. Simple 2D games on itch.io often run with wasteful 1.5 GB WASM heaps.

**Approach**: Reduce the WASM memory budget significantly:

```cpp
#ifdef __EMSCRIPTEN__
constexpr size_t kEngineMemory = Megabytes(256);
constexpr size_t kHotReloadMemory = 0;  // No hot-reload on web
#else
constexpr size_t kEngineMemory = Gigabytes(4);
constexpr size_t kHotReloadMemory = Megabytes(128);
#endif
```

Use `-sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH` in the Emscripten link flags. 256 MB should be generous for a 2D game; profile and adjust.

#### 5. PhysFS (moderate)

PhysFS is used for virtual filesystem operations (`filesystem.h`/`filesystem.cc`). It works on Emscripten in principle (it's pure C with POSIX fallbacks), but the virtual filesystem needs backing storage.

**Approach**: For WASM, PhysFS mounts would target Emscripten's MEMFS (populated by `--preload-file`). The PhysFS write directory would map to IDBFS for save-game persistence. This may require minor init changes but PhysFS itself should compile.

#### 6. File watching / hot reload (trivial to disable)

The `Filewatcher` class uses Linux inotify (`game.cc:108-148`). Already has an `#ifdef _WIN32` stub for Windows. Just add `#ifdef __EMSCRIPTEN__` to produce another empty stub. Hot reload makes no sense in a packaged web build.

The background thread for `CheckChangedFiles` (`game.cc:195-220`) should also be disabled — no source directory to watch, and threading adds COOP/COEP hosting requirements.

#### 7. Threading (moderate)

The `ThreadPool` (`thread_pool.h`) uses `SDL_Thread`/`SDL_Mutex`/`SDL_Cond`. On Emscripten, threading requires `SharedArrayBuffer` which requires the server to send:

```
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Opener-Policy: same-origin
```

Many hosting services (itch.io, GitHub Pages) don't set these headers. Godot defaults to single-threaded web exports for this reason.

**Approach**: Disable the thread pool on WASM. The only current use is the background file-watcher thread, which is disabled anyway. If future features need async work (asset streaming), use Emscripten's async fetch API instead of threads.

#### 8. Platform-specific code (`platform.cc`)

Already has `#ifdef _WIN32` / `#elif __APPLE__` / POSIX branches. Emscripten provides a POSIX-like environment, so the Linux/POSIX paths should mostly work. `GetExePath` and `GetExeDir` don't apply to WASM — return fixed paths. `GetUserCacheDir` would point to an IDBFS mount for persistence.

#### 9. Audio latency (minor)

The engine uses SDL2 audio callbacks (`game.cc:623-634`) with a 256-sample buffer at 44100 Hz. This works through Emscripten's SDL2 port but has known latency issues — audio processing runs on the main thread in the browser, and the Web Audio API has a minimum 128-sample buffer.

**Short-term**: Accept slightly higher latency on web. The SDL2 callback path works.

**Long-term**: Consider a direct Web Audio API backend (like Godot 4.3) or OpenAL (Emscripten provides a full OpenAL 1.1 emulation backed by Web Audio API). OpenAL has better latency characteristics than SDL2 audio on the web.

#### 10. `glLineWidth` (trivial)

Used in `renderer.cc`. Browsers ignore `glLineWidth` — all lines render at 1px. This is a WebGL specification limitation. Document it as a known difference. If thick lines are needed, switch to quad-based line rendering (already the approach for high-quality line rendering on desktop too).

### What doesn't apply to WASM

| Component | Reason |
|---|---|
| Hot reload | No source directory in packaged web build |
| Thread pool | COOP/COEP headers not widely available; only used for file watching |
| Debug output (`GL_KHR_debug`) | Not available in WebGL |
| MSAA configuration | WebGL 2 supports MSAA, but sample count selection differs |
| `SDL_ShowSimpleMessageBox` | No native message boxes in browser — use console/overlay |
| X11 window hints | Not applicable |

## Build system integration

### CMake with Emscripten toolchain

Emscripten provides `Emscripten.cmake` as a CMake toolchain file. The build invocation:

```bash
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```

CMakeLists.txt changes:

```cmake
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # SDL2 is provided as an Emscripten port
    # No need to find_package(SDL2) or find_package(OpenGL)

    target_compile_options(Game PRIVATE
        -sUSE_SDL=2
        -pthread  # only if threading is enabled
    )

    target_link_options(Game PRIVATE
        -sUSE_SDL=2
        -sMAX_WEBGL_VERSION=2
        -sMIN_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH
        -sINITIAL_MEMORY=67108864
        -sMALLOC=mimalloc
        -sENVIRONMENT=web
        --preload-file ${CMAKE_BINARY_DIR}/assets.sqlite3@/assets.sqlite3
        --shell-file ${PROJECT_SOURCE_DIR}/web/shell.html
    )

    # Exclude GLAD (GL functions provided directly)
    # Exclude platform-specific sources that don't apply
    # Exclude thread pool (or compile as no-op stubs)
else()
    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    # ... existing desktop build
endif()
```

### Output files

An Emscripten build produces:

| File | Purpose | Typical size (2D game) |
|---|---|---|
| `game.html` | Page shell (customizable) | 1-2 KB |
| `game.js` | JS glue code (loads WASM, initializes) | 50-200 KB |
| `game.wasm` | Compiled engine + game logic | 2-8 MB |
| `game.data` | Preloaded assets (SQLite DB) | Varies |

### Size optimization

Flags to minimize output size:

| Flag | Effect |
|---|---|
| `-Os` | Optimize for size |
| `-flto` | Link-time optimization |
| `--closure 1` | Closure Compiler on JS glue |
| `-fno-exceptions -fno-rtti` | Already used; saves ~15% |
| `-sENVIRONMENT=web` | Strip non-web runtime code |
| `-sEVAL_CTORS` | Evaluate constructors at compile time |

Post-link: run `wasm-opt -Oz game.wasm -o game.wasm` (from Binaryen).

**Compression** is the single biggest win. WASM compresses extremely well:

| Method | Typical reduction |
|---|---|
| gzip | ~75% |
| Brotli | ~85-90% |

A 5 MB `.wasm` file compresses to ~600 KB with Brotli. Most web servers can serve `.br` files. For itch.io, the platform handles compression automatically.

## Asset loading on WASM

### Current pipeline

Source files (PNG, WAV, OGG, Lua, etc.) are packed into a SQLite database (`assets.sqlite3`) at build time. At runtime, the engine opens the database and loads assets on demand.

### WASM approach

1. **Pack assets at build time** (unchanged): `game package` produces `assets.sqlite3`.
2. **Bundle via `--preload-file`**: Emscripten downloads the SQLite file and mounts it in MEMFS before `main()` runs.
3. **SQLite reads from MEMFS**: SQLite's file I/O uses POSIX calls, which Emscripten maps to MEMFS. This works transparently.
4. **Save data via IDBFS**: For persistent state (save files, preferences), mount an IDBFS directory and sync periodically.

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// After writing save data:
EM_ASM(
    FS.syncfs(false, function(err) {
        if (err) console.error('syncfs error:', err);
    });
);
#endif
```

### Startup experience

The `.data` file (asset bundle) must download before the game starts. For a typical 2D indie game with modest assets (5-20 MB), this takes 1-3 seconds on a decent connection. Emscripten provides progress callbacks for a loading bar. The custom shell HTML (`--shell-file`) can display a splash screen during download.

## LuaJIT implications

The `LuaJIT Migration.md` design doc proposes migrating from Lua 5.1 to LuaJIT for performance. **LuaJIT cannot compile to WASM** because its core is hand-tuned assembly (x86, ARM, MIPS, PPC). All engines that use LuaJIT on desktop (Love2D, Defold) fall back to standard Lua for their WASM builds.

**Options**:

1. **Stay on Lua 5.1**: No issue. Lua 5.1 compiles to WASM cleanly.
2. **Migrate to LuaJIT with Lua 5.1 fallback**: Use LuaJIT on desktop, Lua 5.1 on WASM. LuaJIT is API-compatible with Lua 5.1 (with minor extensions). The engine already targets Lua 5.1, so this means keeping the current Lua 5.1 source alongside LuaJIT and selecting at build time. The main risk is accidentally depending on LuaJIT extensions (FFI, `bit` library, `jit` library) in game scripts that then fail on WASM.
3. **Use Lua 5.1 everywhere**: Simpler. Performance difference matters less for a scripting layer in a 2D game.

## Cross-platform distribution strategy

### Build matrix

| Target | Toolchain | Output | Distribution |
|---|---|---|---|
| Linux | GCC/Clang | ELF binary + assets | itch.io, AppImage, tarball |
| Windows | MSVC or MinGW | `.exe` + DLLs + assets | itch.io, Steam, zip |
| macOS | Clang (Xcode) | `.app` bundle + assets | itch.io, Steam, DMG |
| Web | Emscripten | `.html` + `.js` + `.wasm` + `.data` | itch.io (browser), GitHub Pages |

### CI/CD (GitHub Actions)

```yaml
strategy:
  matrix:
    include:
      - os: ubuntu-latest
        target: linux
      - os: windows-latest
        target: windows
      - os: macos-latest
        target: macos
      - os: ubuntu-latest
        target: web  # Emscripten runs on Linux
```

For the web target, use the `mymindstorm/setup-emsdk` GitHub Action. Each target produces a zip artifact. The web build zip contains `index.html` + `.js` + `.wasm` + `.data`, ready for itch.io upload.

### itch.io pattern

Most indie developers use:
- **Web build on itch.io** for free demos / instant play (zero friction, widest reach).
- **Desktop builds on itch.io or Steam** for the paid/full release.
- The web build doubles as marketing — players try instantly, then buy the desktop version.

itch.io's `butler` CLI tool automates uploads from CI.

## Implementation plan

### Phase 1: Refactor for portability (no new dependencies)

These changes improve the codebase for all platforms, not just WASM.

1. **Extract `StepFrame()`**: Move the game loop body from `Game::Run()` into `Game::StepFrame()`. Move `last_frame`, `t`, `accum` to `Game` members. The `for(;;)` loop just calls `StepFrame()`. This is a pure refactor with no behavior change.

2. **Shader version abstraction**: Add a compile-time shader header selection. Define shaders with a substitutable version/precision prefix. Verify all shaders compile under `#version 300 es` with `precision mediump float` (they should — we use no desktop-only features).

3. **Conditional GLAD**: Wrap GLAD includes and the loader call behind `#ifndef __EMSCRIPTEN__`. On Emscripten, include `<GLES3/gl3.h>` instead.

4. **Conditional Filewatcher**: Add `#ifdef __EMSCRIPTEN__` empty stub alongside the existing `#ifdef _WIN32` stub.

5. **Conditional thread pool usage**: Guard the file-watcher thread launch behind `#ifndef __EMSCRIPTEN__`.

6. **Configurable memory budget**: Make `kEngineMemory` a compile-time platform-dependent constant.

### Phase 2: Emscripten build integration

7. **Add Emscripten CMake path**: Add `if(EMSCRIPTEN)` blocks to `CMakeLists.txt` for compile/link flags, SDL2 port, WebGL version, memory settings, and asset preloading.

8. **Create web shell**: Add `web/shell.html` with a loading bar and canvas element. Emscripten provides a default but a custom one allows branding.

9. **WASM main loop**: Add `#ifdef __EMSCRIPTEN__` path in `Game::Run()` using `emscripten_set_main_loop`.

10. **Test build**: Get a minimal build compiling and rendering a colored rectangle in the browser.

### Phase 3: Polish and distribution

11. **Asset pipeline for web**: Ensure `game package` output works with `--preload-file`. May need a packer mode that outputs the SQLite DB in a web-friendly location.

12. **Save data persistence**: Implement IDBFS mounting for save files on web.

13. **CI/CD web target**: Add Emscripten build to the GitHub Actions matrix.

14. **Size optimization**: Apply `-Os`, `-flto`, `--closure 1`, `wasm-opt`, Brotli compression.

15. **Complete Windows/macOS support**: Fill in the stubbed platform code, test builds on CI.

## Open questions

- **OpenGL version strategy**: Should we drop from 4.6 to 3.3 on desktop to narrow the gap with ES 3.0? We don't use any features above 3.3 except debug output (4.3). Lowering to 3.3 would simplify shader maintenance and increase hardware compatibility.

- **WebGPU instead of WebGL 2?**: As of December 2025, WebGPU is supported by all major browsers. It's more performant than WebGL 2 and is the clear future direction. However, it requires a completely different rendering API — not a minor shader tweak. Sokol's `SOKOL_WGPU` backend demonstrates this is feasible, but it's a much larger effort. Worth considering for a future rendering backend rewrite.

- **LuaJIT decision**: Should WASM portability influence the LuaJIT migration decision? If we migrate to LuaJIT, we commit to maintaining two Lua runtimes. If we stay on Lua 5.1, WASM is simpler but desktop performance for compute-heavy scripts is lower.

- **Custom shader portability**: How should user-supplied `.vert`/`.frag` shaders handle the ES 3.0 vs desktop difference? Options: (a) require two versions per shader, (b) auto-translate the version line and inject precision qualifiers, (c) document "write to ES 3.0 subset" as the only supported path.

- **Audio backend**: Is SDL2 audio via Emscripten good enough for shipping, or should we invest in an OpenAL backend for lower web latency from the start?

- **Single-file packaging on web**: The current `game package` appends assets to the executable binary. On web, assets are a separate `.data` file. Should the packer have a `--target web` mode, or should the Emscripten `--preload-file` approach handle it?
