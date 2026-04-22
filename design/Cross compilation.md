---
status: in-progress
tags: [build, cross-compilation, packaging, portability, sfx]
---

# Cross Compilation

## Glossary

- **Cross compilation**: Compiling code on one platform (the _host_) to produce
  binaries for a different platform (the _target_). Example: building a Windows
  `.exe` on a Linux machine.
- **Toolchain file**: A CMake file (`-DCMAKE_TOOLCHAIN_FILE=...`) that tells
  CMake which compiler, linker, and system libraries to use for a given target.
  MinGW toolchain files are the standard approach for Linux-to-Windows cross
  compilation.
- **MinGW-w64**: A GCC-based toolchain that produces native Windows binaries
  from Linux. Provides `x86_64-w64-mingw32-g++` and related tools.
- **Target triple**: A string identifying the target platform, e.g.
  `x86_64-w64-mingw32` (64-bit Windows via MinGW) or
  `x86_64-unknown-linux-gnu`.
- **Sysroot**: The directory tree containing headers and libraries for the
  target platform. MinGW provides a Windows sysroot on Linux.

## Problem

The engine currently builds and runs on Linux. To test a game on a Windows
machine, you must either:

1. Set up a full Windows build environment (Visual Studio or MSYS2/MinGW,
   install all dependencies, clone the repo, build from scratch), or
2. Use a Windows VM on the development machine.

Both are slow and friction-heavy. The goal is to run `game package --target
windows` on the Linux host and produce a directory (or zip) containing a
Windows `.exe` + `assets.sqlite3` that can be copied to a Windows machine and
run directly.

### Concrete issues (original, most now resolved)

| Issue | Status |
|---|---|
| No CMake toolchain file for cross compilation | **Fixed.** `cmake/mingw-w64-x86_64.cmake` |
| `game package` copies the host binary | **Fixed.** `--engine-binary` flag |
| No way to specify a target platform | Partially addressed — `--engine-binary` works; `--target` convenience flag pending (Phase 3) |
| Vendored SDL2 Windows libs are stale | **Fixed.** Deleted; SDL3 vendored from source |
| MSVC compatibility unverified | Deferred — MinGW uses GCC, no issue for cross-compilation |
| No SDL3 Windows libraries vendored | **Fixed.** SDL3 built via `add_subdirectory()` |
| No CI for Windows builds | Pending (Phase 4) |

## Current state

Phases 0–2 are complete. The engine cross-compiles from Linux to Windows via
MinGW, and `game package` produces a self-contained distributable with all
required DLLs. Self-extracting .7z.exe archives are supported via `--sfx`.
Tested successfully on Windows with multiple games (flappybird, Space Garbage!).

### What works

- **All dependencies are vendored** and written in portable C/C++:
  Box2D, Lua 5.1, PhysFS, mimalloc, SQLite3, stb_*, GLAD,
  double-conversion, yyjson, backward-cpp, dr_wav.
- **SDL3 is vendored** and built via `add_subdirectory()` for both Linux
  and Windows. Stale SDL2 libraries have been deleted.
- **Platform abstraction is clean.** `platform.cc` has complete `#ifdef _WIN32`
  implementations for file operations, directory traversal, path handling, and
  executable detection. Thread naming in `thread.h` handles Windows.
  `file_watcher.h` documents per-platform backends.
- **Asset format is platform-agnostic.** SQLite databases and the asset schema
  have no platform-specific content.
- **MinGW cross-compilation toolchain** is set up via
  `scripts/setup-mingw-toolchain.sh` (xpack GCC 14.2, with NixOS patchelf
  support). CMake toolchain file at `cmake/mingw-w64-x86_64.cmake`.
- **`game package`** accepts `--engine-binary` to use a pre-built Windows
  binary, `--sfx` for self-extracting archives, and `--strip` for stripping
  debug symbols. Auto-copies SDL3.dll and MinGW runtime DLLs.
- **Portability fixes** applied: backward.h include order, pcg_random.h
  exception removal, sqlite3.c warning suppression.
- **Hot reload thread** is skipped in packaged mode (no source directory).

### What does not work yet

- **MSVC compatibility is unverified and not a priority.** The codebase uses
  GCC/Clang `__attribute__` extensions (`malloc`, `format(printf, ...)`) in
  `allocators.h`, `stringlib.h`, and likely elsewhere. MinGW uses GCC so this
  is not a problem for cross-compilation. Native MSVC builds would require a
  porting pass, but since the engine targets distribution (not Windows
  development), this is deferred indefinitely.
- **`--sfx` shells out to `7z`** using `system()` with sh syntax. Only works
  on Linux. See [[Bug fixes and minor improvements]] for portable alternatives.
- **No `--target` convenience flag** yet (Phase 3).
- **No CI release builds** yet (Phase 4).

