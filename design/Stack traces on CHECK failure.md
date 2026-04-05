# Stack Traces on CHECK Failure

## Problem

When a `CHECK` or `DCHECK` fails, the engine calls `Crash()` which shows an
error message and either traps into a debugger or aborts. Without GDB attached,
the developer only sees the failing condition and file/line of the CHECK itself
-- not how execution got there. This makes debugging crash reports and
non-interactive environments (CI, remote testing) difficult.

## Goal

Print a human-readable stack trace (function names + file:line) to stderr/log
before aborting on CHECK failure. The trace should appear automatically without
needing a debugger attached.

## Current Crash Flow

```
CHECK(cond, ...) -> Crash(file, line, ...) -> g_CrashHandler(message) -> abort/trap
```

`Crash()` in `logging.cc` calls a pluggable `CrashHandler` function pointer.
The default handler just calls `std::abort()`. In `game.cc`, it is overridden
to show an SDL message box and then `__builtin_debugtrap`.

The insertion point for stack traces is clear: print the trace inside `Crash()`
before calling `g_CrashHandler`.

## Options Evaluated

### 1. backward-cpp (recommended)

A single-header library (~4500 lines) that captures and resolves stack traces
on Linux, macOS, and Windows.

- **Files to vendor:** 1 header (`backward.hpp`) + 1 trivial `.cpp`
- **Compilation:** One translation unit, ~1-3 seconds
- **Symbol resolution:**
  - Linux: uses libdw (elfutils) or libbfd for file:line. Falls back to
    `backtrace_symbols()` (function names only) if neither is available.
  - macOS: uses system frameworks (no extra deps)
  - Windows: uses DbgHelp (ships with Windows SDK)
- **Compatibility:** No exceptions, no RTTI, no `try`/`catch` in the core path.
  Compiles clean with `-fno-exceptions -fno-rtti`
- **STL usage:** Uses `std::vector`, `std::string`, `std::map` internally.
  Acceptable for vendored library code (same policy as Box2D, pugixml, etc.)
- **License:** MIT
- **Maintenance:** ~3.5k GitHub stars, actively maintained

### 2. cpptrace

Full CMake library (~92 source files). Feature-rich but too heavy to vendor.
Not designed for `-fno-exceptions` environments. Would require patches.

**Verdict:** Too complex for our needs.

### 3. C++23 std::stacktrace

Standard library feature available in GCC 12+ and Clang with `-std=c++23`.
Requires linking `-lstdc++_libbacktrace`.

**Verdict:** Not viable. Project targets C++17.

### 4. Raw platform APIs (backtrace() / libunwind / CaptureStackBackTrace)

Linux `backtrace()` + `backtrace_symbols()` gives function names but no
file:line without spawning `addr2line`. Would need ~500-1000 lines of
platform-specific code across Linux/macOS/Windows to match what backward-cpp
provides out of the box.

**Verdict:** Reimplementing backward-cpp from scratch. Not worth it.

### 5. Boost.Stacktrace

Header-only mode exists but pulls in multiple Boost headers (Config, Core,
ContainerHash). The `backtrace` backend needs linking. Bringing in Boost for
one feature contradicts the project's minimal-dependency approach.

**Verdict:** Too heavy.

### 6. libbacktrace

GCC's C library (~15 `.c` files) that resolves DWARF symbols. Pure C, no
exceptions. Used internally by GCC's `std::stacktrace`. Could be vendored.
However, Windows support is limited to MinGW (no PDB/MSVC support).

**Verdict:** Good Linux/macOS option but incomplete Windows story. backward-cpp
uses libbacktrace internally when available anyway.

## Recommendation: backward-cpp

### Integration Plan

**Vendor the library:**

```
libraries/backward-cpp/
  backward.hpp
  backward.cpp
```

**Add a `PrintStackTrace()` function** in `logging.cc` (or a new
`stacktrace.cc`):

```cpp
#include "backward.hpp"

namespace G {

void PrintStackTrace() {
  backward::StackTrace st;
  st.load_here(32);       // Capture up to 32 frames.
  st.skip_n_firsts(3);    // Skip PrintStackTrace -> Crash -> CHECK internals.

  backward::Printer p;
  p.print(st, stderr);
}

}  // namespace G
```

**Call it from `Crash()`:**

```cpp
[[noreturn]] void Crash(const char* message) {
#ifdef GAME_WITH_ASSERTS
  PrintStackTrace();
#endif
  g_CrashHandler(message);
  std::abort();
}
```

Stack traces are debug-only (`GAME_WITH_ASSERTS`). Release builds have zero
overhead -- backward.hpp is never included.

### Build Changes

- Compile `backward.cpp` only in debug/assert builds (guard with
  `GAME_WITH_ASSERTS` in CMakeLists.txt)
- On Linux: link against `libdw` for file:line resolution. Already available in
  the Nix devenv via elfutils. Without it, traces still work but show only
  function names + offsets
- On macOS/Windows: no extra link dependencies
- Add `-DBACKWARD_HAS_DW=1` (or `BACKWARD_HAS_BFD=1`) to tell backward-cpp
  which resolver to use on Linux

### Example Output

With backward-cpp and libdw, a CHECK failure would print:

```
F [game.cc:145] CHECK failed: texture != nullptr  Failed to load player sprite
Stack trace (most recent call first):
#4  src/game.cc:145           in G::EngineModules::LoadTexture(char const*)
#3  src/assets.cc:89          in G::DbAssets::Load()
#2  src/cmd_run.cc:34         in G::RunCommand(...)
#1  src/main.cc:12            in main
```

### Alternatives Within backward-cpp

backward-cpp also provides `SignalHandling` which installs handlers for
SIGSEGV, SIGABRT, etc. and prints a trace on crash. This could be useful as a
second phase but is not needed for the initial CHECK integration -- calling
`PrintStackTrace()` explicitly is simpler and more predictable.

## Scope

- Debug builds only (zero cost in release)
- CHECK, DCHECK, and DIE failures
- No signal handler installation in phase 1 (keep it simple)
- Vendor backward-cpp under `libraries/`
