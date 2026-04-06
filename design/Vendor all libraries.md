---
status: implemented
tags: [build, dependencies]
---

# Vendor All Libraries

## Motivation

Git submodules (`box2d`, `googletest`, `json.cpp`) break in worktrees because they aren't initialized automatically. Rather than scripting around this, vendor everything directly into the repo and strip unnecessary files (tests, docs, CI, examples) to keep the tree lean.

## Current State

| Library | Integration | Notes |
|---|---|---|
| box2d | Submodule (empty) | Never initialized |
| googletest | Submodule (empty) | Never initialized |
| json.cpp | Submodule (orphaned) | Already vendored as `json.cc`/`json.h` |
| qoiview | Submodule in `tools/` | Unused, not in build |
| mimalloc | Committed source | Has 5+ MB of removable docs/tests/IDE files |
| physfs | Committed source | Has removable docs/tests/extras |
| lua | Committed source | Has removable docs/tests/standalone tools |
| SDL2 | Pre-built Windows binaries | 41 MB test suite can go |
| double-conversion | Committed source | Already minimal, nothing to remove |
| Single-file libs | Committed `.h`/`.cc` | Already minimal |

## Steps

### 1. Remove dead submodule references

Remove entries from `.gitmodules` and `.git/config` for submodules that will be vendored or are unused:

```bash
git rm --cached libraries/box2d
git rm --cached libraries/googletest
# json.cpp entry in .gitmodules is orphaned (dir doesn't exist)
# qoiview is unused
```

Delete `.gitmodules` entirely (all 4 entries will be vendored or removed). Remove the empty `tools/` directory.

### 2. Vendor box2d

Clone `https://github.com/Glorify1705/box2d.git` at commit `7de5281` into `libraries/box2d/`.

**Keep:**
- `CMakeLists.txt` (root)
- `include/box2d/` (all headers)
- `src/` (collision/, common/, dynamics/, rope/)
- `LICENSE`

**Remove:**
- `testbed/` — interactive GUI demo
- `unit-test/` — test suite
- `docs/` — Doxygen documentation
- `extern/` — glad, glfw, imgui, sajson (testbed dependencies)
- `.github/` — CI workflows
- `build.bat`, `build.sh`, `build_docs.sh`, `deploy_docs.sh`
- `CHANGELOG.md`, `README.md`
- `.gitignore`

### 3. Vendor googletest

Clone `https://github.com/Glorify1705/googletest.git` at commit `504ea69` into `libraries/googletest/`.

**Keep:**
- `CMakeLists.txt` (root)
- `googletest/CMakeLists.txt`
- `googletest/cmake/` (internal_utils.cmake, Config.cmake.in, .pc.in files)
- `googletest/include/gtest/` (all headers including `internal/`)
- `googletest/src/` (all .cc files)
- `googlemock/CMakeLists.txt`
- `googlemock/cmake/`
- `googlemock/include/gmock/` (all headers including `internal/`)
- `googlemock/src/` (all .cc files)

**Remove:**
- `docs/` — all documentation
- `googletest/samples/` — example code
- `googletest/test/` — gtest's own tests (~120 files)
- `googlemock/test/` — gmock's own tests
- `ci/` — CI scripts
- Bazel files (`BUILD.bazel`, `MODULE.bazel`, `WORKSPACE*`, `*.bzl`)
- `.github/`
- `README.md`, `CONTRIBUTING.md`, `CONTRIBUTORS`
- `.clang-format`, `.gitignore`

### 4. Clean up json.cpp

The `json.cc`/`json.h` files in `libraries/` are already vendored directly — the `json.cpp` submodule entry in `.gitmodules` is orphaned. Just remove the `.gitmodules` entry (handled in step 1).

### 5. Remove qoiview

Not referenced in the build system. Remove the submodule entry and the empty `tools/` directory.

### 6. Trim mimalloc

Currently 6.3 MB. Can drop to ~1 MB.

