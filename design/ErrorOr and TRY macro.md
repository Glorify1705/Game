# ErrorOr\<T\> and TRY Macro — Zig-Style Error Handling in C++

## Motivation

The engine aims to be "zig-like in C++": explicit memory management, no STL, no exceptions, no RTTI. Zig's `try` keyword is one of its best features — it propagates errors up the call stack without exceptions and without boilerplate. SerenityOS has implemented this pattern in C++ using an `ErrorOr<T>` result type and a `TRY()` macro. This document evaluates adopting a similar pattern for our engine.

### Current error handling

The engine has three error handling strategies, none of which propagate errors cleanly:

| Strategy | Mechanism | Used where | Problem |
|---|---|---|---|
| **Crash** | `CHECK`, `DIE`, `NOTNULL` | Invariants, null checks | No recovery possible |
| **Debug-only crash** | `DCHECK` | Bounds checks, capacity overflow | Compiles out in release |
| **Bool + out-param** | `bool Load(const char* path, Texture* out)` | Asset loading, initialization | Awkward API, easy to forget to check |

Missing: a way to return either a value or an error, and propagate errors up the call stack without boilerplate. Consider a typical initialization chain:

```cpp
// Current pattern — verbose, error path is ad-hoc
bool LoadLevel(Allocator* a, const char* path, Level* out) {
  Texture* tex;
  if (!LoadTexture(a, "tileset.png", &tex)) {
    LOG("Failed to load tileset");
    return false;
  }
  TileMap* map;
  if (!LoadTileMap(a, path, &map)) {
    LOG("Failed to load tilemap");
    return false;
  }
  out->texture = tex;
  out->map = map;
  return true;
}

// With TRY — direct, compositional
ErrorOr<Level> LoadLevel(Allocator* a, const char* path) {
  auto* tex = TRY(LoadTexture(a, "tileset.png"));
  auto* map = TRY(LoadTileMap(a, path));
  return Level{.texture = tex, .map = map};
}
```

## SerenityOS implementation

SerenityOS's AK library (`AK/Error.h`, `AK/Try.h`) provides three components:

### Error type

A lightweight error value. No heap allocation, no `std::string`:

```cpp
class [[nodiscard]] Error {
public:
  static Error from_errno(int code);
  static Error from_string_literal(char const (&str)[N]);
  static Error from_string_view(StringView sv);

  int code() const;
  bool is_errno() const;
  StringView string_literal() const;

private:
  StringView m_string_literal;
  int m_code = 0;
};
```

### ErrorOr\<T\> result type

A union-based tagged result. Stores either `T` or `Error`, never both:

```cpp
template<typename T, typename E = Error>
class [[nodiscard]] ErrorOr {
  union { T m_value; E m_error; };
  bool m_is_error;

public:
  ErrorOr(T&& value);      // implicit from value
  ErrorOr(E&& error);      // implicit from error
  bool is_error() const;
  T&& release_value();
  E&& release_error();
};

// Specialization for void
template<typename E>
class [[nodiscard]] ErrorOr<void, E> : public ErrorOr<Empty, E> {};
```

Key properties:
- **Move-only** — no copies, ownership is clear
- **`[[nodiscard]]`** — compiler warns if you ignore the result
- **Union storage** — zero overhead vs. a plain `T` return (no `Optional` wrapper)
- **Trivially destructible optimization** — compiler can elide destructor calls for POD types

### TRY and MUST macros

```cpp
#define TRY(expression)                                \
    ({                                                 \
        auto&& _temporary_result = (expression);       \
        if (_temporary_result.is_error()) [[unlikely]]  \
            return _temporary_result.release_error();   \
        _temporary_result.release_value();              \
    })

#define MUST(expression)                               \
    ({                                                 \
        auto&& _temporary_result = (expression);       \
        VERIFY(!_temporary_result.is_error());          \
        _temporary_result.release_value();              \
    })
```

