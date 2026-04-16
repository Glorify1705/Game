---
status: partially-implemented
tags: [build, testing, cmake, ctest]
---

# CMake and CTest Improvements

## Problem

The CMake build system was recently modernized (PR #71: library split, presets,
ccache). Several issues remain that affect day-to-day development:

1. **Preset cache poisoning**: switching between presets that share `build/`
   leaves stale cache variables. Example: `cmake --preset sanitize` sets
   `ENABLE_SANITIZERS=ON` in the cache. Switching back to `cmake --preset dev`
   doesn't reset it — the "dev" binary is silently sanitized.

2. **Test infrastructure is minimal**: 123 tests in a single 1,415-line file,
   no ctest labels, no timeouts, no test execution in the build script, and
   tests only cover data structures and collision — not math, transforms, color,
   stats, XML, QOA, or logging.

3. **clang-tidy preset has pre-existing errors**: `game-tidy` fails on
   `cmd_atlas.cc` (3 `bugprone-implicit-widening-of-multiplication-result`
   warnings treated as errors). This is a code issue, not a build issue, but
   the tidy preset is unusable until fixed.

---

## Part 1: Fix preset cache poisoning

### The bug

Presets `dev`, `sanitize`, `profile`, and `tidy` all write to `build/`. CMake
caches variables in `build/CMakeCache.txt`. A preset only sets the variables it
declares — it does not clear variables set by a previous preset.

Sequence that produces a silently wrong binary:

```
cmake --preset sanitize   # sets ENABLE_SANITIZERS=ON in cache
cmake --build --preset sanitize
cmake --preset dev        # does NOT set ENABLE_SANITIZERS=OFF
cmake --build --preset dev
# "dev" binary has sanitizers enabled, ninja says "no work to do"
```

### Fix: explicit defaults in every shared-directory preset

Every configure preset that uses `build/` must explicitly set every option:

```json
{
  "name": "dev",
  "binaryDir": "build",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Debug",
    "ENABLE_SANITIZERS": "OFF",
    "ENABLE_PROFILING": "OFF",
    "ENABLE_CLANG_TIDY": "OFF"
  }
}
```

Same for `sanitize` (PROFILING=OFF, CLANG_TIDY=OFF), `profile` (SANITIZERS=OFF,
CLANG_TIDY=OFF), and `tidy` (SANITIZERS=OFF, PROFILING=OFF).

### Alternative: separate build directories

Give each preset its own directory (`build-sanitize/`, `build-tidy/`, etc.).
This avoids the cache problem entirely but costs more disk. Switching presets
becomes instant (no reconfigure) but you burn ~500 MB per directory.

**Recommendation**: Explicit defaults. Disk is cheap but reconfiguring SDL3
takes 90 seconds when the cache is cold. Separate directories only make sense if
we add a CI matrix.

---

## Part 2: CTest improvements

### 2a. Run ctest in game-test

Currently `game-test.sh` builds the Tests target but doesn't run `ctest`. The
test binary is run implicitly by GoogleTest's post-build step, but this skips
ctest features (parallelism, filtering, result reporting).

Change `game-test.sh` to:

```bash
cmake --preset dev
cmake --build --preset test
ctest --preset dev --output-on-failure
```

### 2b. Add timeouts and labels

The current `gtest_discover_tests(Tests)` call has no configuration. A stuck
test (e.g., a deadlock in ThreadPoolExecutor) hangs forever.

```cmake
gtest_discover_tests(Tests
    DISCOVERY_TIMEOUT 10
    PROPERTIES
        TIMEOUT 30
        LABELS "unit"
)
```

### 2c. Add a ctest preset with parallel execution

```json
{
  "name": "dev",
  "configurePreset": "dev",
  "output": {
    "outputOnFailure": true
  },
  "execution": {
    "jobs": 0
  }
}
```

`"jobs": 0` uses all available cores. GoogleTest tests are isolated (no shared
state between test cases), so full parallelism is safe.

---

## Part 3: Split test.cc into per-subsystem files

### Current state

All 123 tests live in `tests/test.cc`. This means:

- Every test edit recompiles all 1,415 lines
- Can't filter by subsystem at the build level
- Hard to navigate; test suites are interleaved with helper functions

### Proposed split