**Keep:**
- `CMakeLists.txt`
- `include/` (all headers)
- `src/` (all source including `prim/` platform subdirs)
- `LICENSE`

**Remove:**
- `test/` — test programs (MI_BUILD_TESTS=OFF)
- `doc/` — Doxygen source (~2.5 MB)
- `docs/` — generated HTML docs (~2.1 MB)
- `bin/` — pre-compiled Windows redirect DLLs
- `contrib/` — vcpkg, Docker files
- `ide/` — Visual Studio project files
- `azure-pipelines.yml`
- `readme.md`, `SECURITY.md`
- `mimalloc.pc.in`
- `.clang-format`, `.gitattributes`, `.gitignore`

### 7. Trim physfs

**Keep:**
- `CMakeLists.txt`
- `src/` (all source files — archivers, platform code, headers)
- `LICENSE.txt`

**Remove:**
- `docs/` — Doxygen config, changelogs, credits
- `extras/` — example code, utility programs, buildbot scripts, SDL RWops wrapper
- `test/` — test program
- `.github/` — CI workflows
- `README.txt`, `.gitignore`
- `Makefile.os2`

### 8. Trim lua

**Keep:**
- `CMakeLists.txt`
- `src/` core files (everything compiled by CMakeLists.txt + headers)
- `COPYRIGHT` (MIT license)

**Remove from `src/`:**
- `lua.c` — standalone interpreter entry point (not compiled by CMake)
- `luac.c` — bytecode compiler (not compiled)
- `print.c` — bytecode printer for luac (not compiled)
- `Makefile` — redundant with CMake

**Remove directories:**
- `doc/` — HTML documentation
- `etc/` — examples and utilities
- `test/` — Lua test scripts
- `Makefile` (root), `README`, `INSTALL`, `HISTORY`

### 9. Trim SDL2

SDL2 is a system dependency on Linux but ships pre-built Windows binaries. The test suite is massive and unused.

**Keep:**
- `cmake/` (sdl2-config.cmake, sdl2-config-version.cmake)
- `i686-w64-mingw32/include/`, `i686-w64-mingw32/lib/`, `i686-w64-mingw32/bin/`
- `x86_64-w64-mingw32/include/`, `x86_64-w64-mingw32/lib/`, `x86_64-w64-mingw32/bin/`
- `COPYING.txt` (zlib license)

**Remove:**
- `test/` — full SDL2 test suite (~41 MB, mostly shape images)
- `docs/` — platform-specific READMEs
- `BUGS.txt`, `CREDITS.txt`, `INSTALL.txt`, `README.txt`, `README-SDL.txt`, `WhatsNew.txt`
- `Makefile`
- `*.la` libtool files in both platform dirs (CMake doesn't use them)
- `libSDL2_test.a` in both platform dirs (test library, not linked)
- `share/` directories (aclocal m4 macros)

### 10. Verify build

After all changes:

```bash
# Clean build from scratch
rm -rf build && mkdir build && cd build
cmake .. && make -j$(nproc)

# Verify tests still compile
make Tests

# Test in a fresh worktree to confirm the original problem is fixed
git worktree add ../test-worktree
cd ../test-worktree
mkdir build && cd build && cmake .. && make -j$(nproc)
git worktree remove ../test-worktree
```

## Summary of space savings

| Library | Before | After | Saved |
|---|---|---|---|
| box2d | submodule (~15 MB full) | ~3 MB | ~12 MB |
| googletest | submodule (~300+ files) | ~70 files | ~230 files |
| mimalloc | 6.3 MB | ~1 MB | ~5.3 MB |
| SDL2 | 91 MB | ~50 MB | ~41 MB |
| physfs | 1.2 MB | ~0.8 MB | ~0.4 MB |
| lua | 1.1 MB | ~0.6 MB | ~0.5 MB |
| qoiview | submodule (unused) | removed | — |
| json.cpp | orphaned submodule | removed | — |

Total estimated savings: ~59 MB, plus worktree builds just work.