`TRY` evaluates an `ErrorOr<T>` expression. If it's an error, return the error from the enclosing function. If it's a value, evaluate to that value. This is the C++ equivalent of Zig's `try` keyword.

`MUST` is the "this cannot fail" variant — crashes if the expression is an error. Equivalent to Zig's `catch unreachable`.

### The try\_ convention on containers

SerenityOS containers have two variants of every allocating method:

```cpp
// Fallible — returns ErrorOr<void>
ErrorOr<void> try_append(T&& value) {
    TRY(try_grow_capacity(size() + 1));
    new (slot(m_size)) StorageType(move(value));
    ++m_size;
    return {};
}

// Panicking — crashes on failure
void append(T&& value) { MUST(try_append(move(value))); }
```

Callers choose: crash on OOM (`append`) or propagate the error (`TRY(v.try_append(x))`).

## Proposed design for our engine

### Error type

Adapted to our naming conventions and string handling:

```cpp
class [[nodiscard]] Error {
 public:
  constexpr static Error Errno(int code) {
    DCHECK(code != 0);
    return Error(code);
  }

  template <size_t N>
  constexpr static Error Message(const char (&literal)[N]) {
    return Error(std::string_view(literal, N - 1));
  }

  constexpr int code() const { return code_; }
  constexpr bool is_errno() const { return code_ != 0; }
  constexpr std::string_view message() const { return message_; }

 private:
  constexpr Error(int code) : code_(code) {}
  constexpr Error(std::string_view msg) : message_(msg) {}

  std::string_view message_;
  int code_ = 0;
};
```

Design choices:
- **`std::string_view` for messages** — we already use `string_view` everywhere, no need for a custom `StringView`
- **Static factory methods** — `Error::Errno(ENOMEM)`, `Error::Message("bad format")`. PascalCase per our conventions.
- **String literals only** — `Message()` takes `const char (&)[N]`, enforcing compile-time string lifetime. No heap allocation for error messages. For dynamic messages, log before returning the error.
- **No syscall variant** — we don't wrap syscalls directly (SDL does that for us)

### ErrorOr\<T\> type

```cpp
template <typename T, typename E = Error>
class [[nodiscard]] ErrorOr {
 public:
  using ValueType = T;
  using ErrorType = E;

  // Construct from value (implicit)
  template <typename U>
  constexpr ErrorOr(U&& value)
      requires(requires { T(std::declval<U>()); } && !std::is_same_v<std::remove_cvref_t<U>, E>)
      : value_(std::forward<U>(value)), is_error_(false) {}

  // Construct from error (implicit)
  constexpr ErrorOr(E&& error) : error_(std::move(error)), is_error_(true) {}
  constexpr ErrorOr(const E& error) : error_(error), is_error_(true) {}

  // Move only
  constexpr ErrorOr(ErrorOr&& other);
  ErrorOr(const ErrorOr&) = delete;
  ErrorOr& operator=(const ErrorOr&) = delete;

  constexpr bool is_error() const { return is_error_; }

  constexpr T& value() {
    DCHECK(!is_error_);
    return value_;
  }

  constexpr E& error() {
    DCHECK(is_error_);
    return error_;
  }

  constexpr T&& release_value() { return std::move(value()); }
  constexpr E&& release_error() { return std::move(error()); }

  constexpr ~ErrorOr() {
    if (is_error_)
      error_.~E();
    else
      value_.~T();
  }

 private:
  union {
    T value_;
    E error_;
  };
  bool is_error_;
};

// Specialization for void — functions that succeed or fail with no return value
template <typename E>
class [[nodiscard]] ErrorOr<void, E> {
 public:
  constexpr ErrorOr() : is_error_(false) {}
  constexpr ErrorOr(E&& error) : error_(std::move(error)), is_error_(true) {}
  constexpr ErrorOr(const E& error) : error_(error), is_error_(true) {}

  constexpr bool is_error() const { return is_error_; }
  constexpr E& error() { DCHECK(is_error_); return error_; }
  constexpr E&& release_error() { return std::move(error()); }
  constexpr void release_value() {}

  constexpr ~ErrorOr() {
    if (is_error_) error_.~E();
  }

 private:
  union { E error_; };
  bool is_error_;
};
```