## Design

### Phase 1: Cross-platform packaging (decouple packer from binary) — DONE

`game package` accepts `--engine-binary <path>` to use a pre-built engine
binary for the target platform. Additional flags: `--strip` to strip debug
symbols, `--sfx` to produce a self-extracting .7z.exe archive.

```
game package games/flappybird --engine-binary build-win64/game.exe \
  -o dist-win64 --strip --sfx
```

When `--engine-binary` is provided, the packager:
- Copies that binary instead of `GetExePath()` result.
- Detects `.exe` extension and auto-copies runtime DLLs (SDL3.dll,
  libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll) from the same
  directory as the source binary.
- Skips `MakeExecutable()` for Windows targets.

When `--engine-binary` is _not_ provided, behavior is unchanged (copies self).

**Output structure:**

```
dist-win64/
  flappybird.exe
  assets.sqlite3
  SDL3.dll
  libgcc_s_seh-1.dll
  libstdc++-6.dll
  libwinpthread-1.dll
```

With `--sfx`, additionally produces `dist-win64/flappybird.7z.exe` — a
self-extracting archive that bundles the 7-Zip SFX stub, config, and a 7z
archive of the output directory. Run `scripts/setup-7z-sfx.sh` to download
the SFX stub.

### Phase 2: MinGW cross-compilation toolchain — DONE

CMake toolchain file at `cmake/mingw-w64-x86_64.cmake`. The MinGW toolchain
is installed via `scripts/setup-mingw-toolchain.sh` (xpack GCC 14.2.0-1,
pre-built for Linux x86_64). On NixOS, the script patches all ELF binaries
with patchelf to set the Nix dynamic linker and RPATH.

Build with `game-build-win64` (devenv script) or manually:

```sh
scripts/setup-mingw-toolchain.sh   # one-time toolchain download
cmake -G Ninja -S . -B build-win64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
ninja -C build-win64
```

The `game-build-win64` script also copies MinGW runtime DLLs into the build
directory after a successful build.

#### SDL3 vendored source build

SDL3 is vendored as source in `libraries/SDL3/` and built via
`add_subdirectory()` for both Linux and Windows. SDL3's CMake build
auto-detects the target platform from the toolchain file. The same
`SDL3::SDL3` target works for both native and cross-compiled builds.

#### CMakeLists.txt cross-compilation support

- `add_subdirectory(libraries/SDL3)` replaces `find_package(SDL3)`.
- OpenGL: MinGW sysroot provides `libopengl32.a`. The toolchain file sets
  `OPENGL_gl_LIBRARY` to `opengl32`.
- libdw (backward-cpp): gated on `CMAKE_SYSTEM_NAME STREQUAL "Linux"`.
  On Windows, backward-cpp links `dbghelp` instead.
- SDL3.dll is copied post-build via `$<TARGET_FILE:SDL3::SDL3>`.
- Vendored sqlite3.c compiles with `-Wno-error` to suppress MinGW warnings.

#### Portability fixes applied

- `libraries/backward.h`: Fixed `psapi.h` include order (must come after
  `windows.h`), guarded with `// clang-format off` to prevent re-sorting.
- `libraries/pcg_random.h`: Replaced `throw std::logic_error(...)` with
  `abort()` for `-fno-exceptions` compatibility.
- `src/hot_reload.cc`: Early-return from `Start()` when `source_directory_`
  is null (packaged mode), avoiding a useless file watcher thread.

#### Nix integration

- MinGW toolchain is installed separately (not via Nix) due to a nixpkgs
  GCC 15 build bug. See `scripts/setup-mingw-toolchain.sh`.
- `devenv.nix` provides `p7zip` (for `--sfx`), `wine` (for testing), and
  `patchelf` (for NixOS toolchain patching).
- `game-build-win64` devenv script wraps the cross-compilation build.

### Phase 3: `game package --target windows`

With phases 1 and 2 in place, add a convenience `--target` flag:

```
game package --target windows --output dist-win64
```

This would:
1. Check for a pre-built Windows binary at `build-win64/game` (or a
   configurable path).
2. If not found, invoke the MinGW cross-build automatically.
3. Copy the Windows binary + SDL3.dll + assets.sqlite3 to the output dir.
4. Optionally produce a `.zip` for easy transfer.

### Phase 4: CI release builds

Add a GitHub Actions workflow that builds engine binaries for each platform on
every tagged release. The workflow would:

1. **Build for each target platform:**
   - Linux x86_64 (native build on ubuntu runner)
   - Windows x86_64 (MinGW cross-compilation or native MSYS2 build)
   - macOS (native build on macos runner, when supported)

2. **Upload binaries as release assets.** Each platform produces a zip
   containing the engine binary and any required shared libraries (e.g.,
   `SDL3.dll` for Windows). Asset names follow the pattern
   `game-<platform>-<arch>.zip`.

