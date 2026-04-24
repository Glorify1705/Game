---
status: in-design
tags: [wasm, android, ios, portability]
---

# Multiplatform Support

## Motivation

The engine currently targets Linux (primary), Windows, and macOS. Both high_impact and Anchor — our closest comparison engines — ship to Windows, macOS, Linux, and Web (WASM). Web/WASM is listed as a Tier 3 gap in the engine comparison doc, but it's arguably the most impactful platform gap: it enables instant sharing, zero-install playtesting, and distribution on itch.io as a browser game. Mobile (Android/iOS) extends reach further — most 2D indie games that succeed commercially have mobile ports.

This document explores what it would take to make the engine portable to WASM, Android, and iOS, and how other 2D engines have solved the same problems.

## Current platform status

| Platform | Status | Toolchain | CI |
|---|---|---|---|
| Linux | Primary | GCC | Yes |
| Windows | Supported | clang-cl | Yes |
| macOS | Supported | Apple Clang (osxcross) | Yes |
| Web (WASM) | Not started | — | — |
| Android | Not started | — | — |
| iOS | Not started | — | — |

## How other engines handle multiplatform

### Sokol (most architecturally relevant)

Sokol is a set of single-file C headers for cross-platform development. WASM and mobile were primary design motivations.

**Architecture**: Three layers — `sokol_app.h` (platform), `sokol_gfx.h` (graphics), `sokol_glue.h` (bridge). The graphics layer has compile-time backend selection via preprocessor defines:

| Define | Backend |
|---|---|
| `SOKOL_GLCORE` | Desktop OpenGL 4.1+ |
| `SOKOL_GLES3` | OpenGL ES 3.0 (mobile/web) |
| `SOKOL_D3D11` | Direct3D 11 (Windows) |
| `SOKOL_METAL` | Metal (macOS/iOS) |
| `SOKOL_WGPU` | WebGPU |

Application code is identical across all targets — only build flags change. A build-time shader cross-compiler (`sokol-shdc`) converts annotated GLSL into platform-specific shader code (GLSL, HLSL, MSL, SPIR-V, WGSL).

`sokol_app.h` provides a unified app lifecycle model with `sapp_desc` callbacks (`init_cb`, `frame_cb`, `cleanup_cb`, `event_cb`) that map to each platform's native lifecycle: `main()` on desktop, `emscripten_set_main_loop` on web, `UIApplicationDelegate` on iOS, `ANativeActivity` on Android. Touch events are normalized alongside mouse/keyboard into a single `sapp_event` struct.

**Key insight**: Define a narrow, low-level graphics API that maps naturally onto both desktop OpenGL and OpenGL ES, with compile-time backend selection and build-time shader translation. Unify input and lifecycle events across all platforms behind a single callback-based model.

### Raylib

First-class WASM and mobile support. Build steps for WASM:

1. Compile each module with `emcc -Os -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2`.
2. Link with `--use-port=sdl2` (or GLFW) and `--preload-file assets/`.
3. Output: `.html` + `.js` + `.wasm` + `.data`.

Two main loop strategies: refactor into a `UpdateDrawFrame()` callback (recommended, zero overhead), or use `-sASYNCIFY` to keep the standard while-loop (simpler but ~50% code size overhead). Uses OpenGL ES 2.0 / WebGL 1 for maximum compatibility. CMake integration via Emscripten's toolchain file.

For Android/iOS, Raylib uses GLFW with native platform backends. Touch input is translated to mouse events for simple games, with raw touch data available for multi-touch. The same OpenGL ES 2.0 path serves both mobile and web.

### high_impact (direct comparison target)

Uses Sokol under the hood (sokol_app, sokol_gfx, sokol_audio). This gives it free WASM and mobile support — the same code compiles to native, web, and mobile. Renders via OpenGL or WebGL 2 depending on target. Audio uses QOA format, decoded at runtime.

### Anchor (direct comparison target)

Uses SDL2 + OpenGL 3.3 / WebGL 2. Compiles to WASM via Emscripten with `--use-port=sdl2`. Uses miniaudio for sound. The codebase has `#ifdef __EMSCRIPTEN__` guards for main loop refactoring and asset loading differences. No mobile support.

### Godot

Most mature multiplatform export among open-source engines.

**Web**: Single-threaded by default (as of 4.3) to avoid `SharedArrayBuffer` COOP/COEP header requirement. Direct Web Audio API calls for low latency. WASM SIMD for performance-critical paths (~1.5-2x gains). Stripped 2D web export compresses to ~2.4 MB with Brotli.

**Mobile**: Separate platform layers for Android (`platform/android/`) and iOS (`platform/ios/`). Uses OpenGL ES 3.0 on Android (with ES 2.0 fallback for old devices) and Metal on iOS (with GLES fallback). Touch input is first-class. Platform-specific export templates handle app packaging, signing, and store metadata. The editor can produce APK/AAB (Android) and Xcode project (iOS) directly.