### TRY and MUST macros

```cpp
#define TRY(expression)                                              \
    ({                                                               \
        auto&& _temporary_result = (expression);                     \
        if (_temporary_result.is_error()) [[unlikely]]               \
            return _temporary_result.release_error();                \
        _temporary_result.release_value();                           \
    })

#define MUST(expression)                                             \
    ({                                                               \
        auto&& _temporary_result = (expression);                     \
        CHECK(!_temporary_result.is_error(),                         \
              _temporary_result.error().message());                  \
        _temporary_result.release_value();                           \
    })
```

`MUST` uses our existing `CHECK` macro (which crashes with file/line info), not SerenityOS's `VERIFY`.

### Container try\_ variants

Add fallible methods to `DynArray`:

```cpp
template <typename T>
class DynArray {
 public:
  // Existing — crashes on OOM (ArenaAllocator returns nullptr)
  void Push(T&& t);

  // New — returns error on OOM
  ErrorOr<void> TryPush(T&& t) {
    TRY(TryResize());
    ::new (&buffer_[elems_]) T(std::move(t));
    elems_++;
    return {};
  }

  ErrorOr<void> TryReserve(size_t size) {
    // ... like Reserve() but returns ErrorOr<void> on alloc failure
  }

 private:
  ErrorOr<void> TryResize() {
    if (buffer_ == nullptr) {
      capacity_ = 16;
      buffer_ = allocator_->NewArray<T>(capacity_);
      if (!buffer_) return Error::Errno(ENOMEM);
    } else if (elems_ == capacity_) {
      const size_t new_capacity = capacity_ + (capacity_ >> 1);
      auto* new_buf = static_cast<T*>(
          allocator_->Realloc(buffer_, capacity_ * sizeof(T),
                              new_capacity * sizeof(T), alignof(T)));
      if (!new_buf) return Error::Errno(ENOMEM);
      buffer_ = new_buf;
      capacity_ = new_capacity;
    }
    return {};
  }
};
```

Note: `TryPush` (PascalCase), not `try_push` (snake_case), matching our naming conventions.

### Allocator::TryNew

Add a fallible factory to `Allocator`:

```cpp
class Allocator {
 public:
  // Existing — crashes if alloc returns null
  template <typename T, typename... Args>
  T* New(Args... args);

  // New — returns ErrorOr
  template <typename T, typename... Args>
  ErrorOr<T*> TryNew(Args... args) {
    T* ptr = reinterpret_cast<T*>(Alloc(sizeof(T), alignof(T)));
    if (!ptr) return Error::Errno(ENOMEM);
    ::new (ptr) T(std::forward<Args>(args)...);
    return ptr;
  }
};
```

## Platform portability: statement expressions

The `TRY` macro relies on **GCC statement expressions** — the `({...})` syntax that allows a block of statements to produce a value. This is a non-standard C++ extension.

### Compiler support

| Compiler | Statement expressions | Status |
|---|---|---|
| **GCC** | Yes | Supported since GCC 3.x. Production quality. |
| **Clang** | Yes | Full support, matches GCC behavior. |
| **MSVC** | **No** | Not supported. No plans to add it. |
| **Emscripten** | Yes | Uses Clang backend. Works identically to native Clang. |
| **Intel ICX** | Yes | Clang-based since oneAPI. |

### Impact on target platforms

