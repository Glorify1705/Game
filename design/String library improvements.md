---
status: implemented
tags: [core, strings]
---

# String Library Improvements

## Problem

The engine's string infrastructure (`StringBuffer`, `FixedStringBuffer`, `Alphanumeric`, `StringTable`) is solid for what it does, but the utility function set is minimal. The full list of string operations:

| Function | Purpose |
|----------|---------|
| `HasPrefix(str, prefix)` | Check prefix |
| `ConsumePrefix(str*, prefix)` | Remove prefix |
| `HasSuffix(str, suffix)` | Check suffix |
| `ConsumeSuffix(str*, suffix)` | Remove suffix |
| `Basename(path)` | Extract filename |
| `WithoutExt(path)` | Remove extension |
| `Extension(path)` | Get extension |
| `PrintDouble(val, buf, size)` | Float formatting |

Missing operations that show up repeatedly as inline code across the engine:

- **Split** — splitting strings by delimiter (done ad-hoc with `find`/`substr` loops)
- **Join** — joining arrays of strings with a separator
- **Trim** — stripping whitespace (or arbitrary chars) from ends
- **Case conversion** — `ToLower`/`ToUpper` (ASCII-only is fine for the engine)
- **Find/Contains** — searching within a string
- **Replace** — substituting substrings
- **Number parsing** — `StringToInt`, `StringToFloat` with error reporting
- **Growable string** — when `FixedStringBuffer` isn't enough (e.g., building SQL, building shader source)

The question: should we vendor an external string library or extend what we have?

## Survey of external libraries

### Abseil (`absl::strings`)

**What it offers:**
- `absl::StrCat` — type-safe concatenation with `AlphaNum` stack buffers (22-48 bytes per argument). Conceptually identical to the engine's `StringBuffer::Append` + `Alphanumeric`.
- `absl::StrFormat` — type-safe printf replacement with compile-time format string checking.
- `absl::StrJoin` / `absl::StrSplit` — flexible splitting and joining with custom formatters.
- `absl::Substitute` — fast string substitution with `$0`-`$9` placeholders.
- `absl::Cord` — rope data structure for large mutable strings.

