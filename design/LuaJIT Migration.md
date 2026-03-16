# Migrating from Lua 5.1 to LuaJIT

**Status: Postponed.** After analysis, the migration is not worth the added complexity at this time. The engine's Lua scripts are primarily glue code that dispatches to C++ — the JIT would accelerate very little of the actual frame time. The build complexity, single-maintainer risk, and debugging downsides outweigh the speculative performance benefit. Revisit if profiling shows Lua execution as a bottleneck.

## Overview

This document covers migrating the game engine's scripting backend from the vendored Lua 5.1.5 (at `libraries/lua/`) to LuaJIT 2.1. LuaJIT is a drop-in replacement for Lua 5.1 with a JIT compiler that provides 10-100x speedups for compute-heavy Lua code. It also ships with an FFI library that allows Lua code to call C functions and access C data structures directly, without crossing the Lua/C API boundary.

## Motivation

- **Performance**: LuaJIT's tracing JIT compiler generates native machine code for hot paths. Per-frame `update()` and `draw()` logic, entity iteration, vector math done in Lua, and physics callbacks all benefit.
- **FFI**: The FFI library lets game scripts access C structs directly. This could eliminate the overhead of userdata for types like `vec2`/`vec3`/`vec4` in the future.
- **Ecosystem**: Many Lua game libraries (e.g., LOVE2D's ecosystem) are optimized for or require LuaJIT.
- **Compatibility**: LuaJIT implements the Lua 5.1 API and ABI. Our engine already targets Lua 5.1 exclusively, so the C API surface is compatible.

## Reasons not to migrate

### The engine is probably not Lua-bottlenecked

The hot path each frame is: poll input (C++/SDL), step physics (C++/Box2D), call `update()` (Lua), call `draw()` (Lua which immediately calls back into C++ rendering). The actual work in `update()` and `draw()` is dominated by C API calls — `G.graphics.draw_sprite`, `G.physics.add_box`, etc. — which cross into C++ immediately. LuaJIT's JIT compiler accelerates *Lua-side* computation (loops, arithmetic, table access), but if most frames spend their time in C++ behind API calls, the real-world speedup could be negligible. The 10-100x claim applies to compute-heavy pure Lua; the engine's Lua scripts are mostly glue code dispatching to C++.

You would need to profile a real game to know whether Lua execution time is actually a bottleneck. If it's 2% of frame time, a 10x speedup there saves 1.8% — invisible.

### LuaJIT is a single-maintainer project

LuaJIT is maintained by Mike Pall, essentially alone. Development has slowed significantly — the `v2.1` branch is rolling-release with no tagged stable versions since 2017. If Mike steps away, the project has no succession plan. PUC Lua (while also a small team at PUC-Rio) has institutional backing and continues to release new versions (5.4.7 as of 2024).

Taking a dependency on LuaJIT means accepting that critical bugs or platform support issues may not be fixed upstream, or may require adopting a community fork (e.g., OpenResty's LuaJIT fork).

### Locks the language level to Lua 5.1 permanently

LuaJIT implements Lua 5.1 only. It selectively backports a few 5.2 features (e.g., `goto`, some `string` functions) but does not and will not support Lua 5.3+ features like:

- Native 64-bit integers (Lua 5.3+)
- Bitwise operators (`&`, `|`, `~`, `<<`, `>>`) as syntax (Lua 5.3+)
- `utf8` standard library (Lua 5.3+)
- Generalized `for` with `<close>` variables (Lua 5.4+)
- Warning system (Lua 5.4+)

The engine currently targets Lua 5.1 and this is fine today. But adopting LuaJIT makes it structurally harder to ever upgrade to a newer Lua version, because game scripts and libraries would start depending on LuaJIT-specific features (FFI, `bit` library, JIT behavior) that don't exist in PUC Lua 5.3/5.4.

With PUC Lua, upgrading from 5.1 to 5.4 is a realistic future option. With LuaJIT, it is not.

### Build complexity increases substantially

PUC Lua 5.1 is 30 C files compiled with a trivial `CMakeLists.txt` — it builds everywhere CMake works with zero special handling. LuaJIT's build involves:

1. Compiling `minilua` (a stripped-down Lua interpreter)
2. Running `minilua` to process DynASM files and generate platform-specific assembly
3. Compiling `buildvm` (a code generator)
4. Running `buildvm` to emit `lj_bcdef.h`, `lj_ffdef.h`, `lj_libdef.h`, `lj_recdef.h`, `lj_folddef.h`, and a platform-specific `lj_vm.S` or `lj_vm.obj`
5. Compiling the actual LuaJIT library

This multi-stage build doesn't have official CMake support. The `ExternalProject_Add` approach delegates to LuaJIT's Makefile, which works but means:
- Build errors in LuaJIT are harder to diagnose (they happen inside an opaque external build)
- Cross-compilation requires careful environment variable setup
- Windows needs a completely different build command (`msvcbuild.bat`)
- Clean rebuilds require cleaning LuaJIT's source tree (since we build in-source)

This is manageable complexity, but it's real complexity compared to the current 40-line `libraries/lua/CMakeLists.txt`.

### Debugging Lua scripts becomes harder

LuaJIT's JIT compiler interferes with some debugging workflows:

- **`debug.getinfo`/`debug.getlocal`** may return incomplete information for JIT-compiled frames. Our `FillLogLine()` function in `lua.cc` calls `lua_getstack`/`lua_getinfo` to extract file and line info for log messages — these work at the C API level but may report less accurate information when the JIT is active.
- **GDB/debugger integration**: JIT-compiled Lua code runs as generated machine code, not as C function calls. Breakpoints in C binding functions still work, but stepping through Lua logic in a debugger is not possible the way it is with the interpreter.
- **Determinism**: JIT compilation introduces non-deterministic frame-time spikes when traces are first compiled. A function might run fast for 100 frames, then stall for a frame while the JIT compiles it, then run faster. This makes profiling harder and can cause visible hitches in a game.
- **Trace aborts ("NYI")**: Certain Lua patterns cause the JIT to give up and fall back to the interpreter. This is silent — there's no error — but it means performance depends on whether your code happens to use patterns the JIT supports. Diagnosing "why is this loop slow" requires understanding LuaJIT's trace compiler internals and using `jit.v` to inspect trace output.

### GC memory ceiling

LuaJIT's garbage collector has a hard memory limit: ~2 GB without GC64, ~4 GB with GC64. PUC Lua has no such limit. Our current Lua allocator arena is 64 MB, so this isn't an issue today, but it's a ceiling that doesn't exist with PUC Lua. A game that loads many large assets into Lua tables or generates large data structures could hit it.

### Fennel-generated code may not JIT well

Fennel compiles to Lua source that is semantically correct but may use patterns the JIT doesn't optimize well. Fennel's pattern matching, destructuring, and macro expansion can generate deeply nested function calls, excessive table creation, or patterns that trigger trace aborts. The code will always *run correctly* (it falls back to the interpreter), but you wouldn't get the JIT performance benefit for Fennel-heavy codebases without auditing the generated Lua.

### It's one more configuration to test

Even with macOS excluded, adding a `USE_LUAJIT` flag means every change must work under two Lua backends. CI time doubles for the Lua-related matrix. Bugs that only reproduce under one backend are possible. If both backends must be supported indefinitely, this is ongoing maintenance cost. If LuaJIT eventually becomes the only backend, there's a migration period where both must work.

## Current Architecture

### How Lua is embedded

The engine embeds Lua as a static library built from source at `libraries/lua/src/` (Lua 5.1.5, 55 C source files). CMake compiles these into a `lua` static library target and the main `Game` executable links against it.

**VM lifecycle** (`src/lua.cc`):
1. `lua_newstate(&Lua::LuaAlloc, this)` — creates VM with a custom allocator
2. Standard libraries loaded: `base`, `package`, `table`, `string`, `math`, `debug`
3. Engine APIs registered under global `G` table via `AddLibrary()` / `AddLibraryWithMetadata()`
4. Scripts loaded via `luaL_loadbuffer()` + `lua_pcall()`
5. Per-frame: `lua_getglobal(state_, "_Game")` then call `update`/`draw` methods

**Custom memory allocator** (`src/lua.cc:152-162`):
```cpp
void* Lua::Alloc(void* ptr, size_t osize, size_t nsize) {
  allocator_stats_.AddSample(nsize);
  if (nsize == 0) {
    if (ptr != nullptr) allocator_->Dealloc(ptr, osize);
    return nullptr;
  }
  if (ptr == nullptr) {
    return allocator_->Alloc(nsize, /*align=*/1);
  }
  return allocator_->Realloc(ptr, osize, nsize, /*align=*/1);
}
```

The Lua VM gets a 64 MB `MimallocAllocator` arena (`game.cc:178-179`). The allocator is passed via `lua_newstate()`'s `lua_Alloc` callback.

### C API usage

The engine uses these Lua 5.1 C API functions extensively:

| Category | Functions used |
|----------|--------------|
| State | `lua_newstate`, `lua_close`, `lua_atpanic` |
| Stack | `lua_gettop`, `lua_settop`, `lua_pushvalue`, `lua_insert`, `lua_pop` |
| Push | `lua_pushnil`, `lua_pushnumber`, `lua_pushinteger`, `lua_pushboolean`, `lua_pushstring`, `lua_pushlstring`, `lua_pushcfunction`, `lua_pushlightuserdata` |
| Get | `lua_tonumber`, `lua_tointeger`, `lua_toboolean`, `lua_tostring`, `lua_touserdata`, `lua_tocfunction`, `lua_type`, `lua_typename` |
| Tables | `lua_newtable`, `lua_getfield`, `lua_setfield`, `lua_gettable`, `lua_settable`, `lua_rawseti`, `lua_next`, `lua_getmetatable`, `lua_setmetatable` |
| Registry | `LUA_REGISTRYINDEX`, `luaL_newmetatable`, `luaL_getmetatable`, `luaL_checkudata` |
| Execution | `lua_call`, `lua_pcall`, `luaL_loadbuffer`, `lua_error` |
| Check | `luaL_checknumber`, `luaL_checkinteger`, `luaL_checkstring`, `luaL_checklstring`, `lua_isnil`, `lua_istable`, `lua_isfunction`, `lua_isstring`, `lua_isboolean`, `lua_iscfunction` |
| Globals | `lua_getglobal`, `lua_setglobal`, `LUA_GLOBALSINDEX` |
| Userdata | `lua_newuserdata` |
| GC | `lua_gc` (`LUA_GCCOLLECT`, `LUA_GCCOUNT`, `LUA_GCCOUNTB`) |
| Debug | `lua_getstack`, `lua_getinfo` |
| Other | `lua_objlen` |

### Lua 5.1-specific APIs used

These functions exist in Lua 5.1 and LuaJIT but were removed in Lua 5.2+. Their use confirms that our codebase is firmly in Lua 5.1 territory, which is exactly what LuaJIT targets:

- `LUA_GLOBALSINDEX` — used in `lua.cc:466` for Fennel compilation
- `lua_objlen` — used in `lua_filesystem.cc:14`, `lua_graphics.cc:275`, `lua_random.cc:79,83`

### Userdata types

The engine creates full userdata objects for these types:

| Type | Metatable name | Source |
|------|----------------|--------|
| `FVec2` | `fvec2` | `lua_math.cc` |
| `FVec3` | `fvec3` | `lua_math.cc` |
| `FVec4` | `fvec4` | `lua_math.cc` |
| `FMat2x2` | `fmat2x2` | `lua_math.cc` |
| `FMat3x3` | `fmat3x3` | `lua_math.cc` |
| `FMat4x4` | `fmat4x4` | `lua_math.cc` |
| `ByteBuffer` | `byte_buffer` | `lua_bytebuffer.cc` |
| `Physics::Handle` | `physics_handle` | `lua_physics.cc` |
| `pcg32` (RNG) | `random_number_generator` | `lua_random.cc` |
| `DbAssets::Sprite` | `asset_sprite_ptr` | `lua_assets.cc` |

### Lua scripts in assets/

15 `.lua` files plus Fennel support. Third-party libraries: `classic.lua` (OOP), `lume.lua` (utilities), `fennel.lua` (compiler), `timer.lua`. All target Lua 5.1. `lume.lua` uses `unpack or table.unpack` for 5.1/5.2 compat. No Lua 5.2+ features used anywhere.

### Fennel support

Fennel scripts (`.fnl`) are compiled to Lua source at load time by the Fennel compiler (`assets/fennel.lua`), which itself is a Lua 5.1 module. The compiled output is cached in SQLite. Fennel's output is standard Lua 5.1 bytecode-compatible source — it will work under LuaJIT without changes.

## LuaJIT Compatibility Analysis

### API Compatibility: What works out of the box

LuaJIT 2.1 implements the full Lua 5.1 C API. Every function listed in the "C API usage" table above is present and has identical semantics. Specifically:

- `lua_newstate` with a custom `lua_Alloc` callback: **supported**
- `lua_atpanic`: **supported**
- `LUA_GLOBALSINDEX`: **supported** (removed in Lua 5.2, but LuaJIT keeps it)
- `lua_objlen`: **supported** (renamed to `lua_rawlen` in 5.2, but LuaJIT keeps `lua_objlen`)
- `lua_pushcfunction`: **supported**
- `luaL_loadbuffer`: **supported**
- All userdata APIs (`lua_newuserdata`, `luaL_checkudata`, etc.): **supported**
- `lua_gc` with `LUA_GCCOLLECT`/`LUA_GCCOUNT`/`LUA_GCCOUNTB`: **supported**

**Verdict**: All C API calls in the engine are compatible with LuaJIT. No source changes are needed for the binding code.

### What changes

1. **LuaJIT is not built from source the same way as PUC Lua.** LuaJIT has its own build system (a mix of `Makefile` and a DynASM-based code generator). It cannot simply be dropped into the existing `libraries/lua/CMakeLists.txt`.

2. **Header paths change.** LuaJIT headers are `luajit.h`, `lua.h`, `lauxlib.h`, `lualib.h`. The core three (`lua.h`, `lauxlib.h`, `lualib.h`) are API-compatible, but the include directory will be different.

3. **LuaJIT has a 1-2 GB GC memory limit** on 64-bit platforms (configurable up to ~4 GB with `LUAJIT_ENABLE_GC64` in LuaJIT 2.1). Our Lua allocator arena is 64 MB — well within limits.

4. **`lua_newstate` custom allocator**: LuaJIT supports this, but with a caveat — LuaJIT requires that the allocator returns pointers in the low 2 GB of address space (or 4 GB with GC64 mode). Our `MimallocAllocator` uses `mi_manage_os_memory_ex` on a pre-allocated arena. The arena's virtual address must be in the low address range, or GC64 mode must be enabled. On Linux, `malloc`/`mmap` typically returns low addresses for small allocations, but this is not guaranteed. **This is the most significant potential issue.**

5. **LuaJIT's `string.dump` produces different bytecode** than PUC Lua 5.1. If we ever dump/load bytecode across backends, this would be incompatible. Currently, the Fennel compilation cache stores Lua *source* strings (not bytecode), so this is not a concern.

6. **`debug.traceback` format** differs slightly. Our error handling in `lua.cc:SetError()` strips lines starting with `[C]` or `(tail call)` — LuaJIT uses the same markers, so this should work.

## Migration Plan

### Phase 1: Add LuaJIT as a CMake option (build system)

**Goal**: Build the engine with either Lua 5.1 or LuaJIT, selected at CMake configure time.

#### Step 1.1: Acquire LuaJIT source

Add LuaJIT 2.1 sources to the repository:

```bash
git subtree add --prefix=libraries/luajit \
  https://github.com/LuaJIT/LuaJIT.git v2.1 --squash
```

Alternatively, for Nix-based builds, use the `luajit` package directly. But since we vendor all other dependencies (Box2D, PhysFS, mimalloc, etc.), vendoring LuaJIT is more consistent.

The LuaJIT source tree contains:
- `src/` — LuaJIT core (C + DynASM files)
- `src/host/` — Build-time code generator (`minilua`, `buildvm`)
- `Makefile` — The canonical build system

#### Step 1.2: Build LuaJIT via CMake

LuaJIT does not ship with CMake support. There are two approaches:

**Option A: ExternalProject (recommended)**

Use CMake's `ExternalProject_Add` to invoke LuaJIT's native Makefile:

```cmake
# LuaJIT is only supported on Linux and Windows. On macOS (especially Apple
# Silicon) official LuaJIT support is limited, so we always fall back to PUC
# Lua there.
if(APPLE)
  set(USE_LUAJIT OFF CACHE BOOL "Use LuaJIT (not available on macOS)" FORCE)
  message(STATUS "LuaJIT is disabled on macOS — using PUC Lua 5.1")
else()
  option(USE_LUAJIT "Use LuaJIT instead of PUC Lua 5.1" OFF)
endif()

if(USE_LUAJIT)
  include(ExternalProject)

  if(WIN32)
    # On Windows, LuaJIT ships msvcbuild.bat for MSVC builds.
    ExternalProject_Add(luajit_build
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libraries/luajit
      CONFIGURE_COMMAND ""
      BUILD_COMMAND cmd /c
        "cd /d ${CMAKE_CURRENT_SOURCE_DIR}/libraries/luajit/src && msvcbuild.bat static"
      INSTALL_COMMAND ""
      BUILD_IN_SOURCE TRUE
      BUILD_BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/libraries/luajit/src/lua51.lib
    )
    set(LUAJIT_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libraries/luajit/src/lua51.lib)
  else()
    # Linux: use LuaJIT's Makefile with GC64 enabled.
    ExternalProject_Add(luajit_build
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libraries/luajit
      CONFIGURE_COMMAND ""
      BUILD_COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/libraries/luajit/src
        CC=${CMAKE_C_COMPILER}
        BUILDMODE=static
        "XCFLAGS=-DLUAJIT_ENABLE_GC64"
      INSTALL_COMMAND ""
      BUILD_IN_SOURCE TRUE
      BUILD_BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/libraries/luajit/src/libluajit.a
    )
    set(LUAJIT_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libraries/luajit/src/libluajit.a)
  endif()

  add_library(lua_backend STATIC IMPORTED GLOBAL)
  set_target_properties(lua_backend PROPERTIES
    IMPORTED_LOCATION ${LUAJIT_LIBRARY}
  )
  add_dependencies(lua_backend luajit_build)
  target_include_directories(lua_backend INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/luajit/src
  )
else()
  add_subdirectory(libraries/lua EXCLUDE_FROM_ALL)
  add_library(lua_backend ALIAS lua)
endif()
```

Then change the main `CMakeLists.txt` to link against `lua_backend` instead of `lua`:

```cmake
target_link_libraries(Game PRIVATE ... lua_backend ...)
```

**Option B: CMake wrapper for LuaJIT**

Write a `libraries/luajit/CMakeLists.txt` that compiles LuaJIT's C files directly. This is fragile because LuaJIT's build involves running `minilua` to process DynASM files and running `buildvm` to generate platform-specific assembly. Several community CMake wrappers exist (e.g., `cmake-luajit`), but they tend to lag behind LuaJIT releases.

**Recommendation**: Option A. LuaJIT's Makefile is battle-tested and handles all the DynASM/buildvm complexity. `ExternalProject` isolates it cleanly.

#### Step 1.3: Update include paths

Currently in `CMakeLists.txt:107`:
```cmake
target_include_directories(Game PRIVATE ... "${PROJECT_SOURCE_DIR}/libraries/lua/src")
```

With the option flag:
```cmake
if(USE_LUAJIT)
  target_include_directories(Game PRIVATE "${PROJECT_SOURCE_DIR}/libraries/luajit/src")
else()
  target_include_directories(Game PRIVATE "${PROJECT_SOURCE_DIR}/libraries/lua/src")
endif()
```

The header names (`lua.h`, `lauxlib.h`, `lualib.h`) are identical between PUC Lua and LuaJIT, so no `#include` changes are needed in C++ source files.

#### Step 1.4: Update devenv.nix

Add LuaJIT to the Nix development environment:

```nix
packages = with pkgs; [
  # ... existing packages ...
  luajit        # For system LuaJIT (optional, for testing)
];
```

No other nix changes needed since we build LuaJIT from the vendored source.

#### Step 1.5: Handle GC64 mode

Enable `LUAJIT_ENABLE_GC64` when building LuaJIT (shown in the ExternalProject command above). This relaxes the pointer address restriction from 2 GB to 4 GB and is the default in LuaJIT 2.1 on x64 since mid-2023. With GC64 enabled, the custom `MimallocAllocator` will work without modification.

Without GC64, LuaJIT requires all GC-managed memory to reside in the low 2 GB of virtual address space. Our allocator's `mi_manage_os_memory_ex` call does not guarantee this. Enabling GC64 avoids this problem entirely.

**Build flag**: `XCFLAGS=-DLUAJIT_ENABLE_GC64`

### Phase 2: Verify compatibility

#### Step 2.1: Compile and run

```bash
cmake -G Ninja -S . -B build -DUSE_LUAJIT=ON
cmake --build build --target Game
./build/game run assets/
```

Expected: The engine starts, loads `testgame1.lua`, and runs identically to the PUC Lua build. The startup log should show `LuaJIT 2.1.x` instead of `Lua 5.1` (from `game.cc:666` which prints `LUA_VERSION`).

Note: LuaJIT defines `LUA_VERSION` as `"Lua 5.1"` for compatibility, but also defines `LUAJIT_VERSION` as `"LuaJIT 2.1.x"`. We can detect which backend is active:

```cpp
#ifdef LUAJIT_VERSION
  LOG("Using ", LUAJIT_VERSION);
#else
  LOG("Using ", LUA_VERSION);
#endif
```

#### Step 2.2: Run all game scenarios

Test each entry point:

| Test | Command | What to verify |
|------|---------|---------------|
| Default game | `game run assets/` | Sprites render, physics bodies move, sounds play |
| Hot reload | Edit `testgame1.lua` while running | Game reloads without crash |
| Fennel | Create `test.fnl`, `require` it from `main.lua` | Fennel compiler loads, compiles, runs |
| Packaging | `game package assets/ -o test.pkg` | Packaged game runs |
| Stubs generation | `game stubs --output /tmp/stubs.lua` | LuaLS stubs generated |
| Error handling | Add `error("test")` to `update()` | Crash screen shows, Q quits |
| Single evaluation | Return non-table from `main.lua` | Single eval mode works |

#### Step 2.3: Run the existing test suite

```bash
cmake --build build --target Tests && ./build/Tests
```

The test suite doesn't test Lua directly (`tests/test.cc` covers `FixedArray`, `DynArray`, vectors, etc.), but it should still pass to confirm the build isn't broken.

#### Step 2.4: Memory allocator validation

Verify the custom allocator works correctly with LuaJIT:

1. Check that `lua_gc(state_, LUA_GCCOUNT, 0)` returns sensible values
2. Run the engine under Valgrind: `valgrind --leak-check=full ./build/game run assets/`
3. Verify allocator stats (`Lua::AllocatorStats()`) look reasonable
4. Stress test: create many entities, run for extended periods, check for leaks or corruption

### Phase 3: Conditional compilation support

Add a compile-time define so C++ code can detect the backend:

```cmake
if(USE_LUAJIT)
  target_compile_definitions(Game PRIVATE GAME_USE_LUAJIT)
endif()
```

Use it for LuaJIT-specific features:

```cpp
void Lua::LoadLibraries() {
  // ...
  AddBasicLibs(state_);
#ifdef GAME_USE_LUAJIT
  // Optionally load the JIT and FFI libraries
  // luaopen_jit(state_);    // JIT control from Lua
  // luaopen_ffi(state_);    // FFI for direct C access
#endif
  // ...
}
```

### Phase 4: Optional LuaJIT-specific enhancements (future)

These are not part of the initial migration but become possible once LuaJIT is in place:

#### 4.1: FFI-based vector types

Replace userdata-based `vec2`/`vec3`/`vec4` with FFI cdata for zero-overhead access:

```lua
local ffi = require("ffi")
ffi.cdef[[
  typedef struct { float x, y; } vec2;
]]
local v = ffi.new("vec2", 1.0, 2.0)
print(v.x, v.y)  -- no C API crossing
```

This would be a significant API change and should be its own project.

#### 4.2: JIT profiling

LuaJIT includes `jit.p` (profiler) and `jit.v` (verbose JIT output) modules. These can be exposed through `G.system` for development builds.

#### 4.3: LuaJIT's `string.buffer` library

LuaJIT 2.1 includes a high-performance string buffer library that could replace or supplement the `ByteBuffer` userdata type.

## Risk Assessment

### High risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Custom allocator address space** | LuaJIT without GC64 requires low-address pointers. Failure causes immediate crash or memory corruption. | Enable GC64 mode (`LUAJIT_ENABLE_GC64`). This is the default on x64 in recent LuaJIT 2.1. |
| **LuaJIT build complexity** | LuaJIT's build involves DynASM code generation and platform-specific assembly. Cross-compilation is harder than PUC Lua. | Use `ExternalProject` to delegate to LuaJIT's own Makefile. Test on all target platforms (Linux x64 primarily, Windows via MinGW). |

### Medium risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Subtle behavioral differences** | LuaJIT has minor differences in error messages, `tostring` output for some types, and floating-point edge cases. | Thorough playtesting. None of our code relies on exact error message text (we parse errors but only to extract file:line). |
| **JIT compilation failures** | Some Lua patterns cause JIT traces to abort ("NYI" — not yet implemented). The code falls back to the interpreter, which is still as fast as PUC Lua. | No action needed for correctness. Profile later for optimization. |
| **Fennel compatibility** | Fennel generates Lua 5.1 source code. Some generated patterns might not JIT-compile well. | Fennel output is standard Lua 5.1 — it will run correctly. JIT optimization is a future concern. |
| **`debug` library differences** | LuaJIT's debug library has the same API but some functions behave slightly differently under JIT (e.g., `debug.getinfo` may return `nil` for JIT-compiled frames). | Our `Traceback` function uses `debug.traceback` which works correctly in LuaJIT. The `lua_getstack`/`lua_getinfo` calls in `FillLogLine` work at the C API level. |

### Low risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Lua script incompatibilities** | All game scripts use Lua 5.1 syntax and semantics. LuaJIT is a conforming 5.1 implementation. | `lume.lua` already handles 5.1/5.2 compat. `classic.lua`, `timer.lua` are widely used with LuaJIT. |
| **Third-party library compat** | `fennel.lua`, `lume.lua`, `classic.lua`, `timer.lua` are all known to work with LuaJIT. | No action needed. |
| **Binary size increase** | LuaJIT's static library is ~400-600 KB vs PUC Lua's ~200 KB. | Negligible for a game engine. |

### Platform-specific risks

| Platform | Risk | Notes |
|----------|------|-------|
| **Linux x86_64** | Low | Primary development platform. LuaJIT is mature and well-tested here. GC64 is default. |
| **Windows x86_64** | Medium | LuaJIT builds with MSVC (`msvcbuild.bat`) or MinGW. The CMake `ExternalProject` uses `msvcbuild.bat` on Windows. |
| **macOS** | N/A | **LuaJIT is gated off on macOS.** The CMake flag `USE_LUAJIT` is force-set to `OFF` on `APPLE` platforms. macOS builds always use PUC Lua 5.1. This avoids the Apple Silicon compatibility issues entirely. |
| **Linux ARM64** | Medium | LuaJIT has ARM64 support but it's less battle-tested than x86_64. |

## What needs to change (file-by-file summary)

| File | Change | Required |
|------|--------|----------|
| `CMakeLists.txt` | Add `USE_LUAJIT` option, `ExternalProject` for LuaJIT, conditional include paths, link `lua_backend` instead of `lua` | Yes |
| `libraries/luajit/` | Add LuaJIT source tree (git subtree or submodule) | Yes |
| `src/lua.h` | No changes needed. Headers are compatible. Optionally add `#ifdef GAME_USE_LUAJIT` for future FFI/JIT features. | No |
| `src/lua.cc` | No changes needed. All C API calls are compatible. | No |
| `src/lua_*.cc` | No changes needed. All binding files use standard Lua 5.1 C API. | No |
| `src/game.cc` | Optionally update `PrintSystemInformation()` to print `LUAJIT_VERSION` when available. | Optional |
| `devenv.nix` | Optionally add `luajit` package for system-level testing. | Optional |
| `assets/.luarc.json` | No change. LuaLS config stays `"runtime.version": "Lua 5.1"`. | No |
| `assets/*.lua` | No changes needed. All scripts are Lua 5.1 compatible. | No |

## Testing Strategy

### Automated testing

1. **CI matrix**: Build and run tests with both `-DUSE_LUAJIT=OFF` (default, PUC Lua) and `-DUSE_LUAJIT=ON` (LuaJIT) to catch regressions in either backend.

2. **Add Lua VM integration tests** (currently absent per `TASKS.md`):
   - Create a test Lua script that exercises all `G.*` APIs
   - Run it under both backends
   - Compare outputs

### Manual testing checklist

- [ ] Engine starts with LuaJIT backend
- [ ] `testgame1.lua` runs identically (sprites, physics, sound)
- [ ] Hot reload works (modify a `.lua` file, engine reloads)
- [ ] Fennel compilation works (`.fnl` files load and run)
- [ ] Error handling works (deliberate `error()` shows crash screen)
- [ ] `game stubs` generates correct output
- [ ] `game package` produces a working packaged build
- [ ] Memory usage is comparable (check `lua_gc` counts)
- [ ] No Valgrind/ASan errors under LuaJIT

### Performance benchmarking

Once both backends work, benchmark to quantify LuaJIT's advantage:

1. Entity-heavy scene (many `update()` calls per frame)
2. Vector math operations in Lua
3. Fennel compilation time
4. Startup time (cold and hot cache)

## Migration order

1. **Vendor LuaJIT** — add sources to `libraries/luajit/`
2. **CMake plumbing** — `ExternalProject`, `USE_LUAJIT` option, conditional paths
3. **Build and fix** — get a clean compile with LuaJIT
4. **Print version** — update `PrintSystemInformation()` to show LuaJIT version
5. **Playtest** — run through the manual testing checklist
6. **Validate allocator** — run under Valgrind, check GC stats
7. **CI** — add LuaJIT build to CI matrix (if applicable)
8. **Ship** — make LuaJIT the default (or keep as opt-in)

## Open Questions

1. **Should LuaJIT be the default or opt-in?** If all tests pass, making it the default is reasonable. PUC Lua can remain as a fallback via `-DUSE_LUAJIT=OFF`.

2. **Should we expose the FFI library to game scripts?** FFI is powerful but breaks Lua's sandboxing — it can access arbitrary memory. For a game engine where scripts are trusted first-party code, this is probably fine. For modding support in the future, it would need to be disabled.

3. **Should we expose `jit.*` control?** Allowing game scripts to call `jit.off()` or `jit.flush()` could be useful for debugging but could also cause confusion. Recommend: expose in debug builds only.

4. **Windows build strategy**: The CMake `ExternalProject` already uses `msvcbuild.bat` on Windows. If MinGW is used instead of MSVC, the build command would need to switch to `make` (same as Linux).

5. **Pinned version**: Should we pin a specific LuaJIT commit hash rather than tracking the `v2.1` branch? The `v2.1` branch is rolling-release. Pinning ensures reproducible builds.