3. **Smoke test.** Verify each binary is valid (PE/ELF headers, `--version`
   output via Wine for Windows builds).

This enables a zero-toolchain workflow: `game package --target windows` can
download the matching engine binary from the GitHub release instead of
requiring a local MinGW toolchain. The engine version in the release must
match the local `game` version to ensure compatibility.

**Benefits:**
- No local cross-compilation toolchain needed for end users
- Reproducible builds from CI
- Scales to new platforms without local setup
- Binary cache: download once, reuse for every `game package` invocation

## Platform-specific concerns

### OpenGL compatibility

The engine uses OpenGL 3.3 core profile (via GLAD). Windows has native OpenGL
support through GPU drivers. No compatibility issues expected for desktop
Windows.

### Audio

QOA decoding is pure C. SDL audio callback handles platform differences.
No issues expected.

### File paths

PhysFS abstracts path separators. `platform.cc` already has Windows
implementations using `\\` separators where needed. The engine stores asset
paths with `/` in SQLite, which PhysFS handles on all platforms.

### Hot reload

`file_watcher.h` documents `ReadDirectoryChangesW` as the Windows backend.
Hot reload should work on Windows, though it's not the primary use case for
cross-compiled packages (users will primarily package release builds).

### Single-file packaging

The single-file packaging system (SQLite DB appended to binary with magic
footer) is platform-agnostic. It should work identically on Windows since it
only depends on being able to read the binary's own path and do file I/O,
both of which `platform.cc` handles.

## Alternatives considered

### MSVC cross-compilation

MSVC doesn't support cross-compilation from Linux. Would require a Windows
machine or VM. Additionally, the codebase uses GCC/Clang `__attribute__`
extensions (`malloc`, `format`) that would need compatibility macros or
`#ifdef` guards before MSVC could compile it. MinGW avoids this entirely
since it uses GCC, making it the only viable cross-compilation path.

### Zig as cross-compiler

Zig's `cc` can cross-compile C/C++ to Windows with zero setup. However:
- The engine uses C++17 features that zig cc may not fully support.
- Would add Zig as a build dependency.
- MinGW is better established and available in Nix.

Worth revisiting if MinGW proves problematic.

### Docker with Windows cross-compilation

A Docker image with MinGW pre-installed. Adds container overhead but provides
reproducible builds. Could be useful for CI but overkill for local development
when Nix already manages the toolchain.

### Ship assets separately, build on target

Instead of cross-compiling, just package the assets and require the user to
build the engine on the target machine. This defeats the purpose: the goal is
zero-setup on the target machine.

## Implementation order

0. ~~**Vendor SDL3 source**~~ DONE. SDL3 is vendored and building via
   `add_subdirectory()`. Stale SDL2 libraries deleted.
1. ~~**Phase 1**~~ DONE. `--engine-binary`, `--strip`, `--sfx` flags.
   Auto-copies runtime DLLs. Tested on Windows with flappybird and Space
   Garbage!.
2. ~~**Phase 2**~~ DONE. MinGW toolchain setup script with NixOS patchelf
   support, CMake toolchain file, `game-build-win64` devenv script,
   portability fixes.
3. **Phase 3** for convenience (ties it all together into one command).
4. **Phase 4** CI release builds, enabling zero-toolchain cross-platform
   packaging via downloaded binaries.

---

## macOS support

### Current readiness

The codebase is architecturally well-positioned for macOS. Most
platform-specific code already exists:

| Component | Status | Notes |
|-----------|--------|-------|
| `platform.cc` | ✅ Done | `GetExePath` uses `_NSGetExecutablePath`, `GetUserCacheDir` uses `~/Library/Caches` |
| `thread.h` | ✅ Done | macOS `pthread_setname_np(name)` (single-arg variant) |
| `file_watcher.cc` | ⚠️ Stubbed | FSEvents design documented, polling fallback works |
| SDL3 | ✅ Vendored | Full macOS support (Cocoa, CoreAudio, IOKit) |
| backward.h | ✅ Done | Detects `BACKWARD_SYSTEM_DARWIN` |
| All vendored libs | ✅ Portable | Box2D, Lua, SQLite, mimalloc, etc. |

### Blocking issue: OpenGL version

The engine requests an **OpenGL 4.6** context and uses `#version 460 core`
shaders. **macOS supports OpenGL 4.1 maximum** (deprecated since 10.14 but
still functional on both Intel and Apple Silicon via a Metal translation
layer).

However, the engine's actual GL usage fits within **OpenGL 3.3**:

- No DSA calls (GL 4.5)
- No compute shaders (GL 4.3)
- No buffer storage (GL 4.4)
- `glDebugMessageCallback` (GL 4.3) is already guarded behind
  `GLAD_GL_VERSION_4_3 && GLAD_GL_KHR_debug`