**Compatibility:**
- Works with `-fno-exceptions` (Google doesn't use exceptions internally).
- Likely works with `-fno-rtti` (no `dynamic_cast`/`typeid` in string code), though not officially documented.
- Modular CMake build — can link just `absl::strings`.

**Dealbreaker: all output is `std::string`.** `absl::StrCat` returns `std::string`. `absl::StrJoin` returns `std::string`. `absl::StrFormat` returns `std::string`. There is no way to pass a custom allocator for the output. The engine avoids `std::string` entirely and controls all allocation through `G::Allocator*`. Abseil's allocation model is fundamentally incompatible.

If the engine replaces global `operator new` with mimalloc (which it may already do), Abseil allocations would route through mimalloc. But per-allocation arena control — the engine's core allocation philosophy — is impossible.

**Cherry-picking potential:** The *algorithms* (split logic, join logic, format parsing) could be studied and reimplemented against `StringBuffer`, but vendoring Abseil just for strings pulls in `absl::base`, `absl::throw_delegate`, `absl::raw_logging_internal`, and several other internal modules (~100-200KB binary overhead).

**Verdict: not viable as a dependency.** The `std::string` return type makes it incompatible. The ideas are good and worth borrowing.

### Facebook Folly

**What it offers:**
- `folly::fbstring` — SSO-optimized string (23-byte inline threshold on 64-bit, vs ~15 for most `std::string`). Three storage modes: small (inline, 0-23 bytes), medium (malloc, 24-254 bytes), large (refcounted, 255+).
- `folly::StringPiece` — non-owning view (redundant with `std::string_view`).
- `folly::format` / `folly::sformat` — Python-style `{}` formatting.
- `folly::to<T>()` — type conversions.

**Dealbreaker 1: requires exceptions.** `fbstring` throws `std::length_error` and `std::out_of_range`. There is no exception-free mode. Incompatible with `-fno-exceptions`.

**Dealbreaker 2: explicitly rejects custom allocators.** The source contains a `static_assert` requiring `std::allocator<E>`. Uses `malloc`/`free` directly via `checkedMalloc()`.

**Dealbreaker 3: massive dependencies.** Requires ~17 external libraries: Boost, glog, gflags, libevent, double-conversion, lz4, lzma, snappy, zlib, OpenSSL, jemalloc, and more.

**Verdict: not viable.** Fails on exceptions, allocators, and dependencies simultaneously.

### EASTL (`eastl::string`, `eastl::fixed_string`)

**What it offers:**
- `eastl::basic_string` — STL-compatible string with custom allocator support, no copy-on-write.
- `eastl::fixed_string<char, N, bEnableOverflow, OverflowAllocator>` — compile-time fixed capacity with optional overflow to a dynamic allocator. This is the closest external equivalent to the engine's `FixedStringBuffer`.
- Extended API: `sprintf()`, `append_sprintf()`, `make_lower()`, `make_upper()`, `trim()`, `compare_i()`.

**Compatibility:**
- **Designed for game engines** by EA. Purpose-built for the exact constraints this engine has.
- Works with `-fno-exceptions` (`EASTL_EXCEPTIONS_ENABLED` guard, uses abort when disabled).
- Works with `-fno-rtti` (templates, not virtual dispatch).
- **First-class custom allocator support.** Allocators are simpler than `std::allocator` — just `allocate(size_t n, int flags)` and `deallocate(void* p, size_t n)`. The engine's `G::Allocator` maps to this with a thin wrapper.

**Downsides:**
- Smaller SSO buffer (15 bytes) than Folly (23 bytes).
- Pulls in EASTL infrastructure (allocator, type traits, compressed_pair).
- Template bloat with many `fixed_string<char, N>` instantiations for different N values.
- The engine already avoids STL-style containers entirely; adopting EASTL's string means adopting EASTL's conventions alongside the engine's existing `DynArray`/`Dictionary` ecosystem.

**Verdict: best external option, but may not be worth the dependency.** EASTL is well-aligned philosophically, but the engine already has working string infrastructure. The gap is in utility functions, not in the core string type.

### Summary matrix

| Feature | Abseil | Folly | EASTL | Engine (current) |
|---------|--------|-------|-------|-----------------|
| Custom allocator output | No (`std::string`) | No (`static_assert`) | Yes (first-class) | Yes (caller buffer) |
| `-fno-exceptions` | Yes | **No** | Yes | Yes |
| `-fno-rtti` | Likely | Problematic | Yes | Yes |
| Fixed-capacity strings | No | No | `fixed_string<N>` | `FixedStringBuffer<N>` |
| Type-safe formatting | `StrFormat` | `folly::format` | `sprintf` (printf) | `AppendF` (printf) |
| Dependencies | ~5 internal modules | ~17 external libs | EASTL infra | None |
| Split/Join | Yes | Yes | No | No |
| Trim/Case | No | No | Yes | No |

## Design: extend the existing library

The external libraries are either incompatible (Folly), misaligned on allocation (Abseil), or add a large dependency for marginal gain over extending what exists (EASTL). The strongest option is to grow the engine's own string utilities, borrowing ideas from Abseil and EASTL where appropriate.

### New utility functions

All new functions operate on `std::string_view` input and either return `std::string_view` (for slicing operations that don't allocate) or write into a caller-provided `StringBuffer` (for operations that produce new strings).

#### Split

```cpp
// Callback-based split. Calls `fn` for each piece. No allocation.
template <typename F>
void SplitString(std::string_view input, char delimiter, F&& fn);

template <typename F>
void SplitString(std::string_view input, std::string_view delimiter, F&& fn);

// Array-based split. Writes views into caller-provided array. Returns count.
int SplitString(std::string_view input, char delimiter,
                std::string_view* out, int max_parts);
```

The callback form is the primary API — it requires zero allocation and handles any number of parts. The array form is a convenience for cases where the caller knows the max part count.

Abseil's `StrSplit` returns a `std::vector<std::string>` by default. The callback form avoids this entirely.

#### Join

```cpp
// Join views with separator, writing into a StringBuffer.
void JoinStrings(const std::string_view* parts, int count,
                 std::string_view separator, StringBuffer& out);

// DynArray overload.
void JoinStrings(const DynArray<std::string_view>& parts,
                 std::string_view separator, StringBuffer& out);
```

#### Trim

```cpp
// Return trimmed views (no allocation).
std::string_view TrimLeft(std::string_view s);
std::string_view TrimRight(std::string_view s);
std::string_view Trim(std::string_view s);

// Trim specific characters.
std::string_view TrimLeft(std::string_view s, std::string_view chars);
std::string_view TrimRight(std::string_view s, std::string_view chars);
std::string_view Trim(std::string_view s, std::string_view chars);
```

Trimming is pure slicing — the returned `string_view` points into the original string. No allocation needed.

#### Case conversion (ASCII only)

```cpp
// In-place conversion (writes into mutable buffer).
void ToLower(char* s, size_t len);
void ToUpper(char* s, size_t len);

// Copy into StringBuffer.
void ToLower(std::string_view s, StringBuffer& out);
void ToUpper(std::string_view s, StringBuffer& out);
```

ASCII-only. The engine does not handle Unicode text rendering, so full Unicode case folding is out of scope.

#### Find / Contains / Replace

```cpp
// Search.
bool Contains(std::string_view haystack, std::string_view needle);
bool Contains(std::string_view haystack, char needle);

// Replace first/all occurrences, writing result into StringBuffer.
void ReplaceFirst(std::string_view input, std::string_view from,
                  std::string_view to, StringBuffer& out);
void ReplaceAll(std::string_view input, std::string_view from,
                std::string_view to, StringBuffer& out);
```

#### Number parsing

```cpp
// Parse with error reporting. Returns false on failure.
bool StringToInt(std::string_view s, int* out);
bool StringToFloat(std::string_view s, float* out);
bool StringToDouble(std::string_view s, double* out);

// Parse with default value on failure.
int StringToIntOr(std::string_view s, int default_value);
float StringToFloatOr(std::string_view s, float default_value);
```

Uses `strtol`/`strtod` internally. The `Or` variants are convenient for config parsing where a default is always acceptable.

### FixedStringBuffer ergonomics

`FixedStringBuffer<N>` requires an explicit size at every callsite. This causes three problems:

1. **Developers guess a size every time.** Is this path 256 or 512? The answer is usually "whatever the last person picked." Sizes across the codebase are inconsistent — paths use 256, 512, and `kMaxPathLength` interchangeably. SQL buffers range from 256 to 872.
2. **Silent truncation when the guess is wrong.** `StringBuffer::capacity()` clamps writes to the remaining space. No crash, no warning — just a subtly wrong string. This is hard to catch in testing because it only triggers on long inputs.
3. **Visual noise.** `FixedStringBuffer<256>` is 23 characters of type name for what is conceptually "a temporary string." It obscures the intent at the callsite.

Current usage patterns from the codebase:

| Size | Count | Typical use |
|------|-------|-------------|
| `kMaxLogLineLength` (511) | ~10 | Logging, error messages |
| `kMaxPathLength` (256) | ~5 | File paths in packer |
| 256 | ~15 | SQL, paths, general |
| 512 | ~5 | Paths, SQL |
| 1024 | ~5 | Init paths, large errors |
| 32-64 | ~5 | Small formatting (vec toString) |

Three changes, each independent but complementary:

#### 1. Named aliases for common uses

Replace magic numbers with semantic names. Define in `stringlib.h` after `FixedStringBuffer`:

```cpp
// Common buffer sizes for specific purposes.
using PathBuffer = FixedStringBuffer<kMaxPathLength>;   // 256 — file paths
using LogBuffer  = FixedStringBuffer<kMaxLogLineLength>; // 511 — log lines
using SqlBuffer  = FixedStringBuffer<1024>;              // SQL statements
using SmallBuffer = FixedStringBuffer<64>;               // vec toString, small formatting
```

And a general-purpose default for code that just needs "a temporary string":

```cpp
template <size_t N = 256>
class FixedStringBuffer final : public StringBuffer { ... };

// The common case: just write Str.
using Str = FixedStringBuffer<>;
```

Before and after at callsites:

```cpp
// Before:
FixedStringBuffer<kMaxPathLength> path(directory, "/", filename);
FixedStringBuffer<256> sql("INSERT OR REPLACE INTO ", table, ...);
FixedStringBuffer<64> buf;
buf.Append(*v);

// After:
PathBuffer path(directory, "/", filename);
SqlBuffer sql("INSERT OR REPLACE INTO ", table, ...);
SmallBuffer buf;
buf.Append(*v);

// Or when you just need a string and don't care about the category:
Str buf("hello ", name);
```

The template still accepts an explicit size for the rare cases where a specific size matters. The aliases just eliminate the decision at ~90% of callsites.

**Migration:** Mechanical find-and-replace. No behavioral change. Can be done incrementally — new code uses aliases, old code works unchanged.

#### 2. Truncation detection in debug builds

Add a `DCHECK` to `StringBuffer::AppendStr` that fires when a write is truncated. This catches undersized buffers during development instead of producing silent data corruption.

The check goes in `StringBuffer` itself (not `FixedStringBuffer`), so it applies to all string building:

```cpp
void AppendStr(std::string_view s) {
    const size_t length = capacity(s.size());
    DCHECK(length == s.size(),
           "StringBuffer truncated: needed ", s.size(),
           " more bytes, had ", remaining(), " remaining");
    std::memcpy(buf_ + pos_, s.data(), length);
    pos_ += length;
}
```

Similarly for `VAppendF`:

```cpp
void VAppendF(const char* fmt, va_list l) {
    int needed = vsnprintf(&buf_[pos_], size_ - pos_, fmt, l);
    DCHECK(static_cast<size_t>(needed) <= size_ - pos_,
           "StringBuffer truncated: format needed ", needed,
           " bytes, had ", size_ - pos_, " remaining");
    pos_ = std::min(size_, pos_ + needed);
}
```

`DCHECK` compiles out in release (`!GAME_WITH_ASSERTS`), so there is zero runtime cost in shipping builds. In debug builds, it crashes immediately at the truncation point with file:line context, instead of silently producing a corrupt string that causes a confusing failure downstream.

**One complication:** some callsites intentionally truncate. For example, logging long messages into `kMaxLogLineLength` is fine — the message is just for humans. Two options for these:

- **Option A: allow-truncation flag.** Add a `bool allow_truncation_ = false` member to `StringBuffer`. Callers that expect truncation set it: `buf.AllowTruncation()`. The DCHECK skips when the flag is set.
- **Option B: separate unchecked append.** Add `AppendTruncating(...)` that explicitly opts out. Normal `Append` checks.
- **Option C: only check in new code.** Leave existing `AppendStr`/`VAppendF` as-is. Add checked variants `AppendStrChecked`/`VAppendFChecked` that new aliases (`PathBuffer`, `SqlBuffer`) route through.

Option A is simplest. The `LogBuffer` alias could set `allow_truncation_` by default since log lines are always best-effort. All other aliases check by default.

```cpp
class StringBuffer {
 public:
    // Opt into silent truncation (e.g., log lines).
    void AllowTruncation() { allow_truncation_ = true; }

 private:
    bool allow_truncation_ = false;
};
```

**Migration:** Add the DCHECK, run `game-test`, fix any buffers that are too small. The test suite with ASan will catch all the truncations in tested code paths.

#### 3. Hybrid buffer with overflow to allocator

Unify `FixedStringBuffer` and the proposed `DynString` into a single type. The buffer starts on the stack (like today), but can optionally grow through an `Allocator*` when the inline capacity is exceeded:

```cpp
template <size_t N = 256>
class FixedStringBuffer final : public StringBuffer {
 public:
    // Stack-only mode (current behavior). Truncates on overflow.
    FixedStringBuffer() : StringBuffer(inline_buf_, N) {}

    template <typename... Ts>
    explicit FixedStringBuffer(Ts... ts) : StringBuffer(inline_buf_, N) {
        Append(std::forward<Ts>(ts)...);
    }

    // Growable mode. Overflows to heap via allocator.
    explicit FixedStringBuffer(Allocator* overflow)
        : StringBuffer(inline_buf_, N), overflow_(overflow) {}

    template <typename... Ts>
    FixedStringBuffer(Allocator* overflow, Ts... ts)
        : StringBuffer(inline_buf_, N), overflow_(overflow) {
        Append(std::forward<Ts>(ts)...);
    }

    ~FixedStringBuffer() {
        if (heap_buf_) overflow_->Dealloc(heap_buf_, heap_capacity_);
    }

    // Non-copyable, non-movable (contains self-referential pointer).
    FixedStringBuffer(const FixedStringBuffer&) = delete;
    FixedStringBuffer& operator=(const FixedStringBuffer&) = delete;

 private:
    char inline_buf_[N + 1];
    Allocator* overflow_ = nullptr;
    char* heap_buf_ = nullptr;
    size_t heap_capacity_ = 0;
};
```

The growth mechanism lives in `StringBuffer` itself via a virtual-like callback. When `AppendStr` detects it would truncate and an overflow allocator is available, it grows:

```cpp
class StringBuffer {
 protected:
    // Called by AppendStr/VAppendF when the buffer is full.
    // Returns true if growth succeeded and the caller should retry.
    // Default: returns false (truncate).
    virtual bool Grow(size_t needed);

 private:
    void AppendStr(std::string_view s) {
        if (s.size() > remaining()) {
            if (Grow(pos_ + s.size())) {
                // Buffer was reallocated. Retry.
            } else {
                DCHECK(allow_truncation_, "StringBuffer truncated...");
                s = s.substr(0, remaining());
            }
        }
        std::memcpy(buf_ + pos_, s.data(), s.size());
        pos_ += s.size();
        buf_[pos_] = '\0';
    }
};
```

`FixedStringBuffer` overrides `Grow`:

```cpp
template <size_t N>
bool FixedStringBuffer<N>::Grow(size_t needed) {
    if (!overflow_) return false;  // No allocator → truncate.

    // Double until big enough.
    size_t new_cap = heap_capacity_ ? heap_capacity_ : N;
    while (new_cap <= needed) new_cap *= 2;

    char* new_buf;
    if (heap_buf_) {
        new_buf = static_cast<char*>(
            overflow_->Realloc(heap_buf_, heap_capacity_, new_cap + 1, 1));
    } else {
        new_buf = static_cast<char*>(overflow_->Alloc(new_cap + 1, 1));
        std::memcpy(new_buf, inline_buf_, pos_ + 1);  // Copy existing content.
    }
    heap_buf_ = new_buf;
    heap_capacity_ = new_cap;
    // Update StringBuffer's internal pointer to point at heap buffer.
    ResetBuffer(heap_buf_, new_cap);
    return true;
}
```

This requires `StringBuffer` to expose a protected `ResetBuffer` method:

```cpp
class StringBuffer {
 protected:
    // Redirect the buffer pointer (used by growable subclasses).
    void ResetBuffer(char* buf, size_t size) {
        buf_ = buf;
        size_ = size;
    }
};
```

**Why virtual `Grow` instead of a function pointer or template policy?**

The engine already compiles with `-fno-rtti` but does use virtual functions in `Allocator` itself. A virtual `Grow` is one vtable entry, called only on the overflow path (cold). The alternative — a function pointer member — saves nothing and is less clear. A template policy would make `StringBuffer` itself a template, breaking the non-template API that everything depends on.

**Wait — `StringBuffer` currently has no virtual functions.** Adding a vtable to `StringBuffer` changes its layout and adds a pointer to every instance. This is worth examining:

| Approach | `StringBuffer` size | `FixedStringBuffer<256>` size | Notes |
|----------|--------------------|-----------------------------|-------|
| Current (no virtual) | 24 bytes (ptr + pos + size) | 281 bytes (24 + 257) | No vtable |
| With virtual `Grow` | 32 bytes (vptr + ptr + pos + size) | 289 bytes (32 + 257) | 8 bytes overhead per instance |
| Function pointer in StringBuffer | 32 bytes (fptr + ptr + pos + size) | 289 bytes | Same cost, less clean |

8 bytes per instance is negligible — these are stack-allocated temporaries, not arrays of thousands. The vtable pointer is the same cost as a function pointer but gives cleaner override semantics.

**Alternative: avoid virtual entirely.** Keep `StringBuffer` non-virtual. Instead of `Grow` being virtual, make the overflow logic live entirely in `FixedStringBuffer` by overriding `Append`/`AppendF` in the subclass. But `FixedStringBuffer` inherits from `StringBuffer` and callers use `StringBuffer&` references (e.g., `AppendToString(const T&, StringBuffer&)`) — so the override wouldn't be called through a base reference. This doesn't work without virtual dispatch.

**Alternative: callback function pointer stored in StringBuffer.** Avoids the vtable but costs the same 8 bytes:

```cpp
class StringBuffer {
 public:
    using GrowFn = bool (*)(StringBuffer* self, size_t needed);

 protected:
    GrowFn grow_fn_ = nullptr;  // null = no growth
};
```

`FixedStringBuffer` sets `grow_fn_` to a static function that does the realloc. This avoids making `StringBuffer` polymorphic while still enabling growth through a base reference. The callback receives `StringBuffer*` and static-casts to `FixedStringBuffer<N>*` — safe because `FixedStringBuffer` is `final`.

This is the better approach. It keeps `StringBuffer` non-virtual (consistent with the engine's style), costs the same 8 bytes, and works through base references.

```cpp
class StringBuffer {
 public:
    StringBuffer(char* buf, size_t size) : buf_(buf), size_(size) {
        buf_[pos_] = '\0';
    }

 protected:
    using GrowFn = bool (*)(StringBuffer* self, size_t needed);
    GrowFn grow_fn_ = nullptr;

    void ResetBuffer(char* buf, size_t size) {
        buf_ = buf;
        size_ = size;
    }

 private:
    void AppendStr(std::string_view s) {
        if (s.size() > remaining() && grow_fn_ && grow_fn_(this, pos_ + s.size())) {
            // Growth succeeded, retry.
        } else if (s.size() > remaining()) {
            DCHECK(allow_truncation_, "StringBuffer truncated...");
            s = s.substr(0, remaining());
        }
        std::memcpy(buf_ + pos_, s.data(), s.size());
        pos_ += s.size();
        buf_[pos_] = '\0';
    }
};
```

**Usage at callsites:**

```cpp
// Stack-only (identical to today):
Str buf("hello ", name);
PathBuffer path(dir, "/", filename);

// Growable (pass an allocator):
Str buf(&arena, "SELECT * FROM ", table, " WHERE id = ", id);
// If the SQL exceeds 256 bytes, it grows through the arena allocator.
// When buf goes out of scope, the arena owns the memory.

// Growable with the system allocator:
Str buf(SysAllocator(), shader_header);
buf.Append(shader_body);
buf.Append(shader_footer);
// Must outlive buf, or buf's destructor frees through the allocator.
```

The key property: **existing code is unchanged.** A `FixedStringBuffer` without an allocator argument behaves exactly as before — stack-only, truncates on overflow. The allocator argument is purely opt-in.

**Migration path for the three changes:**

| Step | Change | Risk | Effort |
|------|--------|------|--------|
| 1 | Add default template parameter and named aliases | None — additive, no behavior change | Low |
| 2 | Add truncation DCHECK + `AllowTruncation()` | Medium — may uncover existing truncation bugs | Low-medium |
| 3 | Add `grow_fn_` + allocator overflow + destructor cleanup | Low — only active when allocator is passed | Medium |

Steps 1 and 2 can ship independently. Step 3 depends on step 2 (the DCHECK is what triggers growth instead of truncation). All three are backward-compatible — no existing callsite needs to change.

### What this does NOT include

- **Regex.** Out of scope. The engine has no regex use cases. `SplitString` + `Contains` + `ReplaceAll` cover the actual needs.
- **Unicode.** The engine works with ASCII/UTF-8 byte strings. Case conversion and splitting operate on bytes. If Unicode-aware text processing is needed in the future, it belongs in a separate `unicode.h`, not in the general string library.
- **Rope / Cord.** `DynString` with doubling growth is sufficient for the engine's string sizes. Rope structures (like `absl::Cord`) are for strings in the megabyte range, which the engine doesn't produce.
- **String formatting overhaul.** The existing `AppendF` (printf-style) and `Append` (type-safe variadic) are working well. Compile-time format string checking (a la `absl::StrFormat` or C++20 `std::format`) would be nice but is a separate effort with significant complexity.
- **Interning changes.** `StringTable` is working well for its purpose (shader names, sound names, asset keys). No changes proposed.

## File changes summary

| File | What changes |
|------|-------------|
| `src/stringlib.h` | Default template parameter on `FixedStringBuffer`. Named aliases (`Str`, `PathBuffer`, `LogBuffer`, `SqlBuffer`, `SmallBuffer`). `grow_fn_` callback + `ResetBuffer` + `AllowTruncation` + truncation DCHECK in `StringBuffer`. Overflow allocator + destructor + `Grow` static function in `FixedStringBuffer`. New utility functions: `SplitString`, `JoinStrings`, `Trim`/`TrimLeft`/`TrimRight`, `ToLower`/`ToUpper`, `Contains`, `ReplaceFirst`/`ReplaceAll`, `StringToInt`/`StringToFloat`/`StringToDouble` and `Or` variants. |
| `src/stringlib.cc` | Implement new utility functions. |
| `src/constants.h` | No change — `kMaxPathLength` and `kMaxLogLineLength` remain as-is, referenced by the aliases. |
| `src/logging.h` | Optionally migrate `FixedStringBuffer<kMaxLogLineLength>` to `LogBuffer`. Set `AllowTruncation()` on log buffers. |
| `src/packer.cc` | Migrate `FixedStringBuffer<256>` to `SqlBuffer` or `PathBuffer` as appropriate. |
| `src/file_watcher.cc` | Migrate to `PathBuffer` / `Str`. |
| `src/cmd_init.cc` | Migrate to `PathBuffer`. |
| `src/lua_math.cc` | Migrate to `SmallBuffer`. |
| `tests/test.cc` | Tests for truncation detection, overflow growth, and all new string operations. |

## Priorities

| Priority | Change | Depends on |
|----------|--------|-----------|
| 1 | Named aliases + default template parameter | Nothing |
| 2 | Truncation DCHECK + `AllowTruncation()` | Nothing |
| 3 | `grow_fn_` + allocator overflow | Step 2 |
| 4 | Migrate existing callsites to aliases | Step 1 |
| 5 | `SplitString` | Nothing |
| 6 | `Trim` | Nothing |
| 7 | `Contains` | Nothing |
| 8 | `StringToInt` / `StringToFloat` | Nothing |
| 9 | `ReplaceAll` | Nothing |
| 10 | `JoinStrings` | Nothing |
| 11 | `ToLower` / `ToUpper` | Nothing |