**Key insight**: Godot separates the "export template" (platform-specific shell + engine binary) from the "project data" (scripts + assets). This means the engine is compiled once per platform, and game-specific content is bundled separately. Mobile lifecycle events (pause/resume/low-memory) are handled in the platform layer and dispatched to the scripting runtime.

### Love2D

No official WASM or mobile support. Community ports (love.js for web, love-android for Android) exist but are second-class. The main blocker for web is LuaJIT — its core VM is hand-tuned assembly and cannot compile to WASM. Android ports require significant patching. This is directly relevant to our `LuaJIT Migration.md` — if we migrate to LuaJIT for desktop, we need a Lua 5.1 fallback path for WASM.

### Macroquad/miniquad (Rust)

Gold standard for "multiplatform-first" design. miniquad supports web, Android, iOS, and desktop from a single codebase. For web, it does **not use Emscripten** — it compiles Rust to `wasm32-unknown-unknown` and uses a small JavaScript loader. For mobile, it provides native activity wrappers. All platforms share the same OpenGL ES 3.0 / WebGL 2 rendering path. Not directly applicable to our C++ codebase, but the architecture is instructive: minimize platform-specific code to a thin shell, keep the engine platform-agnostic.

### Bevy (Rust)

Uses `wasm-bindgen` for JS interop and `wgpu` (WebGPU) for rendering in the browser. Its parallel ECS falls back to single-threaded on WASM. Android support is experimental. iOS support is community-maintained.

## Current engine audit

### What ports cleanly to all targets

| Component | WASM | Android | iOS | Notes |
|---|---|---|---|---|
| C++17 | Yes | Yes | Yes | Emscripten/NDK/Xcode all support C++17 |
| `-fno-exceptions -fno-rtti` | Ideal | Fine | Fine | Reduces binary size on all targets |
| SDL3 | Yes | Yes | Yes | SDL3 has first-class support for all three |
| Lua 5.1 | Yes | Yes | Yes | Pure C, compiles everywhere |
| Box2D | Yes | Yes | Yes | Pure C, no platform deps |
| SQLite3 | Yes | Yes | Yes | Widely used on all platforms |
| stb libs | Yes | Yes | Yes | Single-file C headers |
| pugixml | Yes | Yes | Yes | Pure C++ |
| double-conversion | Yes | Yes | Yes | Pure C++ |
| nlohmann/json | Yes | Yes | Yes | Header-only C++ |
| ENet | No | Yes | Yes | Raw UDP works on mobile, not in browsers |

SDL3 is a major advantage. It provides unified APIs for windowing, input (including touch), audio, and lifecycle events across all three targets. The engine is already on SDL3.

### What needs adaptation

#### 1. Main loop (WASM only — critical)

The current game loop in `game.cc` is a blocking `while (running)` loop:

```cpp
void Game::Run() {
    SDL_ResumeAudioDevice(sdl->audio_device);
    last_frame = Now();
    while (running) {
        HandleHotReload();
        // poll events, update, render
    }
}
```

Browsers use cooperative multitasking — an infinite loop freezes the tab. This must be refactored into a single-frame step function called via `emscripten_set_main_loop`.

**Why this is WASM-only**: On Android and iOS, SDL3 handles the platform event loop internally. The blocking `while (running)` loop works because SDL3's `SDL_PollEvent` integrates with the native run loop (Android's `Looper`, iOS's `NSRunLoop`). No refactoring needed for mobile.

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
    while (running) {
        if (!StepFrame()) return;
    }
}
#endif
```

The state variables (`last_frame`, `accum`) are already `Game` members, so the refactor is clean.

#### 2. OpenGL ES 3.0 shaders (all three targets)

All engine shaders use `#version 410 core` (`shaders.cc`). The OpenGL context requests version 4.1 (`sdl_init.cc`). WebGL 2 corresponds to OpenGL ES 3.0 — `#version 300 es`. Android and iOS also use OpenGL ES 3.0.

**Key differences**:

| Feature | OpenGL 4.1 core | OpenGL ES 3.0 / WebGL 2 |
|---|---|---|
| Shader version | `#version 410 core` | `#version 300 es` |
| Precision qualifiers | Optional | Required for `float` in fragment shaders |
| `layout(location=N)` | Yes | Yes |
| `in`/`out` qualifiers | Yes | Yes |
| `texture()` function | Yes | Yes |
| Geometry shaders | Yes | No |
| Compute shaders | Yes (4.3+) | No |
| `glLineWidth` | Works | Ignored by browsers; works on mobile |
| Debug output | Yes (`GL_KHR_debug`) | No (web); varies (mobile) |