- All shader features (`layout(location)`, `dFdx`/`dFdy`, `smoothstep`)
  are GLSL 3.30

**Fix:** Change `SDL_GL_CONTEXT_MAJOR_VERSION` from 4 to 3 (or 4/1),
change all `#version 460 core` to `#version 330 core` (or `#version 410
core`), and regenerate GLAD for the lower version. This is a small,
mechanical change (~10 lines).

**Alternatives:**
- [MGL](https://github.com/openglonmetal/MGL) — OpenGL 4.6 on Metal
  (incomplete, macOS-only)
- [ANGLE](https://chromium.googlesource.com/angle/angle) — OpenGL ES on
  Metal (production-grade, but requires ES porting)
- SDL3 GPU API — cross-platform Metal/Vulkan/D3D12 abstraction (requires
  renderer rewrite)

### Cross-compilation: osxcross

[osxcross](https://github.com/tpoechtrager/osxcross) wraps Clang with
Apple's cctools and macOS SDK headers/libraries. Produces binaries like
`arm64-apple-darwin22-clang++`.

**Setup:**
1. Obtain a macOS SDK (extract from Xcode.xip or from a Mac)
2. Build osxcross: `./build.sh`
3. Add `<osxcross>/bin` to PATH

**CMake toolchain file** (same pattern as `cmake/mingw-w64-x86_64.cmake`):
```cmake
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)

get_filename_component(_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_OSXCROSS_ROOT "${_PROJECT_ROOT}/toolchains/osxcross")

set(CMAKE_C_COMPILER "${_OSXCROSS_ROOT}/bin/arm64-apple-darwin22-clang")
set(CMAKE_CXX_COMPILER "${_OSXCROSS_ROOT}/bin/arm64-apple-darwin22-clang++")

set(CMAKE_FIND_ROOT_PATH "${_OSXCROSS_ROOT}/SDK/MacOSX14.0.sdk")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
```

**Limitations:**
- Cannot code-sign or notarize (requires macOS-only `codesign` tool)
- Apple SDK license is a legal gray area for Linux usage
- Cannot run tests (no macOS runtime on Linux)

**Zig as cross-compiler:** Not viable for this project. `zig cc` can
target macOS but doesn't ship framework headers (Cocoa, CoreAudio, IOKit)
that SDL3 requires. You'd still need the macOS SDK, at which point
osxcross is more complete.

### Recommended approach: GitHub Actions

**Native macOS CI runners are the recommended path** for release builds.
This is what most game engines (Godot, Love2D) do.

```yaml
jobs:
  build-macos:
    runs-on: macos-14  # Apple Silicon M1
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: true
      - name: Build
        run: |
          cmake -G Ninja -S . -B build \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
          cmake --build build
```

**Advantages over cross-compilation:**
- Native compilation — no toolchain setup or SDK extraction
- Code signing and notarization work natively
- Can run tests on actual macOS
- Universal binaries via `CMAKE_OSX_ARCHITECTURES` just work
- Free for public repositories (including M1 runners)

### App bundle structure

macOS apps are `.app` directories:

```
MyGame.app/
  Contents/
    Info.plist           # Required metadata
    MacOS/
      game               # Executable
    Resources/
      game.icns          # App icon
      assets.db          # Game assets
```

CMake can generate app bundles with:
```cmake
set_target_properties(Game PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Game"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.game"
)
```

### Code signing and distribution

- **Unsigned apps work** but users must right-click → Open to bypass
  Gatekeeper, or run `xattr -d com.apple.quarantine MyGame.app`
- **Signed + notarized** is the friction-free path, requires an Apple
  Developer account ($99/year) and the `codesign`/`notarytool` tools
  (macOS only)
- Hardened Runtime (`codesign -o runtime`) is required for notarization

### Architecture

- **arm64 (Apple Silicon)** is the primary target — all Macs since late
  2020
- **x86_64 (Intel)** Macs are EOL but still in use
- **Universal binaries** contain both architectures:
  `cmake -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`
- SDL3's CMakeLists.txt already handles `CMAKE_OSX_ARCHITECTURES`

### Implementation plan

1. **GL version downgrade** — change context request and shader versions
   from 4.6 to 4.1 (or 3.3). Regenerate GLAD. ~10 lines.
2. **CMakePresets.json** — add a `macos` preset.
3. **GitHub Actions CI** — add `macos-14` runner job for automated builds.
4. **App bundling** — `game package` support for `.app` creation.
5. **FSEvents file watcher** — implement the macOS backend for hot reload
   (optional, polling fallback works).
6. **Code signing** — CI pipeline for signed+notarized distribution
   (optional, unsigned apps work with manual bypass).