| File | Test suites | Line count (approx) |
|------|-------------|---------------------|
| `tests/test_containers.cc` | FixedArray, DynArray, Dictionary, CircularBuffer, SegmentedList, InlinedArray | ~600 |
| `tests/test_collision.cc` | Collision, CollisionWorld | ~350 |
| `tests/test_math.cc` | Vectors, Bits, Easing (+ new: Mat, Transformations) | ~150 |
| `tests/test_error.cc` | Error, ErrorOr, TRY, MUST | ~150 |
| `tests/test_executor.cc` | InlineExecutor, ThreadPoolExecutor | ~100 |
| `tests/test_strings.cc` | FixedStringBuffer*, StrAlias, SmallBufferAlias, StringTable (+ new: Stats) | ~100 |

### CMake integration

Two options:

**Option A: Single test executable, multiple sources** (recommended)

```cmake
add_executable(Tests
    tests/test_containers.cc
    tests/test_collision.cc
    tests/test_math.cc
    tests/test_error.cc
    tests/test_executor.cc
    tests/test_strings.cc
)
```

Same binary, same `gtest_discover_tests`. Ninja rebuilds only the changed
translation unit. Simple, no build system complexity.

**Option B: Multiple test executables**

Separate executables per subsystem. Allows different link dependencies (e.g.,
math tests don't need to link engine at all — just headers). More complex CMake,
but faster link times per binary and better isolation.

**Recommendation**: Start with Option A. Move to Option B only if link time
becomes a bottleneck.

---

## Part 4: Expand test coverage

### Untested pure-logic subsystems

These subsystems have no SDL/OpenGL/Lua dependencies and can be tested today
with no mocking:

| Subsystem | What to test | Priority |
|-----------|-------------|----------|
| `mat.h` | Multiply, transpose, identity, matrix-vector product | High — wrong matrix = wrong rendering, hard to debug visually |
| `transformations.cc` | Ortho, rotation, scale, composition | High — same reason |
| `color.cc` | Float/uint8 roundtrip, color table lookup, out-of-range | Medium |
| `stats.cc` | Welford's mean/variance, percentile buckets, empty/single | Medium |
| `xml.cc` | Parse valid/malformed XML, attribute lookup, ForEachChild | Medium |
| `qoa.cc` | Encode/decode roundtrip, streaming frame boundaries | Medium |
| `logging.cc` | Level filtering, channel routing (inject test sink) | Low |
| `profiler.cc` | Event ring buffer, JSON export format | Low |
| `config.cc` | JSON parse to struct, default values | Low (needs fixture data) |

### Recommended test fixtures

Many tests repeat allocator setup. A shared fixture reduces boilerplate:

```cpp
class AllocatorFixture : public ::testing::Test {
protected:
    Allocator* alloc = SystemAllocator::Instance();
};
```

For arena-heavy tests:

```cpp
template <size_t N>
class ArenaFixture : public ::testing::Test {
protected:
    StaticAllocator<N> storage;
    ArenaAllocator arena{&storage};
};
using SmallArena = ArenaFixture<4096>;
using LargeArena = ArenaFixture<65536>;
```

---

## Part 5: Fix pre-existing clang-tidy errors

`cmd_atlas.cc:134-136` has three `bugprone-implicit-widening-of-multiplication-result`
warnings that are treated as errors. These are real bugs (int multiplication used
as pointer offset could overflow for very large sprites):

```cpp
uint8_t* dst = output + y * atlas_width * 4 + sprite.x * 4;
uint8_t* src = pixels + (sprite.height - 1 - y) * sprite.width * 4;
memcpy(dst, src, sprite.width * 4);
```

Fix by casting to `size_t` before the multiply:

```cpp
uint8_t* dst = output + (size_t)y * atlas_width * 4 + (size_t)sprite.x * 4;
uint8_t* src = pixels + (size_t)(sprite.height - 1 - y) * sprite.width * 4;
memcpy(dst, src, (size_t)sprite.width * 4);
```

This unblocks `game-tidy` so it can be used for ongoing development.

---

## Implementation order

### Phase 1: Fix what's broken (small, immediate)

1. Fix preset cache poisoning (explicit defaults in CMakePresets.json)
2. Fix cmd_atlas.cc tidy errors (3 casts)
3. Add `ctest --preset dev` to game-test.sh

### Phase 2: Improve test infrastructure (medium effort)

4. Add timeouts and labels to gtest_discover_tests
5. Add ctest parallel execution preset
6. Split test.cc into per-subsystem files

### Phase 3: Expand coverage (ongoing)

7. Add mat.h and transformations.cc tests
8. Add color.cc and stats.cc tests
9. Add xml.cc and qoa.cc tests
10. Add test fixtures for allocator setup