Our shaders use only basic features (vertex transforms, texture sampling, per-vertex color, SDF font rendering) that are fully available in OpenGL ES 3.0. No geometry shaders, no compute, no tessellation.

**Approach**: Maintain two shader versions — the existing `#version 410 core` for desktop and `#version 300 es` equivalents for WASM/mobile. The differences are small:

```glsl
// Desktop (current)
#version 410 core
out vec4 frag_color;

// WASM / mobile (ES)
#version 300 es
precision mediump float;
out vec4 frag_color;
```

Options (in order of preference):

1. **Preprocessor selection at compile time**: Define shaders with a `SHADER_VERSION_HEADER` macro that expands to the right version string and precision qualifiers. Simplest, zero runtime cost.

2. **Build-time shader cross-compilation** (sokol-shdc approach): Write shaders in annotated GLSL, cross-compile to GLSL 410, GLSL ES 300, and potentially HLSL/MSL. More infrastructure but enables future Metal/Vulkan backends.

3. **Drop to OpenGL 3.3 + ES 3.0 common subset everywhere**: `#version 330 core` on desktop, `#version 300 es` on WASM/mobile. Since our shaders don't use any 4.1-specific features, this is feasible but loses debug output support on desktop.

User-supplied custom shaders (loaded from `.vert`/`.frag` asset files) present an additional challenge — either document that game developers must provide both versions, or provide a simple translation pass (version line replacement + precision qualifier injection).

#### 3. GLAD loader (WASM and mobile)

GLAD (`libraries/glad.cc`) loads desktop OpenGL function pointers. On Emscripten, OpenGL ES functions are provided directly — GLAD is unnecessary and won't work. On Android/iOS, EGL provides the GL ES context; GLAD is also not needed.

**Approach**: Conditionally exclude GLAD on non-desktop builds:

```cpp
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#elif defined(__ANDROID__) || defined(TARGET_OS_IPHONE)
#include <GLES3/gl3.h>
#else
#include "libraries/glad.h"
#endif
```

The GLAD loader call in `CreateOpenglContext` (`gladLoadGLLoader`) is skipped on non-desktop targets. GLAD feature-detection macros need appropriate guards.

#### 4. Touch input (Android and iOS — critical)

The input system (`input.cc`) handles keyboard, mouse, and gamepad exclusively. There is **no touch event handling** — zero references to `SDL_FINGER`, `SDL_TOUCH`, or touch-related APIs anywhere in the codebase.

SDL3 provides touch events via `SDL_EVENT_FINGER_DOWN`, `SDL_EVENT_FINGER_UP`, `SDL_EVENT_FINGER_MOTION`. Each event carries a finger ID, normalized x/y coordinates (0.0–1.0), and pressure.

**Approach**: Add a `Touch` subsystem alongside `Keyboard`, `Mouse`, and `Controllers`:

```cpp
struct TouchFinger {
    SDL_FingerID id;
    float x, y;       // Normalized 0-1.
    float pressure;
    bool pressed;      // Down this frame.
    bool released;     // Up this frame.
};

class Touch {
 public:
    void HandleEvent(const SDL_Event& event);
    void EndFrame();  // Clear per-frame flags.

    int finger_count() const;
    const TouchFinger* finger(int index) const;
    bool is_any_down() const;
 private:
    FixedArray<TouchFinger, 10> fingers_;
};
```

Lua bindings (`lua_input.cc`):

```lua
-- New touch API
local touches = G.input.touches()        -- array of {id, x, y, pressure}
local down = G.input.is_touch_pressed()  -- any finger went down this frame
local count = G.input.touch_count()      -- number of fingers currently down
```

For simple games, touch can also be mapped to mouse events (SDL3 does this by default on some platforms, but explicit control is better).

**WASM note**: Touch events also fire in mobile browsers, so the same `Touch` subsystem serves web on mobile devices. On desktop browsers, mouse events are sufficient.

#### 5. App lifecycle events (Android and iOS — critical)

The engine does not handle mobile lifecycle events. The main loop only checks for `SDL_EVENT_QUIT`. Mobile OSes aggressively suspend and kill background apps.

SDL3 provides these events:

| SDL3 Event | Meaning | Required action |
|---|---|---|
| `SDL_EVENT_WILL_ENTER_BACKGROUND` | App is about to be backgrounded | Pause audio, save state |
| `SDL_EVENT_DID_ENTER_BACKGROUND` | App is now in background | Release non-essential resources |
| `SDL_EVENT_WILL_ENTER_FOREGROUND` | App is about to resume | Prepare to restore resources |
| `SDL_EVENT_DID_ENTER_FOREGROUND` | App is now in foreground | Resume audio, restore GL context |
| `SDL_EVENT_LOW_MEMORY` | OS is low on memory | Free caches, non-critical allocations |
| `SDL_EVENT_TERMINATING` | App is being killed | Last chance to save |

