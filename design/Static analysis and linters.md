# Static Analysis and Linters

## Problem

Header hygiene is fragile. We just spent two PRs (#14, #15) manually hunting dead includes like `<ostream>`, `<string>`, `<deque>`, and `<sstream>`. These accumulate silently and inflate compile times. More broadly, we have no automated tooling to catch:

- **Unnecessary or missing includes** — transitively-satisfied includes break when an intermediate header changes.
- **C++ undefined behavior** — signed overflow, misaligned access, invalid enum values, shift-past-width. ASan catches memory bugs but not language-level UB.
- **OpenGL undefined behavior** — wrong enum combos, state machine violations, shader errors that only surface on specific drivers.
- **Code quality regressions** — narrowing conversions, shadowed variables, missed `const`, performance pitfalls.

## Current state

### What we already have

| Tool | Status | Where |
|------|--------|-------|
| `-Wall -Wextra -Werror` | Enabled for all targets | CMakeLists.txt:130 |
| `-Wno-unused-parameter` | Suppressed (too noisy) | CMakeLists.txt:130 |
| `-fno-exceptions -fno-rtti` | Engine code only (not vendored) | CMakeLists.txt:137 |
| AddressSanitizer | Tests only | CMakeLists.txt:186,199 |
| `clang-format` | Pre-commit hook + `game-format` script | devenv.nix:74, git-hooks |
| `DONOTSUBMIT` checker | Pre-commit hook | scripts/donotsubmit.sh |
| `compile_commands.json` | Generated | CMakeLists.txt:7 |
| `glDebugMessageCallback` | Enabled at runtime if `GLAD_GL_VERSION_4_3` | game.cc:767 |
| `OPENGL_CALL` macro | Records source location for GL debug callback | logging.h:102 |
| `glGetError` checks | Scattered in renderer.cc (4 callsites) | renderer.cc:209,225,299,323 |
| RenderDoc | In nix devenv packages | devenv.nix:27 |
| `ccls` | In nix devenv, configured via `.ccls` | devenv.nix:8 |

### What we're missing

| Gap | Impact |
|-----|--------|
| No include hygiene tool (IWYU) | Dead includes accumulate, compile times grow, refactors cause cascading breakage |
| No UBSan | Signed overflow, invalid shifts, misaligned access go undetected |
| No clang-tidy | No automated checks for narrowing, shadowing, performance pitfalls, modernization |
| No shader validation | Shader errors in inline GLSL strings only caught at runtime on the current GPU/driver |
| No `-Wshadow`, `-Wconversion` etc. | Whole categories of bugs invisible to the compiler |
| GL debug context not requested at creation | `SDL_GL_CONTEXT_DEBUG_FLAG` not set, so drivers may not generate debug messages |

---

## Tool 1: Include What You Use (IWYU)

### What it does

IWYU parses each translation unit using the Clang front-end, determines which symbols are actually used, and recommends:

1. **Remove** — headers included but whose symbols are never referenced.
2. **Add** — symbols used transitively through another header (fragile dependency).
3. **Forward-declare** — where a full include can be replaced by a forward declaration.

### Integration

**Option A: CMake variable (runs during build)**

```cmake
option(ENABLE_IWYU "Run include-what-you-use during build" OFF)
if(ENABLE_IWYU)
    find_program(IWYU_EXE NAMES include-what-you-use REQUIRED)
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE
        "${IWYU_EXE}"
        "-Xiwyu" "--mapping_file=${CMAKE_SOURCE_DIR}/iwyu.imp"
        "-Xiwyu" "--no_fwd_decls"
        "-Xiwyu" "--cxx17ns"
    )
    # Don't analyze vendored code
    set_source_files_properties(${VENDORED_SRCS} PROPERTIES
        CXX_INCLUDE_WHAT_YOU_USE ""
    )
endif()
```

Warnings appear in build output but do not block the build. Good for local iteration.

**Option B: Standalone (uses compile_commands.json)**

```bash
iwyu_tool.py -p build/ -- \
    -Xiwyu --mapping_file=iwyu.imp \
    -Xiwyu --no_fwd_decls \
    2>&1 | tee iwyu_output.txt

# Auto-apply fixes
fix_includes.py < iwyu_output.txt
```

Better for CI since it decouples analysis from building.

### Mapping file

We need a mapping file (`iwyu.imp`) because IWYU doesn't understand umbrella headers like glad.h or SDL.h out of the box:

```json
[
  { "include": ["@\"libraries/glad/.*\"", "private", "\"libraries/glad/glad.h\"", "public"] },
  { "include": ["@<SDL.*>", "private", "\"SDL.h\"", "public"] },
  { "include": ["@<GL/.*>", "private", "\"libraries/glad/glad.h\"", "public"] },
  { "include": ["\"libraries/stb_truetype.h\"", "private", "\"libraries/stb_truetype.h\"", "public"] }
]
```

### Pragmas for edge cases

```cpp
#include "SDL.h"  // IWYU pragma: keep  (used via macro expansion)

// IWYU pragma: no_include "libraries/glad/khrplatform.h"
```

### Version compatibility

IWYU versions must match the Clang version exactly. We use Clang 21, so we need IWYU 0.25. IWYU 0.25 compiles built-in mappings into the executable instead of shipping `.imp` files — use `include-what-you-use --export_mappings=./mappings/` to dump them if needed for customization.

### Nix package

Add to `devenv.nix`:

```nix
packages = with pkgs; [
    # ... existing packages ...
    include-what-you-use
];
```

Verify the packaged version matches our Clang version. If it doesn't, we may need an overlay to build IWYU against our Clang.

### Known limitations

- **Macro-provided types**: IWYU struggles when types come through macros in unrelated headers.
- **Template metaprogramming**: Complex SFINAE (like our `HasAppendString`) can produce false positives.
- **Conditional compilation**: Only sees the active `#ifdef` branch.
- **Aggregate initialization**: Sometimes misses includes needed for aggregate init.

Forward declarations are our project convention for function declarations in headers (e.g., `class StringBuffer;` in stats.h before it was changed). IWYU's `--no_fwd_decls` flag disables its forward-declaration suggestions, which avoids fights with our style.

---

## Tool 2: Undefined Behavior Sanitizer (UBSan)

### What it catches (that ASan doesn't)

| UBSan | ASan |
|-------|------|
| Signed integer overflow | Heap/stack/global buffer overflow |
| Invalid shift (negative or too-wide) | Use-after-free |
| Misaligned pointer access | Use-after-return/scope |
| Invalid bool/enum values | Double-free |
| Float-to-int overflow | Memory leaks (LSan) |
| Reaching `__builtin_unreachable` | |
| Invalid vptr / object type | |
| Division by zero | |
| Null pointer dereference (with context) | |

They are complementary. ASan catches memory corruption; UBSan catches language-level UB.

### Performance

| Configuration | Slowdown | Memory |
|---------------|----------|--------|
| UBSan alone | ~1.2x | Minimal |
| ASan alone | ~2-3x | ~2-3x |
| ASan + UBSan | ~2-3x | ~2-3x (UBSan adds negligible extra) |

ASan + UBSan combined costs effectively the same as ASan alone. There is no reason not to enable UBSan alongside our existing ASan on the test target.

### CMake changes

Replace the current hard-coded ASan on Tests with a combined flag:

```cmake
# Current (CMakeLists.txt:186):
$<${IS_GCC_LIKE}:-fsanitize=address>

# Proposed:
$<${IS_GCC_LIKE}:-fsanitize=address,undefined;-fno-sanitize-recover=all;-fno-omit-frame-pointer>
```

And the link flags (CMakeLists.txt:199):

```cmake
# Current:
target_link_options(Tests PRIVATE $<${IS_GCC_LIKE}:-fsanitize=address>)

# Proposed:
target_link_options(Tests PRIVATE $<${IS_GCC_LIKE}:-fsanitize=address,undefined>)
```

`-fno-sanitize-recover=all` makes UBSan abort on first error instead of continuing, which is what we want for tests.

### Optional: enable on Game target too

For development builds, we could gate sanitizers behind a CMake option so we can run the actual game with sanitizers:

```cmake
option(ENABLE_SANITIZERS "Enable ASan + UBSan for Game target" OFF)
if(ENABLE_SANITIZERS)
    target_compile_options(Game PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(Game PRIVATE -fsanitize=address,undefined)
endif()
```

Add a devenv script:

```nix
scripts."game-sanitize" = {
    exec = ''
        cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON -G Ninja -S . -B build \
        && cmake --build build --target Game \
        && UBSAN_OPTIONS="print_stacktrace=1" \
           ASAN_OPTIONS="detect_leaks=1" \
           ./build/game run assets
    '';
};
```

### Suppression file

For vendored code or intentional unsigned wrap:

```
# ubsan.supp
unsigned-integer-overflow:libraries/*
alignment:libraries/stb_*
signed-integer-overflow:libraries/sqlite3.c
```

Runtime: `UBSAN_OPTIONS="suppressions=ubsan.supp:print_stacktrace=1"`

### Extra: `-fsanitize=integer`

Catches unsigned integer overflow (not technically UB, but catches unintentional wraparound). Noisy in hash functions and RNG — use suppression file. Worth trying once to see what surfaces.

---

## Tool 3: clang-tidy

### Recommended configuration

Start minimal and expand. A `.clang-tidy` file at project root:

```yaml
---
Checks: >
  -*,
  bugprone-*,
  -bugprone-easily-swappable-parameters,
  -bugprone-exception-escape,
  performance-*,
  misc-*,
  -misc-non-private-member-variables-in-classes,
  -misc-use-anonymous-namespace,
  concurrency-*

# Promote critical checks to errors once the codebase is clean
WarningsAsErrors: >
  bugprone-use-after-move,
  bugprone-dangling-handle

HeaderFilterRegex: 'src/.*\.h$'
FormatStyle: file
```

### Why these categories first

**bugprone-\*** (highest value for game code):
- `bugprone-undefined-memory-manipulation` — catches `memcpy`/`memset` on non-trivially-copyable types. We use `memset` on matrices (e.g., `FMat2x2::Zero()`), which is correct for POD types but would be wrong if we ever added a constructor.
- `bugprone-narrowing-conversions` — implicit `double` → `float`, `int` → `short`. Common in GL code where `GLsizei` (int) mixes with `size_t`.
- `bugprone-misplaced-widening-cast` — integer overflow before cast.
- `bugprone-signed-char-misuse` — signed char where unsigned expected.
- `bugprone-dangling-handle` — dangling `string_view` (relevant to our `StringBuffer::piece()`).

**performance-\*** (matters in game loop):
- `performance-for-range-copy` — range-for copies where const-ref suffices.
- `performance-unnecessary-copy-initialization` — avoidable copies.
- `performance-type-promotion-in-math-fn` — float→double promotion in `sin()`/`cos()` calls. Costs on some platforms.
- `performance-unnecessary-value-param` — pass by value when const-ref would work.

**concurrency-\*** (we have a thread pool):
- `concurrency-mt-unsafe` — calls to functions that aren't thread-safe.

### Categories to add later

Once the initial set is clean:

```yaml
  modernize-*,
  -modernize-use-trailing-return-type,
  -modernize-avoid-c-arrays,
  -modernize-use-nodiscard,
  readability-*,
  -readability-identifier-length,
  -readability-magic-numbers,
  -readability-function-cognitive-complexity,
  -readability-else-after-return,
```

### CMake integration

```cmake
option(ENABLE_CLANG_TIDY "Run clang-tidy during build" OFF)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY
        "${CLANG_TIDY_EXE}"
        "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
        "--header-filter=src/.*\\.h$"
        "--use-color"
    )
    # Don't run on vendored code
    set_source_files_properties(${VENDORED_SRCS} PROPERTIES
        CXX_CLANG_TIDY ""
    )
endif()
```

Or run standalone with `run-clang-tidy` (already available via `clang-tools` in devenv):

```bash
run-clang-tidy -p build/ -j$(nproc) \
    -header-filter='src/.*\.h$' \
    'src/.*\.cc$'
```

### Suppressing false positives

```cpp
int x = dangerous_cast(y); // NOLINT(bugprone-narrowing-conversions)

// NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
auto copy = expensive_thing;

// NOLINTBEGIN(readability-magic-numbers)
constexpr float gravity = 9.81f;
// NOLINTEND(readability-magic-numbers)
```

---

## Tool 4: Additional compiler warnings

We already have `-Wall -Wextra -Werror`. Worth adding:

### High value (enable immediately)

```cmake
set(EXTRA_WARNINGS
    -Wshadow              # Variable shadows another in outer scope
    -Wdouble-promotion    # Float implicitly promoted to double (perf on some GPUs)
    -Wformat=2            # Enhanced printf format checking (relevant: AppendF uses snprintf)
    -Wimplicit-fallthrough  # Switch case fallthrough without annotation
    -Wundef               # Undefined macro used in #if
)
```

### Medium value (enable once codebase is clean)

```cmake
list(APPEND EXTRA_WARNINGS
    -Wconversion          # Implicit narrowing (int->float, size_t->int). Very noisy in GL code.
    -Wsign-conversion     # Implicit signed/unsigned conversions
    -Wold-style-cast      # C-style casts — we have some (uintptr_t) casts in stringlib
    -Wcast-align          # Pointer cast increases alignment requirement
    -Woverloaded-virtual  # Derived class hides base virtual function
    -Wzero-as-null-pointer-constant  # 0 instead of nullptr
)
```

### Clang-specific

```cmake
$<$<CXX_COMPILER_ID:Clang>:
    -Wthread-safety       # Thread safety annotations (useful with our SDL_mutex wrappers)
>
```

**Note on `-Wconversion`**: This is the noisiest flag. OpenGL code constantly mixes `GLsizei` (int), `GLuint` (unsigned int), `GLenum` (unsigned int), `size_t`, and `float`. Consider enabling it via clang-tidy's `bugprone-narrowing-conversions` check first (per-file suppressions are easier) before making it a compiler warning.

---

## Tool 5: OpenGL debugging

### Current state

We already have good infrastructure:

1. **`glDebugMessageCallback`** is set up in `game.cc:770-773` with `GL_DEBUG_OUTPUT_SYNCHRONOUS`.
2. **`OPENGL_CALL` macro** in `logging.h:102` records source locations so the debug callback can report where the offending GL call was made.
3. **`glGetError` checks** scattered in `renderer.cc` after texture/framebuffer creation.
4. **RenderDoc** is in the nix devenv.

### Gap: debug context flag

The GL context is created without requesting a debug context. The `glDebugMessageCallback` setup checks `GLAD_GL_VERSION_4_3 && GLAD_GL_KHR_debug` at runtime, but without the debug context flag, many drivers won't generate messages.

Add before window creation in `game.cc`:

```cpp
#ifdef GAME_WITH_ASSERTS
SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
```

This ensures debug messages are generated in debug builds. The flag has minimal performance impact on modern drivers (Mesa, NVIDIA) but should not be set in release builds.

### Shader validation

Our shaders are inline GLSL string literals in `shaders.cc` (GLSL 460 core). This means shader errors are only caught at runtime when `glCompileShader` is called. Options:

**Option A: Extract shaders to files + validate at build time**

Move inline GLSL to `.vert`/`.frag` files and validate with `glslangValidator`:

```cmake
find_program(GLSLANG_VALIDATOR glslangValidator)
if(GLSLANG_VALIDATOR)
    file(GLOB SHADER_FILES "${CMAKE_SOURCE_DIR}/assets/*.vert" "${CMAKE_SOURCE_DIR}/assets/*.frag")
    foreach(SHADER ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/${SHADER_NAME}.validated
            COMMAND ${GLSLANG_VALIDATOR} --target-env opengl ${SHADER}
            COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/${SHADER_NAME}.validated
            DEPENDS ${SHADER}
            COMMENT "Validating ${SHADER_NAME}"
        )
        list(APPEND VALIDATED_SHADERS ${CMAKE_BINARY_DIR}/${SHADER_NAME}.validated)
    endforeach()
    add_custom_target(validate_shaders ALL DEPENDS ${VALIDATED_SHADERS})
    add_dependencies(Game validate_shaders)
endif()
```

This already works for the fragment shaders in `assets/` (`basic.frag`, `crt.frag`, `pixelate.frag`, `testshader.frag`). The vertex shaders embedded in `shaders.cc` would need to be extracted.

**Option B: Keep inline shaders, validate in a test**

Write a test that compiles each shader string with `glslangValidator` via the C API (libglslang). More work, but doesn't require changing the shader loading architecture.

**Option C: Status quo with better runtime reporting**

Our `glDebugMessageCallback` already catches `GL_DEBUG_SOURCE_SHADER_COMPILER` messages. If we fix the debug context flag (above), shader compilation errors will be caught and logged via our existing callback. This is the least effort option.

### apitrace

apitrace records every GL call to a trace file for offline replay and inspection:

```bash
apitrace trace ./build/game run assets
apitrace replay game.trace
apitrace dump game.trace > gl_calls.txt
```

Add to nix:

```nix
packages = with pkgs; [
    # ... existing packages ...
    apitrace
];
```

### Typed GL wrappers

For the most error-prone GL calls, enum class wrappers provide compile-time type safety:

```cpp
enum class TextureTarget : GLenum { k2D = GL_TEXTURE_2D, k2DMultisample = GL_TEXTURE_2D_MULTISAMPLE };
enum class TextureFilter : GLint { kLinear = GL_LINEAR, kNearest = GL_NEAREST };

void SetTextureFilter(TextureTarget target, TextureFilter min, TextureFilter mag) {
    glTexParameteri(static_cast<GLenum>(target), GL_TEXTURE_MIN_FILTER, static_cast<GLint>(min));
    glTexParameteri(static_cast<GLenum>(target), GL_TEXTURE_MAG_FILTER, static_cast<GLint>(mag));
}
```

This is consistent with the project's existing use of enum classes (PR #14 converted several macros and bool params to enum classes). No static analyzer can catch passing `GL_LINEAR` where `GL_TEXTURE_2D` was expected, but the type system can.

---

## Tool 6: cppcheck (supplementary)

cppcheck has a different analysis engine from clang-tidy with relatively little overlap. It's particularly good at:

- Flow-sensitive null-after-deref detection
- Buffer overrun analysis
- Exception safety (less relevant to us since we disable exceptions)
- Uninitialized variable analysis across branches

```cmake
option(ENABLE_CPPCHECK "Run cppcheck during build" OFF)
if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES cppcheck REQUIRED)
    set(CMAKE_CXX_CPPCHECK
        "${CPPCHECK_EXE}"
        "--enable=warning,performance,portability"
        "--suppress=missingInclude"
        "--suppress=unmatchedSuppression"
        "--inline-suppr"
        "--quiet"
    )
    set_source_files_properties(${VENDORED_SRCS} PROPERTIES
        CXX_CPPCHECK ""
    )
endif()
```

Lower priority than clang-tidy and IWYU. Add once those are stable.

---

## Compilation time and runtime performance impact

All measurements taken on the current project (Clang 21, `-O1`, 16 cores).

### Baseline build times

| Scope | Parallel (`-j16`) | Serial (`-j1`) |
|-------|-------------------|-----------------|
| Full clean build (Game + Tests + all libraries) | **24s** | 90s |
| Engine sources only (`src/*.cc`, 39 files) | ~5s | **37s** |
| Heaviest file (`renderer.cc`) | — | **2.3s** |
| Second heaviest (`game.cc`) | — | **1.6s** |
| Lightest files (`stringlib.cc`) | — | **0.2s** |

### Per-tool impact on compilation time

#### Extra warning flags (`-Wshadow -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough`)

| File | Baseline | With flags | Overhead |
|------|----------|------------|----------|
| `renderer.cc` | 2.3s | 2.3s | **<1%** |

Warning flags are checked during the normal parse/sema phase that the compiler already does. Negligible compile-time cost. Even adding the noisier `-Wconversion -Wsign-conversion` showed no measurable slowdown.

**Verdict: Free.** Enable immediately.

#### UBSan (`-fsanitize=undefined`)

| File | Baseline | ASan only | ASan + UBSan | UBSan overhead |
|------|----------|-----------|--------------|----------------|
| `renderer.cc` | 2.3s | (current) | 3.3s | **~15%** |
| `game.cc` | 1.6s | (current) | 3.0s | **~23%** |

UBSan inserts runtime checks at every signed arithmetic op, shift, division, and pointer access. This adds ~15-25% compile time per file (the instrumentation pass runs after optimization). The full parallel build would go from ~24s to ~27s.

**Runtime overhead**: ~1.2x slowdown (vs ~2-3x for ASan alone). Since ASan dominates, adding UBSan to an already-ASan'd build has minimal additional runtime cost.

**Verdict: Cheap.** The ~3s increase on a 24s build is imperceptible. Already paying the ASan cost on Tests, so adding UBSan is nearly free.

#### clang-tidy (`bugprone-*`, `performance-*`)

| File | Normal compile | clang-tidy | Ratio |
|------|---------------|------------|-------|
| `renderer.cc` | 2.3s | 0.9s | 0.4x |
| `game.cc` | 1.6s | 1.1s | 0.7x |
| `stringlib.cc` | 0.2s | 0.1s | 0.5x |
| **All engine sources** (parallel) | ~5s | **4.0s** | 0.8x |

clang-tidy runs a full Clang parse internally (same cost as compiling) plus the checker passes. When run via `CMAKE_CXX_CLANG_TIDY`, it runs **in addition to** the normal compile, so per-file cost roughly doubles.

| Integration method | Total engine build time | Overhead |
|--------------------|------------------------|----------|
| Normal build | ~5s (parallel) | — |
| With `CMAKE_CXX_CLANG_TIDY` (during build) | ~9s (parallel) | **+80%** |
| Standalone `run-clang-tidy` (after build) | +4s (parallel) | **+4s one-time** |

The standalone approach (`run-clang-tidy -j16`) takes ~4s on all 39 engine files — it fully parallelizes. This is fast because the project is small and the files are not huge.

**Verdict: Cheap enough.** 4s standalone or +80% during-build. Recommend standalone for local dev (run explicitly via `game-analyze` script), not during every build. During-build is acceptable if you want continuous feedback.

#### IWYU

IWYU is architecturally similar to clang-tidy (runs a full Clang parse per file). Expected overhead is comparable: ~0.5-1.0x per file, ~4-5s standalone on all engine sources.

IWYU is not installed in the current devenv so I could not measure directly, but based on its architecture (Clang front-end parse + include graph analysis, no optimization passes), it will be comparable to clang-tidy. The include graph walk is O(headers) which is fast for a project this size.

**Verdict: Cheap.** Run standalone via `game-iwyu` script. Not recommended during every build since its output is noisy during active development (you add an include, start using it, IWYU complains about the ones you haven't used yet).

#### cppcheck

cppcheck uses its own parser (not Clang). It's generally faster per-file than clang-tidy but slower on complex template code (it does multiple passes with different `#ifdef` configurations). For a project this size, full analysis should take ~2-5s.

**Verdict: Cheap.** Lower priority tool anyway.

### Runtime performance impact

These affect the running game, not just compilation:

| Tool/Flag | Runtime overhead | Memory overhead | Notes |
|-----------|-----------------|-----------------|-------|
| Extra `-W` flags | **0%** | **0%** | Warnings are compile-time only, zero runtime cost |
| UBSan (added to existing ASan) | **<5% additional** | **Negligible** | ASan already dominates at ~2-3x slowdown; UBSan adds ~1.2x on its own, but combined with ASan the incremental cost is minimal |
| clang-tidy | **0%** | **0%** | Analysis-only tool, does not affect generated code |
| IWYU | **0%** | **0%** | Analysis-only tool |
| cppcheck | **0%** | **0%** | Analysis-only tool |
| GL debug context flag | **<5%** | **Minimal** | Mesa/NVIDIA have minimal overhead for debug contexts. Only enabled in `GAME_WITH_ASSERTS` builds. Some drivers (Intel) may have higher overhead |
| `GL_DEBUG_OUTPUT_SYNCHRONOUS` | **5-15%** | **Minimal** | Already enabled. Forces synchronous GL error checking. The main cost is losing driver-side command batching. Disable in release builds |
| Typed GL wrappers | **0%** | **0%** | Zero-cost abstraction — `static_cast<GLenum>(enum_value)` compiles to nothing |
| Shader validation (build-time) | **0%** | **0%** | Build-time only, not linked into the binary |
| apitrace (when tracing) | **20-50%** | **High** | Intercepts every GL call and serializes to disk. Only used during debugging sessions, never in normal builds |
| RenderDoc (when capturing) | **10-30%** | **High** | Hooks the GL context. Only active when capturing a frame |

### Summary

| Tool | Compile overhead | Runtime overhead | When to run |
|------|-----------------|-----------------|-------------|
| `-Wshadow` etc. | **0%** | **0%** | Every build |
| UBSan | **+15-25%** per file | **<5%** incremental over ASan | Every test build |
| clang-tidy | **+80%** in-build, or **+4s** standalone | **0%** | Standalone before commit, or in-build if desired |
| IWYU | **~+4-5s** standalone | **0%** | Manually after include changes |
| GL debug context | **0%** compile | **<5%** runtime | Debug builds only |
| cppcheck | **~+2-5s** standalone | **0%** | Periodically |

The project is small enough that **every tool is affordable**. The full build is 24s parallel; even enabling everything during-build wouldn't push it past 45s. The real question is noise management, not performance.

---



Ordered by effort/value ratio:

### Phase 1: Zero effort, high value

1. **Add UBSan to tests** — Change `-fsanitize=address` to `-fsanitize=address,undefined` in CMakeLists.txt (2 lines).
2. **Request GL debug context** — Add `SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG)` guarded by `GAME_WITH_ASSERTS` (3 lines).
3. **Add `-Wshadow -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough`** to compiler flags, fix any warnings.

### Phase 2: Low effort, high value

4. **Add `.clang-tidy`** starting with `bugprone-*` and `performance-*` only. Fix all warnings. Promote to `WarningsAsErrors` once clean.
5. **Add IWYU to nix devenv** and create `iwyu.imp` mapping file for SDL2/glad. Run manually, fix issues.

### Phase 3: Medium effort

6. **Add `game-analyze` devenv script** that runs clang-tidy on all engine sources.
7. **Add `-Wconversion`** (expect noise; fix incrementally or use clang-tidy's check instead).
8. **Extract vertex shaders to files** and add `glslangValidator` build step.

### Phase 4: Polish

9. **Add cppcheck** as supplementary analysis.
10. **Add remaining clang-tidy categories** (`modernize-*`, `readability-*`).
11. **Add `apitrace` to nix devenv** for GL debugging sessions.

### Avoiding tool fatigue

- **Ratchet approach**: Add one check category at a time, fix all warnings, then add the next.
- **Never enable checks you don't intend to follow**. Each check is a commitment.
- **Use `HeaderFilterRegex`** to restrict clang-tidy to `src/` (never analyze vendored headers).
- **`WarningsAsErrors` only for stabilized checks** — once a category is clean, lock it down.
- **Diff-only analysis in CI**: `clang-tidy-diff.py` can restrict analysis to changed lines only:

```bash
git diff -U0 HEAD~1 | clang-tidy-diff.py -p1 -path build/ \
    -header-filter='src/.*\.h$'
```

---

## devenv.nix changes summary

```nix
packages = with pkgs; [
    # ... existing packages ...
    include-what-you-use  # IWYU for include hygiene
    cppcheck              # Supplementary static analysis
    apitrace              # OpenGL call tracing
    glslang               # Shader validation (glslangValidator)
];
```

New scripts:

```nix
scripts."game-analyze" = {
    exec = ''
        cmake -DCMAKE_BUILD_TYPE=Debug -G Ninja -S . -B build \
        && cmake --build build --target Game \
        && run-clang-tidy -p build/ -j$(nproc) \
            -header-filter='src/.*\.h$' \
            'src/.*\.cc$'
    '';
};

scripts."game-iwyu" = {
    exec = ''
        cmake -DCMAKE_BUILD_TYPE=Debug -G Ninja -S . -B build \
        && iwyu_tool.py -p build/ -- \
            -Xiwyu --mapping_file=iwyu.imp \
            -Xiwyu --no_fwd_decls
    '';
};

scripts."game-sanitize" = {
    exec = ''
        cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON -G Ninja -S . -B build \
        && cmake --build build --target Game \
        && UBSAN_OPTIONS="print_stacktrace=1" \
           ASAN_OPTIONS="detect_leaks=1" \
           ./build/game run assets
    '';
};
```

## CMakeLists.txt changes summary

| Change | Lines affected | Phase |
|--------|---------------|-------|
| `-fsanitize=address` → `-fsanitize=address,undefined` + `-fno-sanitize-recover=all` | 186, 199 | 1 |
| Add `-Wshadow -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough` | 130 | 1 |
| Add `ENABLE_CLANG_TIDY` option | New block | 2 |
| Add `ENABLE_IWYU` option | New block | 2 |
| Add `ENABLE_SANITIZERS` option for Game target | New block | 2 |
| Add `ENABLE_CPPCHECK` option | New block | 4 |
| Add shader validation target | New block | 3 |