| Platform | Compiler | TRY works? | Notes |
|---|---|---|---|
| **Linux** (primary) | GCC or Clang | Yes | Current target. No issue. |
| **WASM** | Emscripten (Clang) | Yes | Emscripten is Clang. Statement expressions work. SerenityOS itself has a Ladybird browser port that uses Emscripten + TRY. |
| **macOS** | Apple Clang | Yes | Apple Clang supports statement expressions. |
| **Windows (Clang)** | clang-cl or MinGW Clang | Yes | If we compile with Clang on Windows (common for game engines), no issue. |
| **Windows (MSVC)** | MSVC cl.exe | **No** | MSVC does not support statement expressions. This is the only blocker. |
| **Android** | NDK Clang | Yes | Android NDK uses Clang exclusively. |
| **iOS** | Apple Clang | Yes | Same as macOS. |
| **Consoles** | Clang-based (PS5, Switch) | Yes | Modern console toolchains are Clang-based. |

### The MSVC question

MSVC is the **only** compiler that doesn't support statement expressions. Three options:

#### Option A: Don't support MSVC (recommended)

Use Clang everywhere, including Windows. This is increasingly common in game development:
- Unreal Engine supports Clang on Windows
- Many indie engines (high_impact, Sokol) target Clang/GCC only
- `clang-cl` is a drop-in replacement for MSVC's `cl.exe` that accepts MSVC flags but uses Clang's frontend
- Our engine already depends on GCC/Clang extensions (`__attribute__((malloc))`, ASAN builtins)
- MinGW-w64 with GCC or Clang is another option for Windows

**Trade-off**: Cannot build with Visual Studio's native compiler. Can still use Visual Studio as an IDE with clang-cl.

#### Option B: Provide an MSVC fallback macro

A less ergonomic but portable `TRY` alternative for MSVC:

```cpp
#ifdef _MSC_VER
// MSVC: TRY assigns to a named variable. Less ergonomic but portable.
#define TRY_OR_RETURN(var, expression)                     \
    auto&& var##_result_ = (expression);                   \
    if (var##_result_.is_error()) [[unlikely]]              \
        return var##_result_.release_error();               \
    auto var = var##_result_.release_value()

// Usage:
ErrorOr<Level> LoadLevel(Allocator* a, const char* path) {
    TRY_OR_RETURN(tex, LoadTexture(a, "tileset.png"));
    TRY_OR_RETURN(map, LoadTileMap(a, path));
    return Level{.texture = tex, .map = map};
}
#else
#define TRY(expression) /* statement expression version */
#endif
```

**Trade-off**: Two macro APIs. The MSVC version requires a variable name, can't be used inline in expressions (`auto x = TRY(foo()) + TRY(bar())` doesn't work).

#### Option C: Use C++23 `std::expected` as the base

C++23 introduces `std::expected<T, E>`, which is the standardized version of `ErrorOr<T>`. It has `and_then()` and `transform()` for monadic chaining. However:
- We target C++17. C++23 is not available on all our target compilers yet.
- Monadic chaining is more verbose than `TRY()`.
- We'd still want the `TRY` macro for ergonomics, which has the same MSVC problem.
- `std::expected` is an STL type, which conflicts with our no-STL goal.

#### Recommendation