**Approach**: Add lifecycle handling to the event loop in `game.cc`:

```cpp
case SDL_EVENT_WILL_ENTER_BACKGROUND:
    SDL_PauseAudioDevice(sdl->audio_device);
    // Notify Lua: game.on_pause() if defined
    break;
case SDL_EVENT_DID_ENTER_FOREGROUND:
    SDL_ResumeAudioDevice(sdl->audio_device);
    // Notify Lua: game.on_resume() if defined
    break;
case SDL_EVENT_LOW_MEMORY:
    // Trim allocator caches, release non-essential textures
    break;
```

Expose lifecycle callbacks to Lua scripts so games can save progress on suspend.

#### 6. CLI-driven architecture (Android and iOS — critical)

The engine's entry point (`game.cc:Main()`) dispatches to CLI subcommands (`game run`, `game init`, `game package`, etc.). Mobile apps have no CLI.

The packaged game path (`CmdRunPackaged`) is close to what mobile needs — it runs a game from bundled assets without a source directory. But it still expects argc/argv parsing and filesystem paths relative to the executable.

**Approach**: Separate engine initialization from CLI parsing:

```cpp
// New: platform-agnostic entry point.
struct GameParams {
    const char* asset_path;    // Path to assets.sqlite3.
    const char* save_dir;      // Platform save directory.
    bool hotreload;            // Always false on mobile.
    int argc;                  // 0 on mobile.
    const char** argv;         // nullptr on mobile.
};

int RunGame(const GameParams& params);

// Desktop: Main() parses CLI, fills GameParams, calls RunGame().
// Mobile: SDL_main or platform entry fills GameParams, calls RunGame().
```

On Android, the asset path points to the APK's asset directory (accessible via SDL3's `SDL_GetBasePath()` or Android asset manager). On iOS, it points to the app bundle's resource directory.

#### 7. Display / DPI / screen handling (Android and iOS)

Window creation in `sdl_init.cc` uses a fixed 1440x1024 resolution from `config.h`. There is no DPI awareness, safe area handling, or orientation support.

**Issues for mobile**:

| Concern | Desktop | Mobile |
|---|---|---|
| Resolution | Fixed window size | Fullscreen, varies wildly (720p to 4K) |
| DPI | ~96-192 | 150-600+ (must scale UI) |
| Aspect ratio | ~16:10 | 18:9 to 21:9 |
| Orientation | Always landscape | Portrait or landscape, can change at runtime |
| Safe areas | N/A | Notches, rounded corners, system bars |

**Approach**: On mobile, always create a fullscreen window and query the actual display size:

```cpp
#if defined(__ANDROID__) || defined(TARGET_OS_IPHONE)
flags |= SDL_WINDOW_FULLSCREEN;
// After creation:
int w, h;
SDL_GetWindowSizeInPixels(window, &w, &h);
float dpi_scale = SDL_GetWindowDisplayScale(window);
```

Expose display info to Lua so games can adapt their layout:

```lua
local w, h = G.window.size()           -- Pixels.
local scale = G.window.display_scale() -- DPI multiplier.
local safe = G.window.safe_area()      -- {x, y, w, h} inset.
```

#### 8. Platform-specific paths (Android and iOS)

`platform.cc` implements `GetExePath()` and `GetUserCacheDir()` for Windows, macOS, and Linux only. No `__ANDROID__` or `TARGET_OS_IPHONE` branches exist.

