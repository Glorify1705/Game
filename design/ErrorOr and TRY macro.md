---
status: implemented
tags: [error-handling, core]
---

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

### Phase 1: Core types ✅

1. ~~Add `Error` class to a new `src/error.h`~~ — done, with file/line via `__builtin_FILE()`/`__builtin_LINE()`
2. ~~Add `ErrorOr<T>` template to `src/error.h`~~ — done, with `void` specialization
3. ~~Add `TRY` and `MUST` macros to `src/error.h`~~ — done
4. ~~Unit tests for all of the above~~ — done, 19 tests

### Phase 2: Allocator integration

5. Add `Allocator::TryNew`, `Allocator::TryNewArray` methods
6. Add `DynArray::TryPush`, `DynArray::TryReserve`, `DynArray::TryEmplace`
7. Existing `Push`/`Reserve`/`Emplace` unchanged — they continue to crash on OOM via `DCHECK`

### Phase 3: Incremental adoption

Convert subsystems one at a time — no big-bang rewrite. Each task below is self-contained: convert the API, update all callers, update tests. Lua boundary functions (`lua_*.cc`) that call these APIs should use `TRY` internally and convert errors to Lua error strings at the boundary.

#### Task 3.1: Filesystem (`src/filesystem.h`, `src/filesystem.cc`, `src/lua_filesystem.cc`)

The filesystem API is the most natural starting point — it already uses the `bool + StringBuffer* err` pattern everywhere. The `StringBuffer* err` parameter goes away entirely; error context moves into `Error::Message()`.

Current signatures:
```cpp
bool WriteToFile(std::string_view filename, std::string_view contents, StringBuffer* err);
bool ReadFile(std::string_view filename, uint8_t* buffer, size_t size, StringBuffer* err);
bool Size(std::string_view filename, size_t* result, StringBuffer* err);
bool Stat(std::string_view filename, StatInfo* info, StringBuffer* err);
```

Proposed signatures:
```cpp
ErrorOr<void>     WriteToFile(std::string_view filename, std::string_view contents);
ErrorOr<void>     ReadFile(std::string_view filename, uint8_t* buffer, size_t size);
ErrorOr<size_t>   Size(std::string_view filename);
ErrorOr<StatInfo> Stat(std::string_view filename);
```

`Exists()` stays `bool` — it's a query, not a fallible operation.

The internal `SetError()` helper (which appends a user message + PhysFS error code to a `StringBuffer`) gets replaced with `return Error::Message(...)`. PhysFS-specific error context should be logged before returning the error, since `Error::Message` only takes string literals. Alternatively, we could add a PhysFS-specific error factory that captures `PHYSFS_getLastErrorCode()` as an errno.

Callers in `lua_filesystem.cc` currently do:
```cpp
if (!filesystem->Size(filename, &size, &err)) {
    lua_pushnil(state);
    lua_pushlstring(state, err.str(), err.size());
    return 2;
}
```
After conversion:
```cpp
auto size = TRY_LUA(filesystem->Size(filename));
```
Where `TRY_LUA` is a Lua-boundary helper macro that converts `Error` to a Lua error return. Define this pattern once and reuse across all `lua_*.cc` files.

#### Task 3.2: Shaders (`src/shaders.h`, `src/shaders.cc`, `src/lua_renderer.cc`)

The shader system has its own `Shaders::Error` struct and a `FillError()` helper that stores error info into `last_error_`. This ad-hoc pattern gets replaced by returning `Error` directly. The existing `Shaders::Error` struct can be removed.

Current signatures:
```cpp
bool Compile(DbAssets::ShaderType type, std::string_view name,
             std::string_view glsl, UseCache use_cache);
bool Link(std::string_view name, std::string_view vertex_shader,
          std::string_view fragment_shader, UseCache use_cache);
bool Load(const DbAssets::Shader& shader, Error* error);

// Templated and non-templated uniform setters (3 overloads):
template <typename T> bool SetUniform(const char* name, const T& value);
bool SetUniform(const char* name, int value);
bool SetUniformF(const char* name, float value);
```

Proposed signatures:
```cpp
ErrorOr<void> Compile(DbAssets::ShaderType type, std::string_view name,
                      std::string_view glsl, UseCache use_cache);
ErrorOr<void> Link(std::string_view name, std::string_view vertex_shader,
                   std::string_view fragment_shader, UseCache use_cache);
ErrorOr<void> Load(const DbAssets::Shader& shader);

template <typename T> ErrorOr<void> SetUniform(const char* name, const T& value);
ErrorOr<void> SetUniform(const char* name, int value);
ErrorOr<void> SetUniformF(const char* name, float value);
```

The `FillError()` helper and `last_error_` member get removed. Each error site returns `Error::Message(...)` directly. `LastError()` accessor also goes away — callers get the error from the `ErrorOr` return.

