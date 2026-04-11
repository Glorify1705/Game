---
status: in-design
tags: [build, cross-compilation, packaging, portability]
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

### Concrete issues

| Issue | Where | Impact |
|---|---|---|
| No CMake toolchain file for cross compilation | `CMakeLists.txt` | Can't target Windows from Linux |
| `game package` copies the _host_ binary | `cmd_package.cc:96-110` | Packaged output is always a Linux binary |
| No way to specify a target platform | `cmd_package.cc` | CLI has no `--target` flag |
| SDL2 Windows libs are vendored but unused | `libraries/SDL2/x86_64-w64-mingw32/` | Pre-built MinGW libs sit idle |
| No CI for Windows builds | — | Regressions go unnoticed |

## Current state

### What already works

The project is surprisingly close to cross-compilation readiness:

- **SDL2 Windows libraries are vendored.** Both 32-bit and 64-bit MinGW
  pre-built libraries exist at `libraries/SDL2/x86_64-w64-mingw32/` and
  `libraries/SDL2/i686-w64-mingw32/`, complete with CMake config files.
- **All other dependencies are vendored** and written in portable C/C++:
  Box2D, Lua 5.1, PhysFS, mimalloc, SQLite3, stb_*, pugixml, GLAD,
  double-conversion.
- **Platform abstraction is clean.** `platform.cc` has complete `#ifdef _WIN32`
  implementations for file operations, directory traversal, path handling, and
  executable detection. Thread naming in `thread.h` handles Windows.
  `file_watcher.h` documents per-platform backends.
- **CMakeLists.txt has MSVC flags.** The build system already distinguishes
  MSVC from GCC/Clang with appropriate warning flags.
- **Asset format is platform-agnostic.** SQLite databases and the asset schema
  have no platform-specific content.

### The blocker: `game package` copies itself

`cmd_package.cc` calls `GetExePath()` to find its own binary, then copies it
into the output directory (lines 96-110). When packaging on Linux, this always
produces a Linux binary. There is no mechanism to substitute a pre-built
Windows binary.

### What needs to happen

There are two separable problems:

1. **Cross-compiling the engine binary** for Windows (build system work).
2. **Packaging a game** with a cross-compiled binary (CLI/workflow work).

These can be tackled independently. Problem 2 is the simpler and more
immediately useful one: if you have a Windows `.exe` of the engine (built via
MinGW cross-compilation, or built once on a Windows machine), you should be
able to package a game for Windows from Linux.

## Design

### Phase 1: Cross-platform packaging (decouple packer from binary)

Modify `game package` to accept a `--engine-binary` flag that specifies a
pre-built engine binary for the target platform, instead of copying itself.

```
game package --engine-binary build-win64/game.exe --output dist-win64
```

When `--engine-binary` is provided:
- Copy that binary instead of `GetExePath()` result.
- Skip `MakeExecutable()` and `strip` (caller handles these, or add
  `--no-strip`).
- The asset packing step is unchanged (SQLite DB is platform-agnostic).

When `--engine-binary` is _not_ provided, behavior is unchanged (copies self).

This is a small, self-contained change to `cmd_package.cc` — roughly 10 lines.

**Output structure** (same as today, just with a Windows binary):

```
dist-win64/
  flappybird.exe
  assets.sqlite3
  SDL3.dll           # copied from vendored libs
```

Note: SDL3.dll must also be included. The CMakeLists.txt already has a
post-build copy step for Windows (line 242-250). The packager should also
copy required DLLs when targeting Windows.

### Phase 2: MinGW cross-compilation toolchain

Add a CMake toolchain file at `cmake/mingw-w64-x86_64.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

Build with:

```sh
cmake -G Ninja -S . -B build-win64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
ninja -C build-win64
```

#### CMakeLists.txt changes needed

The main CMakeLists.txt needs minor adjustments for cross compilation:

1. **SDL2/SDL3 discovery.** When cross-compiling, `find_package(SDL3)` must
   search the vendored MinGW libraries. Set `SDL3_DIR` or `CMAKE_PREFIX_PATH`
   in the toolchain file to point at `libraries/SDL2/x86_64-w64-mingw32/`.
   (The vendored CMake configs already handle architecture selection.)

2. **OpenGL.** `find_package(OpenGL REQUIRED)` needs a MinGW-compatible
   `libopengl32.a`. MinGW sysroots typically include this. May need
   `set(OPENGL_gl_LIBRARY opengl32)` in the toolchain file.

3. **libdw.** The Linux-only `find_library(LIBDW dw)` block (used for
   backward-cpp stack traces) should be gated on `CMAKE_SYSTEM_NAME STREQUAL
   "Linux"`, which it already partially is.

4. **backward-cpp.** On Windows, backward-cpp uses `dbghelp.h` instead of
   libdw. May need `target_link_libraries(... dbghelp)` on Windows.

5. **DLL copying.** The post-build SDL3.dll copy step should use the vendored
   DLL path rather than assuming a system install.

#### Nix integration

Add a MinGW cross-compilation environment to `devenv.nix`:

```nix
packages = [
  pkgs.pkgsCross.mingwW64.buildPackages.gcc
  pkgs.pkgsCross.mingwW64.windows.pthreads
];
```

Or provide a `game-build-win64` script that invokes cmake with the toolchain
file.

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

### Phase 4: CI for Windows builds (optional)

Add a GitHub Actions workflow that cross-compiles with MinGW and runs basic
smoke tests (e.g., `game.exe --help` via Wine, or just verify the binary is a
valid PE executable).

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
machine or VM. MinGW is the standard approach for Linux-to-Windows.

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

1. **Phase 1** first (small `cmd_package.cc` change). This unblocks the
   workflow immediately: cross-compile once with MinGW manually, then use
   `game package --engine-binary` repeatedly as you iterate on game scripts.
2. **Phase 2** next (toolchain file + CMake adjustments). Makes the cross
   build reproducible and scriptable.
3. **Phase 3** for convenience (ties it all together into one command).
4. **Phase 4** if/when Windows testing becomes important for CI.