**Android**:
- Exe path: not meaningful. Use `SDL_GetBasePath()` for the APK's lib directory.
- Cache dir: `SDL_GetPrefPath()` or JNI call to `Context.getCacheDir()`.
- Assets: Bundled in the APK, accessed via `SDL_RWFromFile()` (SDL3's Android asset integration) or extracted to cache on first run.

**iOS**:
- Exe path: app bundle path via `[[NSBundle mainBundle] resourcePath]`.
- Cache dir: `NSSearchPathForDirectoriesInDomains(NSCachesDirectory, ...)` or `SDL_GetPrefPath()`.
- Assets: Bundled in the `.app` directory, directly accessible via normal file I/O.

**Approach**: Add platform branches to `platform.cc`:

```cpp
#if defined(__ANDROID__)
Error GetUserCacheDir(char* buf, size_t size) {
    const char* path = SDL_GetPrefPath("Game", app_name);
    // ...
}
#elif defined(TARGET_OS_IPHONE)
Error GetUserCacheDir(char* buf, size_t size) {
    // NSSearchPathForDirectoriesInDomains or SDL_GetPrefPath
}
#endif
```

SDL3's `SDL_GetBasePath()` and `SDL_GetPrefPath()` handle the platform differences for most cases, reducing the amount of platform-specific code needed.

#### 9. Memory budget (WASM and mobile)

The engine allocates a 4 GB arena (`game.cc`):

```cpp
constexpr size_t kEngineMemory = Gigabytes(4);
```

32-bit WASM has a 4 GB theoretical address space limit. In practice, browsers cap usable memory lower — many mobile browsers fail above 1 GB. Mobile devices vary widely (2-16 GB RAM), and the OS reserves a large portion.

**Approach**: Platform-dependent memory budget:

```cpp
#if defined(__EMSCRIPTEN__)
constexpr size_t kEngineMemory = Megabytes(256);
#elif defined(__ANDROID__) || defined(TARGET_OS_IPHONE)
constexpr size_t kEngineMemory = Megabytes(512);
#else
constexpr size_t kEngineMemory = Gigabytes(4);
#endif
```

Use `-sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH` in the Emscripten link flags. 256 MB should be generous for a 2D game; profile and adjust.

On mobile, 512 MB is conservative. The engine could also query available memory at runtime and adjust, but a fixed budget is simpler and more predictable with arena allocators.

#### 10. PhysFS / filesystem (all three targets)

PhysFS is used for virtual filesystem operations (`filesystem.cc`). It works on all three targets in principle (pure C with POSIX fallbacks), but the virtual filesystem needs platform-appropriate backing storage.

| Target | Read path | Write path |
|---|---|---|
| WASM | Emscripten MEMFS (via `--preload-file`) | IDBFS for save data |
| Android | APK assets (SDL3 asset integration) | `SDL_GetPrefPath()` |
| iOS | App bundle resources | `SDL_GetPrefPath()` |

PhysFS mount points would be set up differently per platform during initialization. The rest of the engine's file I/O goes through PhysFS and remains unchanged.

#### 11. Networking (WASM only — critical)

ENet uses raw UDP sockets, which browsers cannot access (security sandbox). Only WebSocket (TCP) and WebRTC (UDP-like, but complex) are available in browsers.

**This does not affect mobile** — Android and iOS support raw UDP sockets, so ENet works as-is.

**WASM options**:
1. **Disable networking on web**: Simplest. Many 2D indie games are single-player.
2. **WebSocket transport**: Replace ENet's socket layer with a WebSocket wrapper for WASM builds. Adds latency but enables basic multiplayer.
3. **WebRTC data channels**: Closest to UDP semantics in the browser, but significantly more complex (ICE, STUN/TURN negotiation).

#### 12. File watching / hot reload (trivial to disable)

The `FileWatcher` class uses Linux inotify. Hot reload makes no sense on mobile or in packaged web builds. Already has a `--no-hotreload` flag.

**Approach**: Disable at compile time on non-desktop targets:

```cpp
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE)
// No file watching on these platforms.
#else
// inotify / FSEvents / ReadDirectoryChangesW
#endif
```

#### 13. Threading (WASM only)

The `ThreadPoolExecutor` (`executor.cc`) uses `std::thread`, `std::mutex`, and `std::condition_variable`. On Emscripten, threading requires `SharedArrayBuffer` which requires specific HTTP headers (`Cross-Origin-Embedder-Policy`, `Cross-Origin-Opener-Policy`). Many hosting services (itch.io, GitHub Pages) don't set these.

**This does not affect mobile** — pthreads work natively on Android and iOS. The thread pool runs without changes.

**WASM approach**: Force use of `InlineExecutor` (already exists). All parallel work becomes sequential on the main thread.

#### 14. Audio (minor, all targets)

The engine uses SDL3 audio callbacks with a buffer at 44100 Hz. This works on all three targets through SDL3.

**WASM**: Audio processing runs on the main thread in the browser. SDL3's Web Audio API integration handles this. Slightly higher latency than native, but acceptable for a 2D game. Browsers require a user gesture (click/tap) before audio can start — SDL3 handles this automatically.

**Mobile**: SDL3 audio works natively on Android (OpenSL ES / AAudio) and iOS (Core Audio). No changes needed. One consideration: mobile OSes may interrupt audio for phone calls or notifications — handled via the lifecycle events (pause audio on background, resume on foreground).

#### 15. mimalloc (moderate risk, all targets)

The engine uses mimalloc for Lua's allocator via `MimallocAllocator`, which manages arenas with `mi_manage_os_memory_ex`.

**WASM**: Emscripten has native mimalloc support (`-sMALLOC=mimalloc`), but the arena-based approach (`mi_manage_os_memory_ex`) may behave differently in WASM's linear memory model. Needs testing.

**Mobile**: mimalloc compiles and runs on Android and iOS. No known issues. The 64 KiB alignment requirement for arenas is satisfied on both platforms.

### Platform-specific features that don't apply

| Component | WASM | Android | iOS |
|---|---|---|---|
| Hot reload | N/A | N/A | N/A |
| File watching | N/A | N/A | N/A |
| Thread pool | Disabled | Works | Works |
| Debug output (`GL_KHR_debug`) | No | Varies | No |
| `SDL_ShowSimpleMessageBox` | No | Works | Works |
| X11 window hints | N/A | N/A | N/A |
| ENet networking | No | Works | Works |
| `glLineWidth` | Ignored | Works | Works |

## Build system integration

### WASM: CMake with Emscripten toolchain

Emscripten provides `Emscripten.cmake` as a CMake toolchain file. The build invocation:

```bash
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```

CMakeLists.txt changes:

```cmake
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # SDL3 is vendored; Emscripten's POSIX layer handles the rest.
    # No need to find_package(OpenGL).

    target_compile_options(Game PRIVATE
        -sUSE_SDL=0  # Using vendored SDL3, not Emscripten's SDL2 port.
    )

    target_link_options(Game PRIVATE
        -sMAX_WEBGL_VERSION=2
        -sMIN_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH
        -sINITIAL_MEMORY=67108864
        -sMALLOC=mimalloc
        -sENVIRONMENT=web
        --preload-file ${CMAKE_BINARY_DIR}/assets.sqlite3@/assets.sqlite3
        --shell-file ${PROJECT_SOURCE_DIR}/web/shell.html
    )

    # Exclude GLAD, file watcher, thread pool.
else()
    find_package(OpenGL REQUIRED)
    # ... existing desktop build
endif()
```

Output files:

| File | Purpose | Typical size (2D game) |
|---|---|---|
| `game.html` | Page shell (customizable) | 1-2 KB |
| `game.js` | JS glue code (loads WASM, initializes) | 50-200 KB |
| `game.wasm` | Compiled engine + game logic | 2-8 MB |
| `game.data` | Preloaded assets (SQLite DB) | Varies |

Size optimization flags: `-Os`, `-flto`, `--closure 1`, `-sENVIRONMENT=web`, `-sEVAL_CTORS`. Post-link: `wasm-opt -Oz`. WASM compresses extremely well — a 5 MB `.wasm` file compresses to ~600 KB with Brotli.

### Android: CMake with NDK toolchain

The Android NDK provides a CMake toolchain file. Build invocation:

```bash
cmake -B build-android \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-android
```

The NDK toolchain sets up cross-compilation for ARM64 (primary) and x86_64 (emulator). Minimum API level 26 (Android 8.0) covers 95%+ of active devices and provides OpenGL ES 3.0.

SDL3 provides an Android project template (`android-project/`) that wraps the native library in a Java/Kotlin activity. The CMake build produces a shared library (`libgame.so`) that the SDL3 activity loads via JNI.

Additional Android requirements:
- `AndroidManifest.xml` with permissions, screen orientation, GL ES version requirement.
- Gradle wrapper for APK/AAB packaging.
- Asset files placed in `app/src/main/assets/` for inclusion in the APK.
- Signing configuration for Play Store distribution.

### iOS: CMake with Xcode generator

```bash
cmake -B build-ios -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64
```

Minimum iOS 15.0 covers 95%+ of active devices. The Xcode generator produces a `.xcodeproj` that can build, sign, and package the app.

SDL3 provides iOS support through `UIApplicationDelegate`. The CMake build produces a `.app` bundle. Assets are added to the Xcode project as bundle resources.

Additional iOS requirements:
- `Info.plist` with display name, bundle ID, orientation settings, required device capabilities.
- Code signing with Apple Developer certificate.
- Launch storyboard or launch screen image.
- App Store Connect metadata for distribution.

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
        target: web        # Emscripten runs on Linux
      - os: ubuntu-latest
        target: android    # NDK runs on Linux
      - os: macos-latest
        target: ios        # Xcode required
```

For web: use the `mymindstorm/setup-emsdk` GitHub Action. For Android: use the NDK bundled with the runner image. For iOS: use the Xcode bundled with the macOS runner.

Each target produces a zip artifact. The web build zip contains `index.html` + `.js` + `.wasm` + `.data`, ready for itch.io upload. The Android build produces an APK/AAB. The iOS build produces an `.xcarchive` (or just verifies compilation, since signing requires developer credentials).

## Asset loading

### Current pipeline

Source files (PNG, WAV, OGG, Lua, etc.) are packed into a SQLite database (`assets.sqlite3`) at build time. At runtime, the engine opens the database and loads assets on demand.

### Per-platform approach

**WASM**: Bundle via `--preload-file`. Emscripten downloads the SQLite file and mounts it in MEMFS before `main()` runs. SQLite reads from MEMFS transparently. Save data uses IDBFS with periodic sync:

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_ASM(
    FS.syncfs(false, function(err) {
        if (err) console.error('syncfs error:', err);
    });
);
#endif
```

**Android**: `assets.sqlite3` is placed in the APK's assets directory. SDL3 can open APK assets via `SDL_RWFromFile()`. Alternatively, extract the database to the app's internal storage on first launch for direct file I/O access. Save data goes to `SDL_GetPrefPath()`.

**iOS**: `assets.sqlite3` is included as a bundle resource. It's directly accessible via normal file I/O (the app bundle is a directory on disk). Save data goes to the Documents or Library directory via `SDL_GetPrefPath()`.

### Startup experience

**WASM**: The `.data` file must download before the game starts. For a typical 2D indie game (5-20 MB), this takes 1-3 seconds. Emscripten provides progress callbacks for a loading bar. A custom shell HTML (`--shell-file`) can display a splash screen.

**Mobile**: Assets are bundled in the APK/IPA and available immediately. No download step. First-launch extraction (if needed for SQLite) is fast since it's local I/O.

## LuaJIT implications

The `LuaJIT Migration.md` design doc proposes migrating from Lua 5.1 to LuaJIT for performance. **LuaJIT cannot compile to WASM** because its core VM is hand-tuned assembly (x86, ARM, MIPS, PPC).

**LuaJIT does work on Android** (ARM64, x86_64) and **iOS** (ARM64, with restrictions — no JIT due to W^X policy, but the interpreter is still faster than PUC Lua 5.1).

**Options**:

1. **Stay on Lua 5.1**: No issue. Works on all platforms.
2. **Migrate to LuaJIT with Lua 5.1 fallback for WASM**: Use LuaJIT on desktop and mobile, Lua 5.1 on WASM. LuaJIT is API-compatible with Lua 5.1. The main risk is accidentally depending on LuaJIT extensions (FFI, `bit` library) in game scripts.
3. **Use Lua 5.1 everywhere**: Simpler. Performance difference matters less for a scripting layer in a 2D game.

## Distribution strategy

| Target | Toolchain | Output | Distribution |
|---|---|---|---|
| Linux | GCC/Clang | ELF binary + assets | itch.io, AppImage, tarball |
| Windows | clang-cl | `.exe` + DLLs + assets | itch.io, Steam, zip |
| macOS | Apple Clang | `.app` bundle + assets | itch.io, Steam, DMG |
| Web | Emscripten | `.html` + `.js` + `.wasm` + `.data` | itch.io (browser), GitHub Pages |
| Android | NDK (Clang) | APK / AAB | Google Play, itch.io, sideload |
| iOS | Xcode (Clang) | IPA / `.app` | App Store, TestFlight |

### itch.io pattern

Most indie developers use:
- **Web build on itch.io** for free demos / instant play (zero friction, widest reach).
- **Desktop builds on itch.io or Steam** for the paid/full release.
- **Mobile builds on Play Store / App Store** for the widest audience.
- The web build doubles as marketing — players try instantly, then buy the full version.

itch.io's `butler` CLI tool automates uploads from CI.

## Implementation plan

### Phase 1: Shared groundwork (benefits all targets)

These changes improve the codebase for all platforms, not just new ones.

1. **Extract `StepFrame()`**: Move the game loop body from `Game::Run()` into `Game::StepFrame()`. The `while (running)` loop just calls `StepFrame()`. This is a pure refactor with no behavior change. Required for WASM, beneficial for code clarity.

2. **Shader version abstraction**: Add a compile-time shader header selection. Define shaders with a substitutable version/precision prefix. Verify all shaders compile under `#version 300 es` with `precision mediump float`.

3. **Conditional GLAD**: Wrap GLAD includes and the loader call behind `#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(TARGET_OS_IPHONE)`. On non-desktop targets, include `<GLES3/gl3.h>` instead.

4. **Conditional file watcher**: Disable on non-desktop targets.

5. **Separate CLI from engine init**: Refactor `Main()` so that `RunGame()` can be called without argc/argv parsing. The CLI path calls `RunGame()` after parsing; mobile/web paths call it directly with platform-appropriate parameters.

6. **Configurable memory budget**: Make `kEngineMemory` a compile-time platform-dependent constant.

### Phase 2: WASM target

7. **Add Emscripten CMake path**: `if(EMSCRIPTEN)` blocks for compile/link flags, WebGL version, memory settings, asset preloading.

8. **Create web shell**: `web/shell.html` with loading bar and canvas.

9. **WASM main loop**: `emscripten_set_main_loop` path in `Game::Run()`.

10. **Test build**: Get a minimal build rendering in the browser.

11. **Asset pipeline for web**: Ensure `game package` output works with `--preload-file`.

12. **Save data persistence**: IDBFS mounting for save files on web.

13. **CI web target**: Add Emscripten build to GitHub Actions.

14. **Size optimization**: `-Os`, `-flto`, `--closure 1`, `wasm-opt`, Brotli.

### Phase 3: Android target

15. **Add NDK CMake toolchain path**: Cross-compilation for ARM64 and x86_64.

16. **SDL3 Android activity**: Set up the Java/Kotlin wrapper project using SDL3's Android template.

17. **Touch input subsystem**: New `Touch` class in `input.cc`, Lua bindings in `lua_input.cc`.

18. **App lifecycle events**: Handle background/foreground/low-memory in the event loop.

19. **Platform paths**: Add `__ANDROID__` branches to `platform.cc` using SDL3 path APIs.

20. **Display handling**: Fullscreen mode, DPI scaling, safe area queries.

21. **Test on device**: Get the engine running on an Android device or emulator.

22. **Gradle/APK packaging**: Build pipeline from CMake shared library to signed APK.

23. **CI Android target**: Add NDK build to GitHub Actions.

### Phase 4: iOS target

24. **Add iOS CMake/Xcode path**: Cross-compilation for ARM64.

25. **Platform paths**: Add `TARGET_OS_IPHONE` branches to `platform.cc`.

26. **Touch input**: Already implemented in Phase 3 (shared with Android).

27. **Lifecycle events**: Already implemented in Phase 3 (shared with Android).

28. **Display handling**: Safe area insets for notch/Dynamic Island, DPI scaling.

29. **Xcode project setup**: Info.plist, launch screen, code signing.

30. **Test on device**: Get the engine running on an iOS device or simulator.

31. **CI iOS target**: Add Xcode build to GitHub Actions (compile-only, no signing).

### Phase 5: Polish

32. **Complete Windows/macOS support**: Fill in remaining platform stubs, test on CI.

33. **Cross-platform testing**: Ensure all platforms pass the test suite.

34. **Documentation**: Platform-specific build instructions, distribution guides, known limitations.

## Effort summary

| Target | Difficulty | Key blockers | Estimated effort |
|---|---|---|---|
| WASM | Medium | Main loop refactor, shader downgrade, no networking | Moderate |
| Android | Medium-Hard | Touch input, lifecycle, CLI refactor, Gradle packaging | Significant |
| iOS | Medium | Xcode setup, signing; shares most work with Android | Moderate (after Android) |

Phases 1 (shared groundwork) and 2 (WASM) can proceed independently. Phase 3 (Android) and Phase 4 (iOS) share significant work (touch input, lifecycle, display handling) and should be done sequentially — Android first, then iOS reuses most of the infrastructure.

## Open questions

- **OpenGL version strategy**: Keep GL 4.1 on desktop (for debug output via `GL_KHR_debug`). Use a shader precompiler that emits `#version 410 core` for desktop and `#version 300 es` with precision qualifiers for WASM/mobile. Game developers write shaders targeting the ES 3.0 common subset; the precompiler injects the version header and platform-specific preamble. This avoids maintaining two shader copies while preserving debug output on desktop.

- **WebGPU instead of WebGL 2?**: WebGPU is supported by all major browsers and is more performant than WebGL 2. However, it requires a completely different rendering API. Sokol's `SOKOL_WGPU` backend shows this is feasible, but it's a much larger effort. Worth considering for a future rendering backend rewrite.

- **Metal on iOS?**: OpenGL ES is deprecated on iOS (since iOS 12). It still works and Apple has not removed it, but new features go to Metal only. For a 2D game using basic GL ES 3.0, the deprecation is unlikely to matter in practice. A Metal backend would be a significant investment — only worth it if Apple actually removes GL ES support.

- **LuaJIT decision**: Should multiplatform portability influence the LuaJIT migration decision? LuaJIT works on desktop and mobile but not WASM. Staying on Lua 5.1 is simpler. If we migrate, we maintain two runtimes.

- **Custom shader portability**: How should user-supplied `.vert`/`.frag` shaders handle the ES 3.0 vs desktop difference? Options: (a) require two versions per shader, (b) auto-translate the version line and inject precision qualifiers, (c) document "write to ES 3.0 subset" as the only supported path.

- **Audio backend**: Is SDL3 audio good enough for all platforms, or should we invest in platform-specific backends (Web Audio API for lower web latency, AAudio for lower Android latency)?

- **Touch-to-mouse emulation**: Should simple games that only use mouse input automatically work with touch? SDL3 can do this, but explicit control is usually better. Possibly provide a `G.input.emulate_mouse_from_touch(true)` option.

- **App Store requirements**: Both Google Play and Apple App Store have review processes, content policies, and technical requirements (64-bit, target API levels, privacy manifests). These are not engineering problems but add distribution overhead. Worth documenting per-platform.

- **Android minimum API level**: API 26 (Android 8.0) is proposed above. Lower levels increase reach but add compatibility burden. API 21 (5.0) is the lowest that supports OpenGL ES 3.1; API 24 (7.0) supports Vulkan. For ES 3.0, API 18 (4.3) is sufficient but very old.