Note: GLSL compilation errors produce dynamic strings (the driver's error log). These can't go in `Error::Message()` (string literals only). Log the GLSL error before returning: `LOG("Shader compile error: ", info_log); return Error::Message("shader compilation failed");`

#### Task 3.3: Sound (`src/sound.h`, `src/sound.cc`, `src/lua_sound.cc`)

The sound system returns `bool` with errors logged via `LOG()`. Multiple distinct failure modes: unknown sound name, max streams exceeded, initialization failure.

Current signatures:
```cpp
bool AddSource(std::string_view name, Source* source,
               Ownership ownership = Ownership::kManaged);
bool SetSourceGain(Source source, float gain);
bool StartChannel(Source source);
bool Stop(Source source);

// Internal, in WavSampler and VorbisSampler:
bool Init(const DbAssets::Sound* sound);
```

Proposed signatures:
```cpp
ErrorOr<Source>  AddSource(std::string_view name,
                           Ownership ownership = Ownership::kManaged);
ErrorOr<void>    SetSourceGain(Source source, float gain);
ErrorOr<void>    StartChannel(Source source);
ErrorOr<void>    Stop(Source source);

// Internal:
ErrorOr<void> WavSampler::Init(const DbAssets::Sound* sound);
ErrorOr<void> VorbisSampler::Init(const DbAssets::Sound* sound);
```

`AddSource` currently takes a `Source* source` out-param. With `ErrorOr<Source>`, the source handle is the return value on success. This is a cleaner API — the caller writes `auto source = TRY(sound.AddSource("bang"))` instead of juggling a bool and an out-param.

For VorbisSampler, the dynamic error string from stb_vorbis (e.g. "not enough memory") should be logged before returning `Error::Message("vorbis decode failed")`.

#### Task 3.4: Image encoding/decoding (`src/image.h`, `src/image.cc`)

The image API has mixed error patterns: `bool* error` out-param, nullptr returns, and `bool + StringBuffer*`.

Current signatures:
```cpp
void* QoiEncode(const void* data, const QoiDesc* desc, int* out_len, Allocator* allocator);
void  QoiEncode(const void* data, const QoiDesc* desc, int* out_len, void* buffer, bool* error);
void* QoiDecode(const void* data, int size, QoiDesc* desc, int channels, Allocator* allocator);
bool  WritePixelsToImage(const char* filename, uint8_t* data, size_t width, size_t height,
                         Filesystem* filesystem, StringBuffer* err, Allocator* allocator);
```

Proposed signatures:
```cpp
struct EncodedImage { void* data; int size; };

ErrorOr<EncodedImage> QoiEncode(const void* data, const QoiDesc* desc, Allocator* allocator);
ErrorOr<void>         QoiEncode(const void* data, const QoiDesc* desc, int* out_len, void* buffer);
ErrorOr<void*>        QoiDecode(const void* data, int size, QoiDesc* desc, int channels,
                                Allocator* allocator);
ErrorOr<void>         WritePixelsToImage(const char* filename, uint8_t* data, size_t width,
                                         size_t height, Filesystem* filesystem, Allocator* allocator);
```

`WritePixelsToImage` loses its `StringBuffer* err` parameter. The QoiEncode overload that writes to a caller-provided buffer replaces `bool* error` with an `ErrorOr<void>` return. The allocating QoiEncode bundles its two out-params (`void*` + `int out_len`) into a small struct return.

Both encode and decode functions have extensive validation checks (null data, invalid dimensions, channel count). Each validation failure becomes `return Error::Message("...")`.

#### Task 3.5: Platform I/O (`src/platform.h`, `src/platform.cc`)

Platform filesystem operations that can fail. Query functions (`FileExists`, `DirectoryExists`) stay as `bool`.

Current signatures:
```cpp
bool MakeDir(const char* path);
bool MakeDirs(const char* path);
bool WriteFile(const char* path, const char* contents);
bool CopyFile(const char* src, const char* dst);
bool MakeExecutable(const char* path);
bool GetExePath(char* out, size_t out_size);
bool GetExeDir(char* out, size_t out_size);
bool GetCwd(char* out, size_t out_size);
```

Proposed signatures:
```cpp
ErrorOr<void> MakeDir(const char* path);
ErrorOr<void> MakeDirs(const char* path);
ErrorOr<void> WriteFile(const char* path, const char* contents);
ErrorOr<void> CopyFile(const char* src, const char* dst);
ErrorOr<void> MakeExecutable(const char* path);
ErrorOr<void> GetExePath(char* out, size_t out_size);
ErrorOr<void> GetExeDir(char* out, size_t out_size);
ErrorOr<void> GetCwd(char* out, size_t out_size);
```

These are thin wrappers around POSIX calls. Failures return `Error::Errno(errno)`, which naturally captures the OS error code. `WriteFileF` (the template that uses `fprintf`) also becomes `ErrorOr<void>`.

`FileExists`, `DirectoryExists`, `AbsolutePath` stay unchanged — they're queries, not operations.

#### Task 3.6: Config loading (`src/config.h`, `src/config.cc`)

Current signature:
```cpp
bool LoadConfigFromFile(const char* path, GameConfig* config, Allocator* allocator);
```

Proposed signature:
```cpp
ErrorOr<void> LoadConfigFromFile(const char* path, GameConfig* config, Allocator* allocator);
```

Straightforward conversion. Returns `Error::Message("config file not found")` instead of `false`. `LoadConfig` and `LoadConfigFromDatabase` stay `void` since they operate on data already in memory / already opened DB.

#### Task 3.7: Renderer asset lookups (`src/renderer.h`, `src/renderer.cc`, `src/lua_renderer.cc`)

Current signatures:
```cpp
bool DrawSprite(std::string_view sprite_name, FVec2 position, float angle);
bool DrawSprite(const DbAssets::Sprite& asset, FVec2 position, float angle);
bool DrawImage(std::string_view imagename, FVec2 position, float angle);
bool DrawImage(const DbAssets::Image& asset, FVec2 position, float angle);

// Private:
bool LoadSDFFromCache(sqlite3* db, std::string_view font_name,
                      uint64_t font_hash, FontInfo* font);
```

Proposed signatures:
```cpp
ErrorOr<void> DrawSprite(std::string_view sprite_name, FVec2 position, float angle);
ErrorOr<void> DrawSprite(const DbAssets::Sprite& asset, FVec2 position, float angle);
ErrorOr<void> DrawImage(std::string_view imagename, FVec2 position, float angle);
ErrorOr<void> DrawImage(const DbAssets::Image& asset, FVec2 position, float angle);

// Private:
ErrorOr<FontInfo> LoadSDFFromCache(sqlite3* db, std::string_view font_name,
                                   uint64_t font_hash);
```

`DrawSprite`/`DrawImage` by name can fail if the asset isn't loaded. The `Error` carries context about which asset was missing. `LoadSDFFromCache` replaces its `FontInfo*` out-param with a return value.

#### Task 3.8: Asset packing (`src/packer.h`, `src/packer.cc`)

Current signatures:
```cpp
DbAssets* ReadAssetsFromDb(sqlite3* db, Allocator* allocator);
AssetWriteResult WriteAssetsToDb(const char* source_directory, sqlite3* db, Allocator* allocator);
```

Proposed signatures:
```cpp
ErrorOr<DbAssets*>         ReadAssetsFromDb(sqlite3* db, Allocator* allocator);
ErrorOr<AssetWriteResult>  WriteAssetsToDb(const char* source_directory, sqlite3* db,
                                           Allocator* allocator);
```

`ReadAssetsFromDb` currently returns `nullptr` on failure (with `DIE` in some paths). Convert the `DIE` calls that represent runtime errors (bad DB, corrupt data) to `Error::Message(...)` returns. Keep `DIE` for true invariant violations (programming bugs).

#### Task 3.9: Color lookup (`src/color.h`, `src/color.cc`)

Current signature:
```cpp
bool ColorFromTable(std::string_view color, Color* result);
```

Proposed signature:
```cpp
ErrorOr<Color> ColorFromTable(std::string_view color);
```

Simple bool + out-param to `ErrorOr<T>` conversion. Returns `Error::Message("unknown color name")` for unrecognized colors.

### What stays the same

- `CHECK` / `DCHECK` / `DIE` for invariant violations (logic bugs, not runtime errors)
- `NOTNULL` for null pointer checks that indicate programming errors
- Functions where failure is a programming error (not a runtime condition) should keep crashing
- `DEFER` for cleanup — not replaced by smart pointers

The distinction: **`ErrorOr<T>` is for operations that can legitimately fail at runtime** (allocation, file I/O, parsing). **`CHECK` is for things that should never happen if the code is correct** (out-of-bounds access, null dereference of a required parameter).

## Resolved questions

- **Error context**: `Error` carries file/line via `__builtin_FILE()`/`__builtin_LINE()` default arguments. Adds `const char*` + `uint32_t` (12 bytes). Could be packed tighter later but not worth the complexity now.

	%% Adding file line seems fine, its a const string and a small number, we could probably pack them as an uin32_t offset in the binary and uin32_t line, right? %%

- **Error type customization**: `ErrorOr<T, E = Error>` keeps the template parameter for flexibility, but `Error` is the default and should be sufficient for all current uses.

	%% ErrorOr sounds good. %%

- **Naming**: `TRY` and `MUST` — short, clear, matches SerenityOS and Zig conventions.

%% TRY and MUST sound good. %%

- **Interaction with Lua boundary**: Lua boundary functions (`lua_*.cc`) use `TRY` internally and convert errors to Lua error strings at the boundary. Define a `TRY_LUA` macro or helper pattern once and reuse across all `lua_*.cc` files.

## References

- SerenityOS AK library: `worktrees/serenity/AK/Error.h`, `AK/Try.h`
- Zig error handling: https://ziglang.org/documentation/master/#Errors
- Our existing error handling: `src/logging.h`
- Our allocator design: `src/allocators.h`, `design/Memory allocators for third-party libraries.md`
- Cross-platform portability: `design/WebAssembly and cross-platform portability.md`