**Option A** — require Clang (or GCC) everywhere. The engine already uses non-portable extensions. Adding one more is consistent. The Windows build should use `clang-cl` or MinGW Clang. This matches the approach of SerenityOS (which builds Ladybird on Windows with Clang) and high_impact (which doesn't support MSVC).

### WASM-specific notes

Statement expressions work perfectly under Emscripten because Emscripten **is** Clang. SerenityOS's Ladybird browser (which uses `TRY` extensively) compiles to WASM via Emscripten. There are no code size implications — the macro expands to a branch and a return, same as hand-written error checking.

The `[[unlikely]]` hint is respected by Emscripten's optimizer and helps WASM code size: the error path is placed out-of-line, keeping the hot path compact.

## What NOT to adopt from SerenityOS

Some SerenityOS patterns don't fit our engine:

| Pattern | Why not |
|---|---|
| **Smart pointers** (`OwnPtr`, `NonnullOwnPtr`, `RefPtr`) | We use `Allocator::Destroy` + `DEFER` instead. Simpler, no hidden control flow, fits our allocator-first design. Smart pointers hide the allocator. |
| **Reference counting** (`RefCounted`) | Game engines avoid refcounting in hot paths (cache misses, atomic ops). We use explicit ownership with arenas and free lists. |
| **Custom `StringView`** | We already use `std::string_view`, which is allowed by our STL policy. No need to reimplement. |
| **Custom `Optional<T>`** | Could be useful but low priority — `std::optional` is header-only and lightweight. Evaluate later. |
| **`NoAllocationGuard`** | Nice debugging tool but low priority. Can add later without design changes. |
| **`Checked<T>` overflow arithmetic** | Useful for buffer math but not urgent. Our allocators don't do complex size calculations. Evaluate later. |
| **`CO_TRY` coroutine support** | We don't use C++20 coroutines. |

## Adoption strategy

### Phase 1: Core types

1. Add `Error` class to a new `src/error.h`
2. Add `ErrorOr<T>` template to `src/error.h`
3. Add `TRY` and `MUST` macros to `src/error.h` (or `src/logging.h` alongside `CHECK`)
4. Unit tests for all of the above

### Phase 2: Allocator integration

5. Add `Allocator::TryNew`, `Allocator::TryNewArray` methods
6. Add `DynArray::TryPush`, `DynArray::TryReserve`, `DynArray::TryEmplace`
7. Existing `Push`/`Reserve`/`Emplace` unchanged — they continue to crash on OOM via `DCHECK`

### Phase 3: Incremental adoption

8. Convert one subsystem (e.g., asset loading) to use `ErrorOr<T>` returns
9. Evaluate ergonomics and adjust the design
10. Convert more subsystems as appropriate — no big-bang rewrite

### What stays the same

- `CHECK` / `DCHECK` / `DIE` for invariant violations (logic bugs, not runtime errors)
- `NOTNULL` for null pointer checks that indicate programming errors
- Functions where failure is a programming error (not a runtime condition) should keep crashing
- `DEFER` for cleanup — not replaced by smart pointers

The distinction: **`ErrorOr<T>` is for operations that can legitimately fail at runtime** (allocation, file I/O, parsing). **`CHECK` is for things that should never happen if the code is correct** (out-of-bounds access, null dereference of a required parameter).

## Open questions

- **Error context**: Should `Error` carry more than a string literal? SerenityOS's `Error` is minimal (errno or string). For debugging, we could add file/line info to `Error`, but this increases its size. Alternative: log context before returning the error, keep `Error` small.

- **Error type customization**: `ErrorOr<T, E>` is templated on the error type. Should we ever use a custom error type (e.g., `ErrorOr<Texture*, ParseError>` with structured error info)? Or is `Error` always sufficient?

- **Naming**: `TRY` vs `TRY_UNWRAP` vs `UNWRAP_OR_RETURN`? `MUST` vs `UNWRAP_OR_DIE`? SerenityOS's names are clean and short. Zig uses `try` and `catch unreachable`. The shorter names are better for readability in practice.

- **Interaction with Lua boundary**: Functions called from Lua (`lua_*.cc`) currently push error strings to the Lua stack and return error codes. `ErrorOr<T>` doesn't change this — the Lua boundary functions would `TRY` internally and convert errors to Lua errors at the boundary. But the pattern should be documented.

## References

- SerenityOS AK library: `worktrees/serenity/AK/Error.h`, `AK/Try.h`
- Zig error handling: https://ziglang.org/documentation/master/#Errors
- Our existing error handling: `src/logging.h`
- Our allocator design: `src/allocators.h`, `design/Memory allocators for third-party libraries.md`
- Cross-platform portability: `design/WebAssembly and cross-platform portability.md`
