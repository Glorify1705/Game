# C++ Code Style Guide

This project's style is based on the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
with significant modifications for a memory-constrained, allocation-aware game
engine. Where this document is silent, defer to Google style. Where this
document contradicts Google style, this document wins.

The guiding aesthetic is closer to Zig than to idiomatic modern C++: prefer
explicit over implicit, make allocations visible, avoid hidden control flow,
and keep the code simple enough to reason about locally.

## Table of Contents

- [Philosophy](#philosophy)
- [Memory Management](#memory-management)
- [Naming](#naming)
- [Formatting](#formatting)
- [Files and Headers](#files-and-headers)
- [Namespaces](#namespaces)
- [Classes and Structs](#classes-and-structs)
- [Functions](#functions)
- [Containers and Data Structures](#containers-and-data-structures)
- [C++17 Features](#c17-features)
- [Forbidden Features](#forbidden-features)
- [Error Handling](#error-handling)
- [Templates](#templates)
- [Comments](#comments)
- [Constants and Enums](#constants-and-enums)
- [Macros](#macros)
- [API Design](#api-design)
- [Operator Overloading](#operator-overloading)
- [Dependencies and Vendoring](#dependencies-and-vendoring)
- [Build System](#build-system)
- [Testing](#testing)
- [Compiler Configuration](#compiler-configuration)

---

## Philosophy

This is a game engine. The runtime is a fixed-memory environment where every
allocation is deliberate and the total memory footprint is bounded at startup.
The code must be understandable by reading it top to bottom, without needing to
trace through layers of abstraction to figure out when and where memory is
allocated, freed, or who owns it.

**Core principles:**

1. **No hidden allocations.** If a function allocates, it takes an
   `Allocator*`. If it doesn't take one, it doesn't allocate. This is the
   single most important rule in this codebase — borrowed from Zig's design
   philosophy where allocators are always explicit parameters.

2. **Memory is a budget, not a resource.** The game allocates its memory
   upfront (arenas, pools, block allocators) and subdivides it. System
   `malloc`/`free` should only appear inside allocator implementations or
   during one-time initialization.

3. **Simplicity over abstraction.** Three similar lines of code are better
   than a premature abstraction. Write the obvious thing. If you need a
   pattern three times, maybe extract it. Maybe not.

4. **Explicit over implicit.** Prefer explicit initialization, explicit
   cleanup, explicit ownership. If the reader has to guess what a line of
   code does, rewrite it.

5. **Local reasoning.** A reader should be able to understand what a function
   does by reading only that function and its signature. Minimize ambient
   state, global dependencies, and action-at-a-distance.

---

## Memory Management

### Allocator-First Design

Every type that can allocate memory takes an `Allocator*` in its constructor
and uses it for all allocations. This is non-negotiable.

```cpp
// Good: allocation source is explicit.
DynArray<int> items(allocator);
auto* node = allocator->New<Node>();

// Bad: hidden allocation via STL.
std::vector<int> items;  // Where does this memory come from?
auto node = std::make_unique<Node>();  // Whose allocator? Which heap?
```

### Allocator Hierarchy

The project provides these allocator types in `allocators.h`:

| Allocator           | Use Case                                        |
|---------------------|-------------------------------------------------|
| `ArenaAllocator`    | Bump allocator for frame-scoped or phase-scoped |
| `StaticAllocator<N>`| Stack-backed arena with compile-time size       |
| `FreeList<T>`       | Object pool for a single type                   |
| `BlockAllocator<T>` | Fixed-size block pool                           |
| `SystemAllocator`   | Wraps malloc/free — use sparingly               |

**Use `SystemAllocator` only when:**
- Bootstrapping allocators that will serve all later allocations.
- One-time startup initialization that runs before the arena is set up.
- Inside test scaffolding.

### Avoiding System Malloc

Do not call `malloc`, `free`, `new`, `delete`, `calloc`, or `realloc`
directly. Use the `Allocator` interface:

```cpp
// Good
T* ptr = allocator->New<T>(args...);
allocator->Destroy(ptr);

T* arr = allocator->NewArray<T>(count);
allocator->DeallocArray(arr, count);

// Bad
T* ptr = new T(args...);
delete ptr;

T* arr = (T*)malloc(count * sizeof(T));
free(arr);
```

### Scratch Allocators

For temporary per-frame allocations, pass a scratch arena:

```cpp
void Render(Allocator* scratch);
```

The caller resets the scratch arena each frame. Code using the scratch
allocator must not hold pointers into it across frames.

### Placement New

Use placement `new` for constructing objects in allocator-provided memory:

```cpp
T* ptr = reinterpret_cast<T*>(allocator->Alloc(sizeof(T), alignof(T)));
::new (ptr) T(std::forward<Args>(args)...);
```

Note the `::new` — always use global placement new to avoid any overloaded
`operator new`.

### ASAN and Valgrind Integration

Allocators must cooperate with AddressSanitizer and Valgrind. Use the provided
macros from `allocators.h`:

```cpp
ASAN_POISON_MEMORY_REGION(addr, size);    // Mark memory as inaccessible.
ASAN_UNPOISON_MEMORY_REGION(addr, size);  // Mark memory as accessible.
```

Poison memory when it is returned to a pool or arena. Unpoison it when handed
out. This catches use-after-free in custom allocators the same way ASAN catches
it for the system heap.

---

## Naming

Naming follows Google style with some refinements observed in this codebase.

### Types

- **Classes and structs:** `PascalCase` — `BatchRenderer`, `ArenaAllocator`,
  `DynArray`.
- **Template parameters:** `PascalCase` — `T`, `Args`, `Size`.
- **Type aliases:** `PascalCase` or `snake_case` matching what they alias —
  `using LogSink = void (*)(...)` or `using type = float`.

### Functions and Methods

- **All functions and methods:** `PascalCase` —  `Alloc()`, `Push()`,
  `LoadTexture()`, `SetActiveColor()`.
- **Accessors that expose a field directly** may use `snake_case` matching
  the field name without the trailing underscore — `size()`, `empty()`,
  `capacity()`, `used_memory()`. This follows the C++ standard library
  convention for container-like accessors and makes range-for and generic code
  work naturally.

### Variables

- **Local variables:** `snake_case` — `buffer_ptr`, `element_count`, `result`.
- **Member variables:** `snake_case` with a trailing underscore —
  `allocator_`, `buffer_`, `pos_`, `events_`.
- **Static and global constants:** `k` prefix + `PascalCase` —
  `kMaxAlign`, `kPixelsPerMeter`, `kCommandMemory`.
- **Compile-time constants in local scope:** `k` prefix + `PascalCase` —
  `kEpsilon`, `kBlockSize`.

### Files

- **Headers:** `.h` extension (not `.hpp`).
- **Source files:** `.cc` extension (not `.cpp` or `.cxx`).
- **File names:** `snake_case` — `batch_renderer.h`, `string_table.cc`. Short
  single-word names are fine — `array.h`, `vec.h`, `defer.h`.

### Enums

- **Enum class names:** `PascalCase` — `ShaderType`, `CommandType`.
- **Enum values:** `k` prefix + `PascalCase` — `kVertex`, `kFragment`,
  `kRenderQuad`.

### Namespaces

- **Namespace names:** All engine code lives in the single project namespace
  `G`. Avoid adding any other named namespace — see the Namespaces section.
  The rare internal header namespace uses the `internal_` prefix
  (`internal_strings`).

### Macros

- **All macros:** `SCREAMING_SNAKE_CASE` — `CHECK`, `DCHECK`, `DEFER`,
  `ASAN_POISON_MEMORY_REGION`.

### Type Prefixes for Math Types

Math types use a type prefix convention:
- `F` for float: `FVec2`, `FVec3`, `FMat4x4`
- `D` for double: `DVec2`, `DVec3`
- `I` for int: `IVec2`, `IVec3`

---

## Formatting

Formatting is enforced by `clang-format`. The project's `.clang-format` is
based on Google style. Key settings:

| Setting                  | Value          |
|--------------------------|----------------|
| Column limit             | 80             |
| Indentation              | 2 spaces       |
| Tabs                     | Never          |
| Brace style              | Attach (K&R)   |
| Pointer alignment        | Left (`int* p`)|
| Namespace indentation    | None           |
| Access modifier offset   | -1             |
| Short functions inline   | Yes            |
| Short if without else    | Single line OK |
| Short loops              | Single line OK |
| Short lambdas            | Single line OK |

Run `clang-format` before committing. Do not argue with the formatter — if it
produces something ugly, restructure the code so the formatted version reads
well.

### Braces

Always use attached braces (K&R style). Opening brace on the same line:

```cpp
if (condition) {
  DoSomething();
} else {
  DoOtherThing();
}

class Foo {
 public:
  void Bar();
};

namespace G {
// ...
}  // namespace G
```

Short single-statement bodies may omit braces when on the same line as the
control statement:

```cpp
if (ptr == nullptr) return;
for (size_t i = 0; i < n; ++i) total += data[i];
```

### Trailing Whitespace and Newlines

No trailing whitespace. Files end with exactly one newline. At most one
consecutive blank line.

---

## Files and Headers

### Include Guards

Use both `#pragma once` and traditional include guards (belt-and-suspenders):

```cpp
#pragma once
#ifndef _GAME_MY_HEADER_H
#define _GAME_MY_HEADER_H

// ...

#endif  // _GAME_MY_HEADER_H
```

Guard macro format: `_GAME_UPPER_SNAKE_CASE_H`.

### Include Order

Follow Google include order, enforced by clang-format:

1. System C headers (`<cstdint>`, `<cstring>`, etc.)
2. System C++ headers (`<array>`, `<string_view>`, etc.)
3. Third-party library headers (`"libraries/rapidhash.h"`)
4. Project headers (`"allocators.h"`, `"logging.h"`)

Separate each group with a blank line. Use `""` for project-local headers,
`<>` for system headers.

### What Goes in Headers vs Source Files

**Headers (`.h`):**
- Class/struct declarations.
- Inline functions and templates (must be in headers).
- Small accessor methods defined in the class body.
- `constexpr` / `inline static constexpr` constants.
- Type aliases and forward declarations.

**Source files (`.cc`):**
- Non-template function implementations.
- Large method bodies.
- File-local helpers in anonymous namespaces.
- Static variable definitions.

### One Class Per Header (Guideline, Not Law)

Small tightly-related types can share a header. For example, `vec.h` defines
all vector types. `array.h` defines `FixedArray`, `DynArray`, `Slice`, and
`ArrayView`. Use judgment — if a header is getting large or the types are
independently useful, split them.

---

## Namespaces

All game engine code lives in `namespace G`. **Avoid adding any other named
namespace.** A flat structure inside `G` is strongly preferred: no nested
namespaces for organizational grouping, no `detail` / `impl` sub-namespaces
for header internals, no per-subsystem wrappers. If you feel the urge to add
one, reach for a different tool first — name-prefix the symbols, put them in
an anonymous namespace in a `.cc`, or make them private members of a class.

```cpp
namespace G {
// All project code here.
}  // namespace G
```

The only acceptable exceptions are:

- `namespace {}` (anonymous) in `.cc` files for file-local helpers — see
  below.
- `namespace internal_foo` (with the `internal_` prefix) **only** when a
  header genuinely must expose implementation details that callers must not
  touch, and hiding them any other way is impractical. Prefer not to need
  this.

Use anonymous namespaces in `.cc` files for file-local helpers.
**Never** use `static` for file-local functions — always use an anonymous
namespace instead:

```cpp
// Good
namespace {
int Helper() { return 42; }
}  // namespace

// Bad
static int Helper() { return 42; }
```

**Never** use `using namespace` in headers. In `.cc` files, prefer explicit
qualification or selective `using` declarations.

Do not indent code inside `namespace G { }` — this matches clang-format's
`NamespaceIndentation: None` setting.

---

## Classes and Structs

### When to Use Struct vs Class

Following Google style with a practical bent:

- **Use `struct`** for passive data with public fields: POD types, config
  bundles, math types (`FVec2`, `Color`), command structs
  (`RenderQuad`, `SetTexture`).
- **Use `class`** when there are invariants to maintain, private state, or
  non-trivial methods: `ArenaAllocator`, `BatchRenderer`, `Dictionary<T>`.

### Member Ordering

Within a class, order sections as:

1. `public` — the interface users see first.
2. `protected` — rarely used, but before private if present.
3. `private` — implementation details last.

Within each access level:

1. Types and type aliases.
2. Static constants.
3. Constructors and destructor.
4. Methods.
5. Data members.

### Constructors

- Use constructor initializer lists. Break before the colon:
  ```cpp
  BatchRenderer::BatchRenderer(IVec2 viewport, Shaders* shaders,
                               Allocator* allocator)
      : allocator_(allocator),
        commands_(kMaxCommands, allocator),
        shaders_(shaders),
        viewport_(viewport) {}
  ```

- If a constructor allocates, it must take an `Allocator*`.
- Prefer explicit single-argument constructors.
- Use `= default` for trivial default constructors and destructors.
- Use `= delete` for suppressed copy/move operations.

### Rule of Zero / Rule of Five

- Trivially copyable types: don't write special members — rely on defaults.
- Types that manage resources through an `Allocator*`: implement destructor,
  delete copy constructor and copy assignment, and implement move
  constructor and move assignment if the type needs to be movable.
- Do not write copy constructors that allocate. If you need to duplicate a
  structure, provide an explicit `Clone(Allocator*)` method.

### Ownership

- **Raw pointers are non-owning** by default. The allocator that created an
  object is responsible for its lifetime.
- If a class takes ownership of a pointer, document it clearly in the
  constructor or field comment.
- Do not use `std::unique_ptr`, `std::shared_ptr`, or any smart pointer.
  Lifetime management goes through allocators.

---

## Functions

### Parameter Ordering

Following Google style and Zig convention — outputs before inputs, allocator
first when present:

```cpp
void RenderFrame(Allocator* scratch, const Scene& scene, Framebuffer* out);
```

When a function uses but does not store the allocator (scratch/temporary
usage), name the parameter `scratch`. When it stores the allocator for later
use, name it `allocator`.

### Size Parameters

When passing a pointer-and-length pair, put the pointer first and the length
immediately after:

```cpp
void ProcessData(const uint8_t* data, size_t size);
```

This mirrors `Slice<T>` semantics and is the convention in C APIs, Zig, and
this codebase.

### Return Values

- Return by value for small types (vectors, colors, handles, small structs).
- Return a pointer for allocated objects (caller knows the allocator).
- Return `bool` for success/failure with out-parameters for data:
  ```cpp
  bool Lookup(std::string_view key, T* value) const;
  ```
- Use `[[nodiscard]]` on functions where ignoring the return value is a bug.

### Parameter Comments for Literals

When passing literal values (numbers, booleans) to a function, add a
`/*parameter_name=*/` comment so the callsite is self-documenting:

```cpp
// Good
spatial_hash_.Init(cell_size, /*table_size=*/1024, allocator);

// Bad
spatial_hash_.Init(cell_size, 1024, allocator);
```

### Short Functions

Short functions that fit on one line are fine as inline definitions:

```cpp
bool empty() const { return elems_ == 0; }
size_t size() const { return elems_; }
```

---

## Containers and Data Structures

### Project Containers

This project provides its own container types. Use them instead of STL
containers:

| This Project        | Instead of STL            | Notes                              |
|---------------------|---------------------------|------------------------------------|
| `FixedArray<T>`     | `std::vector<T>` (fixed)  | Fixed capacity, explicit allocator |
| `DynArray<T>`       | `std::vector<T>`          | Growable, explicit allocator       |
| `Slice<T>`          | `std::span<const T>`      | Non-owning read-only view          |
| `ArrayView<T>`      | `std::span<const T>`      | Non-owning const iterator view     |
| `Dictionary<T>`     | `std::unordered_map`      | String-keyed, explicit allocator   |
| `FixedStringBuffer` | `std::string`             | Fixed-capacity string buffer       |
| `StringBuffer`      | `std::ostringstream`      | Non-allocating string formatting   |
| `NullTerminated`    | —                         | Ensures string_view is C-safe      |
| `FreeList<T>`       | —                         | Object pool                        |
| `BlockAllocator<T>` | —                         | Fixed-block pool                   |

### Forbidden STL Containers

Do **not** use any of the following in engine code (test code may use them
sparingly):

- `std::vector`, `std::list`, `std::deque`, `std::set`, `std::map`
- `std::unordered_map`, `std::unordered_set`
- `std::string` as a stored member (use `std::string_view` with interned
  strings, or `FixedStringBuffer` / `StringBuffer`)
- `std::queue`, `std::stack`, `std::priority_queue`
- `std::any`, `std::variant` (if they allocate)

### Allowed STL Headers

These STL headers are fine — they don't allocate or provide useful
type-level utilities:

- `<array>` — `std::array` for fixed-size stack arrays.
- `<bitset>` — `std::bitset` for flag sets.
- `<string_view>` — non-owning string references.
- `<type_traits>` — compile-time type inspection.
- `<utility>` — `std::move`, `std::forward`, `std::pair`.
- `<cstdint>`, `<cstddef>`, `<cstring>`, `<cstdlib>`, `<cmath>`,
  `<cstdio>`, `<cstdarg>` — C standard library wrappers.
- `<limits>` — `std::numeric_limits`.
- `<algorithm>` — `std::min`, `std::max`, `std::sort`, etc. — fine when
  operating on project containers.
- `<initializer_list>` — for variadic construction patterns.
- `<new>` — for placement new.
- `<atomic>` — when truly needed for thread safety.

### std::string

`std::string` may appear as a **transient local variable** (for building a
result that is immediately consumed) or in startup/shutdown paths.  It must
not appear as a **class member** in engine types. Prefer:
- `std::string_view` + string interning for identifiers and keys.
- `FixedStringBuffer<N>` for formatted output.
- `allocator->StrDup(s)` for arena-owned copies.

`StrCat(...)` and `StrAppend(...)` from `stringlib.h` are the preferred
string formatting utilities.

### StringBuffer Named Aliases

Use the named aliases from `stringlib.h` instead of raw
`FixedStringBuffer<N>` when the size matches a standard use case:

| Alias          | Size   | Use Case                              |
|----------------|--------|---------------------------------------|
| `Str`          | 256    | General-purpose short strings         |
| `PathBuffer`   | 256    | Virtual filesystem paths              |
| `LogBuffer`    | 511    | Log lines (with `kTruncating`)        |
| `CmdBuffer`    | 1024   | CLI paths, system commands            |
| `SqlBuffer`    | 1024   | SQL statements                        |
| `SmallBuffer`  | 64     | Thread names, vector `__tostring`     |

For buffers that may truncate (log lines, error messages), construct with
the `kTruncating` tag instead of calling `AllowTruncation()` separately:

```cpp
// Good: single-step construction.
FixedStringBuffer<kMaxLogLineLength> buf(kTruncating);
buf.Append("[", file, ":", line, "] ", message);

// Bad: two-step pattern.
FixedStringBuffer<kMaxLogLineLength> buf;
buf.AllowTruncation();
```

### NullTerminated for C API Interop

When passing a `std::string_view` to a C API that requires `const char*`,
use `NullTerminated` instead of creating a full `FixedStringBuffer`:

```cpp
// Good: lightweight, zero-copy when already null-terminated.
PHYSFS_openRead(NullTerminated(filename));

// Bad: allocates a full StringBuffer just for null-termination.
FixedStringBuffer<256> path(filename);
PHYSFS_openRead(path.str());
```

---

## C++17 Features

This project uses **C++17** (`cxx_std_17`). Use C++17 features freely as long
as they do not introduce implicit heap allocations.

### Encouraged

- **`if constexpr`** — compile-time branching in templates. Strongly preferred
  over SFINAE when possible:
  ```cpp
  if constexpr (std::is_trivially_destructible_v<T>) {
    // skip destructor call
  }
  ```
- **Structured bindings** — for destructuring pairs, tuples, and small structs:
  ```cpp
  auto [x, y] = GetPosition();
  ```
- **`std::string_view`** — non-owning string references. The default way to
  pass strings. Prefer `std::string_view` over `const char*` when comparing
  strings — use `==` on `string_view` instead of `strcmp`.
- **`[[nodiscard]]`** — on functions where ignoring the return is likely a bug.
- **`[[maybe_unused]]`** — to suppress warnings on intentionally unused
  variables or conditionally-used parameters (e.g. `#ifdef` branches). For
  unconditionally unused function parameters, prefer commenting out the name
  instead (e.g. `int /*start*/`). Do not use `(void)param;` casts.
- **`[[noreturn]]`** — on functions like `Crash()`.
- **Fold expressions** — for variadic template parameter packs.
- **Inline variables** — `inline static constexpr` for constants in headers.
- **`constexpr` if/else** — compute values at compile time wherever possible.
- **Class template argument deduction (CTAD)** — when it makes code clearer:
  `ArrayView(ptr, size)` instead of `ArrayView<T>(ptr, size)`.
- **`std::optional`** — stack-only, no allocation. Fine for return types.

### Avoid

- **`std::any`** — type erasure with potential heap allocation. Use unions or
  tagged variants instead.
- **`std::variant`** — fine in principle but the visitor pattern (`std::visit`)
  has overhead. Prefer explicit tagged unions with a `switch` when
  performance matters.
- **`std::filesystem`** — may allocate. Use PhysFS (vendored) for virtual
  filesystem operations and `platform.h` (`MakeDir`, `MakeDirs`, `FileExists`,
  etc.) for real filesystem path operations. Do not use SDL filesystem functions
  (`SDL_CreateDirectory`, etc.) — keep path operations in PhysFS or platform.h.
- **`std::regex`** — allocates heavily. Write manual parsers or use simple
  string matching.

---

## Forbidden Features

These are banned in this project. No exceptions.

### Exceptions

Do not use C++ exceptions. Compile with `-fno-exceptions`. Error handling uses
`CHECK`, `DCHECK`, and return values. Fatal errors crash immediately — this is
a game, not a server.

### RTTI

Do not use `dynamic_cast`, `typeid`, or any feature that requires runtime
type information. Compile with `-fno-rtti`.

### The Global `new` and `delete`

Do not use `new` or `delete` (the global operators). All allocation goes
through `Allocator*`. Placement `::new` is fine and expected.

**Exception — intentionally-leaked singletons:** Following Google style, use
`static T* p = new T(...)` for singletons that must survive until process
exit. This avoids the
[static destruction order fiasco](https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use)
where a `static T t;` would have its destructor run at exit, potentially after
other statics it depends on are already destroyed. The leaked `new` is
intentional — the OS reclaims the memory on exit:

```cpp
// Good: intentionally leaked, survives past other statics.
static ColorTable* table = new ColorTable;

// Bad: destructor runs at exit in unpredictable order.
static ColorTable table;  // Only safe if ColorTable has a trivial destructor
                          // and no dependencies on other statics.
```

### Smart Pointers

Do not use `std::unique_ptr`, `std::shared_ptr`, or `std::weak_ptr`. Lifetime
is managed through allocators, arenas, and explicit cleanup.

### Streams for Formatting

Do not use `std::ostringstream`, `std::stringstream`, or iostream-based
formatting for runtime string building. Use `StrCat`, `StrAppend`,
`StringBuffer`, or `FixedStringBuffer`. `std::ostream& operator<<` overloads
are fine for debug output and test assertions.

`StringBuffer` supports `operator+=` and `operator<<` as alternatives to
`Append()`. Use `Append()` for multi-value appends (variadic), and the
operators for single-value chaining when it reads more naturally:

```cpp
// Variadic Append — preferred for building a string in one call.
buf.Append("x=", x, " y=", y);

// Stream-style — fine for incremental building.
buf << "x=" << x << " y=" << y;
```

### Raw Thread Creation

Do not create threads directly with `std::thread`, `pthread_create`, or
`SDL_CreateThread`. All concurrent work must go through the `Executor`
interface (`executor.h`):

- Use `executor->Submit(&task)` for async work.
- Use `executor->ParallelFor(...)` for data-parallel loops.
- Use `InlineExecutor` in tests and single-threaded code paths.

The engine creates a single `ThreadPoolExecutor` at startup. Subsystems that
need concurrency accept an `Executor*` parameter, the same way they accept an
`Allocator*`. This prevents uncoordinated thread creation and
oversubscription.

### `goto`

Do not use `goto`. Use `DEFER`, a boolean flag with `break`, or a small RAII
helper for cleanup. `goto` obscures control flow and makes local reasoning
harder.

---

## Error Handling

### Fatal Errors — `CHECK` and `DIE`

Use `CHECK(condition, message...)` for invariants that must hold in all builds.
If the condition fails, the program crashes immediately with file, line, and
message:

```cpp
CHECK(index < size_, "Index ", index, " out of bounds (size ", size_, ")");
```

Use `DIE(message...)` for unconditionally fatal paths:

```cpp
default:
  DIE("Unknown command type: ", static_cast<int>(type));
```

### Debug-Only Checks — `DCHECK`

Use `DCHECK(condition, message...)` for invariants that are expensive to check
or only relevant during development. These compile to nothing when
`GAME_WITH_ASSERTS` is not defined:

```cpp
DCHECK(elems_ < capacity_, elems_, " vs ", capacity_);
```

### Null Checks — `NOTNULL`

Use `NOTNULL(ptr)` as a concise crash-if-null:

```cpp
auto* renderer = NOTNULL(GetRenderer());
```

### Return Values for Non-Fatal Failures

Functions that can fail non-fatally return `bool` (success/failure) with
out-parameters, or return `nullptr` to indicate "not found":

```cpp
bool Lookup(std::string_view key, T* value) const;
T* FindEntity(EntityId id);  // Returns nullptr if not found.
```

### DEFER for Cleanup

Use the `DEFER` macro for scope-based cleanup. This is the primary RAII
mechanism in this codebase:

```cpp
auto* stmt = PrepareStatement(db, query);
DEFER([&] { sqlite3_finalize(stmt); });
```

DEFER runs at scope exit, like Zig's `defer` or Go's `defer`. Use it for
any cleanup that should happen regardless of the exit path.

---

## Templates

### When to Use Templates

Templates are appropriate for:
- **Container types** — `DynArray<T>`, `FreeList<T>`, `Slice<T>`.
- **Allocator helpers** — `New<T>()`, `NewArray<T>()`, `Destroy<T>()`.
- **Compile-time dispatch** — `if constexpr` on type traits.
- **Variadic formatting** — `StrCat(...)`, `Log(...)`.

### When Not to Use Templates

Do not use templates for:
- Runtime polymorphism — use virtual functions (sparingly) or function
  pointers.
- Code that only ever instantiates one type — just write the concrete version.
- Complex metaprogramming that obscures intent — keep it simple.

### Template Implementation Location

Templates must be defined in headers. For large template implementations,
keep the declaration readable and put the implementation below the class or
at the bottom of the header.

### SFINAE vs `if constexpr`

Prefer `if constexpr` over SFINAE (`std::enable_if`) whenever possible:

```cpp
// Good: clear and direct.
template <typename T>
void Destroy(T* ptr) {
  if constexpr (!std::is_trivially_destructible_v<T>) {
    ptr->~T();
  }
  Dealloc(ptr, sizeof(T));
}

// Avoid: harder to read.
template <typename T, typename = std::enable_if_t<!std::is_trivially_destructible_v<T>>>
void Destroy(T* ptr);
```

---

## Comments

### When to Comment

Comment the **why**, not the **what**. If the code is clear, don't add a
comment. If the code is tricky, explain the reasoning:

```cpp
// We duplicate the origin angle and color for every vertex in the quad
// to avoid having to reset a uniform on drawing every colored rotated quad,
// which would require an OpenGL context switch.
```

### Style

- Use `//` for all comments, never `/* */` (except for disabling code blocks
  temporarily).
- **No banner comments.** Do not use decorative section dividers like
  `// --- Section Name ---` or `// ========`. If code needs grouping, use
  a blank line and an ordinary comment.
- Two spaces before trailing comments:

  ```cpp
  int result = 0;  // Accumulator.
  ```

- Comment closing braces of namespaces:

  ```cpp
  }  // namespace G
  ```

### Documentation Comments

All public classes, structs, and their public methods must have a one-line `//`
comment above the declaration explaining their purpose:

```cpp
// 2D camera with follow, shake, and viewport bounds.
class Camera {
 public:
  // Advances follow-lerp, bounds clamping, and shake decay.
  void Update(float dt, FVec2 viewport);
};
```

Every member of a public enum must have a trailing `//` comment explaining
its meaning:

```cpp
// Discriminant for the Timer union.
enum TimerType : uint8_t {
  kAfter,     // Fire once after a delay.
  kEvery,     // Fire repeatedly at a fixed interval.
};
```

For public API functions that are non-obvious, a one-line `//` comment above
the declaration is sufficient:

```cpp
// Returns the previous color.
Color SetColor(Color color);
```

Do not use Doxygen-style comments (`///`, `/** */`, `@param`). This is not a
library — the audience is the development team, not external consumers.

---

## Constants and Enums

### Constants

Use `inline static constexpr` for constants in headers:

```cpp
inline static constexpr size_t kMaxAlign = alignof(std::max_align_t);
inline static constexpr size_t kAtlasWidth = 2048;
```

Use `constexpr` for local constants:

```cpp
constexpr float kEpsilon = 1e-10f;
```

### Enums

Prefer `enum class` for type safety:

```cpp
enum class ShaderType : uint32_t {
  kVertex,
  kFragment,
};
```

Plain `enum` is acceptable when the values are used as bit fields, array
indices, or when the verbosity of `enum class` hurts readability (e.g.,
for internal command types in a command buffer):

```cpp
enum CommandType : uint32_t {
  kRenderQuad = 1,
  kRenderTrig,
  kSetTexture,
  // ...
};
```

Always specify the underlying type explicitly.

Enum values use the `k` prefix: `kValue`, `kOtherValue`.

### Exhaustive Switch Statements

When switching over an enum, handle every enumerator and **omit the `default`
case**. This way the compiler warns (and with `-Werror`, errors) when a new
enumerator is added but not handled:

```cpp
std::string_view CommandName(CommandType t) {
  switch (t) {
    case kRenderQuad: return "RenderQuad";
    case kRenderTrig: return "RenderTrig";
    case kSetTexture: return "SetTexture";
    // ... every case ...
  }
  DIE("Unexpected CommandType: ", static_cast<int>(t));
}
```

The code after the `switch` handles the (theoretically impossible) case where
the enum holds a value outside its enumerators. Use `DIE()` — if we get
there, something is deeply wrong.

See [Abseil Tip #147](https://abseil.io/tips/147).

---

## Macros

Macros are a necessary evil. Use them for:

- **Assertions and logging** — `CHECK`, `DCHECK`, `LOG`, `DIE`, `NOTNULL`.
- **Scope guards** — `DEFER`.
- **Platform-specific attributes** — `ALLOCATOR_NO_ALIAS`.
- **Compile-time feature flags** — `#ifdef GAME_WITH_ASSERTS`.
- **OpenGL call wrapping** — `OPENGL_CALL`.

**Do not** use macros as a substitute for functions or templates. If it can be
a `constexpr` function or a template, write it that way.

Name all macros `SCREAMING_SNAKE_CASE`. Macros that generate unique names
should use `__COUNTER__` (not `__LINE__`) for uniqueness.

Undefine macros that are only needed temporarily (`#undef`).

---

## API Design

These guidelines are drawn from
[Abseil's Tips of the Week](https://abseil.io/tips/) — a collection of C++
best practices from Google's codebase. The tips below are the ones most
relevant to this project.

### Prefer Enums Over Bool Parameters

A bare `true` or `false` at a callsite tells the reader nothing. Use an enum
instead so the callsite is self-documenting:

```cpp
// Bad: what does `true` mean here?
LoadTexture(data, width, height, true);

// Good: the callsite reads like prose.
enum class FilterMode { kNearest, kLinear };
LoadTexture(data, width, height, FilterMode::kLinear);
```

This is especially important when a function has **multiple** bool parameters
— it's nearly impossible to remember the order. If you're defining a function
that takes a bool, stop and ask whether an enum with two values would be
clearer. The answer is almost always yes.

For existing functions that take bools, use `/*parameter_name=*/` comments
at the callsite as a stopgap:

```cpp
ParseConfig(path, /*hot_reload=*/false, /*strict=*/true);
```

See [Abseil Tip #94](https://abseil.io/tips/94).

### Prefer Return Values Over Output Parameters

Return values are easier to reason about than output pointers. They make
ownership clear, enable `const`, and compose naturally:

```cpp
// Prefer: clear ownership, works with const and auto.
ExtractResult ExtractSpecs(const Doodad& doodad);

// Avoid: unclear if out-params are input, output, or both.
bool ExtractSpecs(const Doodad& doodad, FooSpec* foo, BarSpec* bar);
```

Use `std::optional<T>` for functions that may not produce a result.

Output parameters are acceptable for **hot-path** code where the caller
wants to reuse an existing buffer (e.g., `Render(Allocator* scratch)`),
or for the established `bool Lookup(key, T* out)` pattern where the
boolean already signals success/failure. Don't add a third pattern.

See [Abseil Tip #176](https://abseil.io/tips/176).

### Avoid Sentinel Values

Don't use magic values (`-1`, `0xFFFFFFFF`, `nullptr` for "not found") when
`std::optional` exists and communicates intent:

```cpp
// Bad: caller must know -1 means "not found."
int FindIndex(std::string_view name);

// Good: type system forces the caller to check.
std::optional<size_t> FindIndex(std::string_view name);
```

When `std::optional` is not appropriate (e.g., returning a pointer where
null is the natural "not found"), that's fine — `nullptr` is a well-understood
convention for pointers. The rule targets **integer and float sentinels** that
are easy to forget.

See [Abseil Tip #171](https://abseil.io/tips/171).

### Option Structs for Many Parameters

When a function accumulates more than 3-4 parameters (especially if several
have defaults), group the optional ones into a struct:

```cpp
struct TextOptions {
  float scale = 1.0f;
  Color color = Color::White();
  bool wrap = false;
};

void DrawText(std::string_view text, FVec2 position,
              const TextOptions& opts = {});
```

This avoids long parameter lists, makes defaults visible, and allows callers
to set only the fields they care about.

See [Abseil Tip #173](https://abseil.io/tips/173).

### Use `if` and `switch` Initializers

C++17 allows declaring variables inside `if` and `switch` statements. Use
this to limit variable scope:

```cpp
// Good: `it` doesn't leak into the outer scope.
if (auto* sprite = GetSprite(name); sprite != nullptr) {
  Draw(sprite);
}

// Bad: `sprite` is visible after the if block.
auto* sprite = GetSprite(name);
if (sprite != nullptr) {
  Draw(sprite);
}
```

This is especially useful for lookup-then-use patterns.

See [Abseil Tip #165](https://abseil.io/tips/165).

### Mark Multi-Parameter Constructors `explicit`

Following [Abseil Tip #142](https://abseil.io/tips/142), mark constructors
`explicit` unless the arguments **are** the value (e.g., `FVec2(x, y)` is a
natural implicit conversion from two floats to a point):

```cpp
// Good: implicit — (x, y) *is* the vector.
FVec2(float x, float y);

// Good: explicit — these arguments configure, not represent.
explicit BatchRenderer(IVec2 viewport, Shaders* shaders, Allocator* alloc);
```

### Prefer Unnamed-Namespace Functions

Helper functions that are only used in one `.cc` file should be non-member
functions in an anonymous namespace, not private methods:

```cpp
// In renderer.cc
namespace {
FVec2 CalculateTexCoord(const Sprite& s, int frame) {
  // ...
}
}  // namespace
```

This keeps the class interface small, makes inputs/outputs explicit through
parameters, and is easy to find (same file, above first use).

See [Abseil Tip #186](https://abseil.io/tips/186).

### Good Locals, Bad Locals

Only introduce a local variable when it adds clarity. Don't name things
just to name them:

```cpp
// Bad: the variable adds nothing.
size_t count = array.size();
return count;

// Good: just return directly.
return array.size();

// Good: the name explains a non-obvious expression.
const float half_diagonal = viewport.Length() * 0.5f;
```

See [Abseil Tip #161](https://abseil.io/tips/161).

---

## Operator Overloading

Operator overloading is appropriate for math types (`FVec2`, `FMat4x4`,
`Color`) where the operators have obvious mathematical meaning.

Do not overload operators for non-mathematical types. Prefer named methods:

```cpp
// Good: clear intent.
array.Push(value);
dict.Lookup(key, &result);

// Bad: what does + mean for a dictionary?
dict + entry;
```

For math types, implement operators as member functions when the left operand
is the type, and as free functions when it isn't:

```cpp
FVec2 operator*(float rhs) const;         // vec * scalar
friend FVec2 operator*(float lhs, FVec2 rhs);  // scalar * vec (if needed)
```

Implement `==` and `!=`. For floating-point types, use an epsilon comparison.

---

## Dependencies and Vendoring

### Vendor Everything

All third-party dependencies are vendored in the `libraries/` directory and
built as part of the project. There are no system package dependencies beyond
a C++ compiler, CMake, and standard system libraries (libc, OpenGL, etc.).

**Rationale:** Reproducible builds. Anyone who clones the repo can build it.
No package manager, no version conflicts, no network required.

### Prefer Single-Header Libraries

When choosing a dependency, prefer single-header or few-file libraries:

- `stb_image.h`, `stb_truetype.h`, `stb_vorbis.c` — single-file image/font/audio.
- `rapidhash.h` — single-header hashing.
- `pcg_random.h` — single-header RNG.
- `dr_wav.h` — single-header WAV decoding.

Single-header libraries are easier to vendor, easier to audit, and have no
build system integration overhead.

### Evaluating New Dependencies

Before adding a dependency, ask:

1. **Can we write it ourselves in < 200 lines?** If yes, do that.
2. **Is it a single header or small set of files?** Prefer it.
3. **Does it allocate memory internally?** If it uses malloc, can we patch
   it to use our allocators, or does it offer allocator callbacks?
4. **Does it use exceptions or RTTI?** If yes, reject it or compile it
   separately with those features disabled.
5. **What is its license?** Must be permissive (MIT, BSD, public domain, zlib).

### Vendoring Process

1. Copy the source files into `libraries/`.
2. Add the necessary `CMakeLists.txt` entries.
3. Commit the vendored code as a separate commit with the library name and
   version in the message.

Do not use git submodules. Do not use package managers (conan, vcpkg, etc.).

---

## Build System

### CMake

The project uses CMake (minimum 3.21). Keep CMake code simple:

- One `CMakeLists.txt` at the root.
- Libraries in `libraries/` each have their own `CMakeLists.txt` added via
  `add_subdirectory` with `EXCLUDE_FROM_ALL`.
- Source files are listed explicitly — do not use `file(GLOB)`.

### C++ Standard

Target C++17: `target_compile_features(Game PRIVATE cxx_std_17)`.

---

## Testing

### Framework

Tests use Google Test (`libraries/googletest`). Test files live in `tests/`
and use the naming convention `test_*.cc` or `*_test.cc`.

### Writing Tests

```cpp
TEST(TestSuite, TestName) {
  FixedArray<int> array(3, SystemAllocator::Instance());
  EXPECT_EQ(array.size(), 0);
  array.Push(1);
  EXPECT_EQ(array.size(), 1);
}
```

Tests may use `SystemAllocator::Instance()` freely — memory discipline is
for the engine runtime, not test scaffolding.

Tests are compiled with AddressSanitizer (`-fsanitize=address`) to catch
memory errors in custom allocators.

### What to Test

- Custom containers and data structures.
- Allocator correctness (especially edge cases: full arena, double-free
  detection, alignment).
- Serialization/deserialization.
- Math utilities.

---

## Compiler Configuration

### Required Flags

```
-Wall -Wextra -Werror -Wno-unused-parameter
-fno-exceptions -fno-rtti
```

All warnings are errors. Fix warnings, don't suppress them (except
`-Wno-unused-parameter` which is allowed because callback signatures often
have unused params). For unused parameters, comment out the name
(`int /*start*/`) rather than casting to `(void)`.

### Debug / Development Builds

```
-O1 -ggdb -DNDEBUG -DGAME_WITH_ASSERTS -fsanitize=address
```

The project uses `-O1` even in development for reasonable performance while
keeping debug info. `GAME_WITH_ASSERTS` enables `DCHECK` macros.

### Release Builds

```
-O2 -DNDEBUG
```

No sanitizers, no debug checks. `DCHECK` compiles away.

---

## Summary of Differences from Google Style

| Topic                     | Google Style               | This Project                     |
|---------------------------|----------------------------|----------------------------------|
| Memory allocation         | STL containers, smart ptrs | Custom allocators, raw pointers  |
| Containers                | `std::vector`, etc.        | `DynArray`, `FixedArray`, etc.   |
| Ownership                 | `unique_ptr`, `shared_ptr` | Allocator-based lifetime         |
| Exceptions                | Allowed (conditionally)    | Banned                           |
| RTTI                      | Allowed (conditionally)    | Banned                           |
| Smart pointers            | Encouraged                 | Banned                           |
| String handling           | `std::string`              | `string_view` + interning        |
| Error handling            | Status/StatusOr            | `CHECK`/`DCHECK`/return values   |
| Source file extension     | `.cc` (same)               | `.cc`                            |
| Namespace depth           | Multi-level                | Flat (`G` + `internal_*`)        |
| Build system              | Bazel                      | CMake                            |
| Dependencies              | Managed                    | Vendored, single-header pref.    |
