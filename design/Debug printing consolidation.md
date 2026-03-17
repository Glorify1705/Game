# Debug Printing Consolidation

## Problem

Custom types (vectors, matrices, stats) have **three parallel string-formatting APIs** that evolved independently:

| API | Sink type | Where defined | Used by |
|-----|-----------|---------------|---------|
| `operator<<(std::ostream&, const T&)` | `std::ostream&` | vec.h, mat.h | Nothing in the engine |
| `DebugString(StringBuffer&)` | `StringBuffer&` | vec.h only | `lua_math.cc` (3 callsites) |
| `AppendToString(const T&, std::string&)` | `std::string&` | vec.h, mat.h, stats.h/cc | `Alphanumeric` -> `LOG()`, `StringBuffer::Append()` |

This is bad for three reasons:

1. **`<ostream>` is a heavy include.** It pulls in `<ios>`, `<streambuf>`, locale machinery, etc. The engine never uses any of this. The `operator<<` overloads exist purely out of habit — nothing in the codebase streams a `FVec2` to `std::cout` or a `std::ostringstream`. They're dead code that increases compile time.

2. **`std::string&` as a sink violates the project's allocation philosophy.** The engine avoids STL containers and controls all memory through its allocator hierarchy. `AppendToString(const T&, std::string&)` forces a `std::string` allocation at every callsite — including inside `Alphanumeric`, where a temporary `std::string` is heap-allocated just to format a vector for `LOG()`. The engine already has `StringBuffer` for exactly this purpose.

3. **Three APIs means inconsistent coverage and bugs.** `DebugString()` exists on vec types but not matrices. The `operator<<` and `AppendToString` implementations have subtly different formatting (e.g., `DebugString` omits commas: `{ 1 2 }` vs `{ 1, 2 }`). Worse, DMat and IMat types in `mat.h` define `AppendToString` as a **member function** instead of a friend free function, so they silently fail the `HasAppendString` SFINAE trait and cannot be used with `LOG()`, `StrCat()`, or `StringBuffer::Append()` at all.

## Current architecture

### How `LOG()` dispatches through `AppendToString`

```
LOG("pos = ", player_pos)

  expands to → ::G::Log(__FILE__, __LINE__, "pos = ", player_pos)

  Log() creates → FixedStringBuffer<1024> buf("[file.cc:42] ")
                   buf.Append("pos = ", player_pos)

  StringBuffer::Append() creates → {Alphanumeric("pos = ").piece(),
                                     Alphanumeric(player_pos).piece()}

  Alphanumeric("pos = ") → matches Alphanumeric(const char*)
                            stores string_view directly

  Alphanumeric(player_pos) → player_pos is FVec2
                              → matches template<T> Alphanumeric(const T&)
                                where HasAppendString<FVec2>::value == true
                              → creates temporary std::string
                              → calls AppendToString(player_pos, temp_string)
                              → stores string_view into temp_string
```

The temporary `std::string` in the last step is the problem. It heap-allocates to format a ~20-character vector string, then gets discarded. Every `LOG()` call with a custom type does this.

### Formatting differences across the three APIs

For `FVec2{1.5f, 2.5f}`:

| API | Output |
|-----|--------|
| `operator<<` | `{ 1.5, 2.5 }` (default float precision) |
| `DebugString` | `{ 1.5 2.5 }` (no commas) |
| `AppendToString` | `{ 1.500, 2.500 }` (3 decimal places) |

The `AppendToString` format is the most intentional (explicit precision), so we'll keep that as the canonical output.

### The DMat/IMat bug

Float matrices define `AppendToString` correctly as a friend free function:

```cpp
// FMat2x2 (mat.h:148) — CORRECT: found by ADL
friend void AppendToString(const FMat2x2& m, std::string& sink) {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
        // ...
        StrAppend(&sink, m.v[row * kDimension + col]);
    }
    sink.append(" }");
}
```

Double and integer matrices define it as a member function:

```cpp
// DMat2x2 (mat.h:595) — BROKEN: invisible to HasAppendString
void AppendToString(std::string& sink) const {
    sink.append("{ ");
    // ...identical body...
    sink.append(" }");
}
```

The `HasAppendString` trait checks for a free function `AppendToString(const T&, std::string&)` via ADL. A member function has a different signature (`this->AppendToString(sink)`) and is invisible to the trait. So `LOG("matrix = ", some_dmat)` fails to compile or falls through to an incorrect overload. This has gone unnoticed because the engine currently only uses `FMat` types in practice.

### `stats.h` also has a dead private member

```cpp
class Stats {
  friend void AppendToString(const Stats& stats, std::string& str);  // public API
private:
  void AppendToString(char* buf, size_t len) const;  // declared but never defined or called
};
```

The private member is dead code.

## Design: single `AppendToString(const T&, StringBuffer&)` API

Replace all three APIs with a single free function taking `StringBuffer&`:

```cpp
friend void AppendToString(const FVec2& v, StringBuffer& sink) {
    sink.AppendF("{ %.3f, %.3f }", v.x, v.y);
}
```

This integrates with the existing infrastructure because `Alphanumeric` (inside `StringBuffer::Append`) detects it via `HasAppendString` and dispatches to it. The only change to `Alphanumeric` is switching from a temporary `std::string` to writing into its own internal buffer via `StringBuffer`.

### `Alphanumeric` changes

Before:

```cpp
class Alphanumeric {
    // ...primitive constructors...

    template <typename T,
              typename = typename std::enable_if_t<HasAppendString<T>::value>>
    Alphanumeric(const T& t, std::string&& output = {}) {
        AppendToString(t, output);           // writes into temporary std::string
        piece_ = std::string_view(output);   // points into the heap-allocated string
    }

    char buf_[32] = {0};
    std::string_view piece_;
};
```

After:

```cpp
class Alphanumeric {
    // ...primitive constructors unchanged...

    // Declared in class, defined out-of-line after StringBuffer
    template <typename T,
              typename = typename std::enable_if_t<HasAppendString<T>::value>>
    Alphanumeric(const T& t);

    char buf_[128] = {0};       // increased from 32 to handle custom types
    std::string_view piece_;
};

// After StringBuffer class definition:
namespace internal_strings {
template <typename T, typename U>
Alphanumeric::Alphanumeric(const T& t) {
    StringBuffer sb(buf_, sizeof(buf_));
    AppendToString(t, sb);     // writes into internal buf_ via StringBuffer
    piece_ = sb.piece();       // points into buf_, no heap allocation
}
}  // namespace internal_strings
```

**Why out-of-line?** There's a circular dependency in `stringlib.h`:

```
Alphanumeric (line 30)
  → custom type constructor needs StringBuffer to wrap buf_
StringBuffer (line 126)
  → Append() template creates Alphanumeric objects
```

The constructor must be defined after `StringBuffer` but before any code that instantiates it. Since `Alphanumeric` is only instantiated through `StringBuffer::Append` (a template function instantiated at call sites in other TUs), defining it anywhere after `StringBuffer` in the header works.

**Why 128 bytes?** The current `buf_[32]` handles numeric types (longest is a pointer: `0x00007fff12345678` = 18 chars). Custom types need more:

| Type | Max output length | Example |
|------|------------------|---------|
| `FVec2` | ~22 chars | `{ 1.234, 5.678 }` |
| `FVec4` | ~46 chars | `{ 1.234, 5.678, 9.012, 3.456 }` |
| `Stats` | ~120 chars | `min = 1.234 max = 5.678 avg = 9.012 ...` |
| `FMat4x4` | ~280 chars | 16 floats in nested braces |

128 bytes handles all vec types and Stats. Matrices truncate gracefully — `StringBuffer` writes as much as fits and stops without crashing. For debug output, truncation is acceptable.

**Stack impact:** `Alphanumeric` grows from 48 bytes (32 buf + 16 string_view) to 144 bytes (128 buf + 16 string_view). A `LOG()` call with 8 arguments creates 8 temporaries = 1152 bytes. This is well within normal stack budgets and replaces 8 potential heap allocations.

### `HasAppendString` trait change

```cpp
// Before (line 24-28):
template <typename T>
struct HasAppendString<
    T, std::enable_if_t<std::is_void_v<decltype(AppendToString(
           std::declval<const T&>(), std::declval<std::string&>()))>>>
    : public std::true_type {};

// After:
template <typename T>
struct HasAppendString<
    T, std::enable_if_t<std::is_void_v<decltype(AppendToString(
           std::declval<const T&>(), std::declval<StringBuffer&>()))>>>
    : public std::true_type {};
```

This works with just the forward declaration `class StringBuffer;` (already on line 17) because `std::declval<StringBuffer&>()` only needs `StringBuffer` to be a declared type, not a complete type. The reference can be formed from an incomplete type.

### Vec type changes

Each vec type (FVec2, FVec3, FVec4, DVec2, DVec3, DVec4, IVec2, IVec3, IVec4 — 9 total) currently has:

```cpp
// REMOVE: pulls in <ostream> for no reason
friend std::ostream& operator<<(std::ostream& os, const FVec2& v) { ... }

// REMOVE: redundant with AppendToString
void DebugString(StringBuffer& sink) const { ... }

// KEEP and CHANGE: canonical API
friend void AppendToString(const FVec2& v, std::string& sink) { ... }
```

New `AppendToString` implementations use `AppendF` for float/double types and `Append` for integer types:

```cpp
// Float vectors: explicit 3-decimal precision
friend void AppendToString(const FVec2& v, StringBuffer& sink) {
    sink.AppendF("{ %.3f, %.3f }", v.x, v.y);
}

friend void AppendToString(const FVec3& v, StringBuffer& sink) {
    sink.AppendF("{ %.3f, %.3f, %.3f }", v.x, v.y, v.z);
}

friend void AppendToString(const FVec4& v, StringBuffer& sink) {
    sink.AppendF("{ %.3f, %.3f, %.3f, %.3f }", v.x, v.y, v.z, v.w);
}

// Double vectors: explicit 3-decimal precision
friend void AppendToString(const DVec2& v, StringBuffer& sink) {
    sink.AppendF("{ %.3lf, %.3lf }", v.x, v.y);
}

// ... DVec3, DVec4 follow the same pattern ...

// Integer vectors: use Append (no format specifier needed)
friend void AppendToString(const IVec2& v, StringBuffer& sink) {
    sink.AppendF("{ %d, %d }", v.x, v.y);
}

// ... IVec3, IVec4 follow the same pattern ...
```

### Mat type changes

Each mat type (FMat2x2, FMat3x3, FMat4x4, DMat2x2, DMat3x3, DMat4x4, IMat2x2, IMat3x3, IMat4x4 — 9 total) needs:

1. Remove `operator<<` overload
2. Change `AppendToString` to take `StringBuffer&`
3. For DMat/IMat: convert from member function to friend free function (fixes the ADL bug)

```cpp
// Before (FMat2x2, friend — correct shape, wrong sink):
friend void AppendToString(const FMat2x2& m, std::string& sink) {
    sink.append("{ ");
    for (...) {
        StrAppend(&sink, m.v[row * kDimension + col]);  // uses std::string* overload
    }
    sink.append(" }");
}

// Before (DMat2x2, member — wrong shape AND wrong sink):
void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (...) {
        StrAppend(&sink, v[row * kDimension + col]);
    }
    sink.append(" }");
}

// After (both FMat and DMat — same shape, correct sink):
friend void AppendToString(const FMat2x2& m, StringBuffer& sink) {
    sink.Append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
        sink.Append("{ ");
        for (size_t col = 0; col < kDimension; ++col) {
            sink.Append(m.v[row * kDimension + col]);
            if (col + 1 < kDimension) sink.Append(", ");
        }
        sink.Append(" }");
        if (row + 1 < kDimension) sink.Append(", ");
    }
    sink.Append(" }");
}
```

### Stats changes

```cpp
// stats.h — before:
#include <string>
class Stats {
    friend void AppendToString(const Stats& stats, std::string& str);
private:
    void AppendToString(char* buf, size_t len) const;  // dead code
};

// stats.h — after:
class StringBuffer;   // forward declaration
class Stats {
    friend void AppendToString(const Stats& stats, StringBuffer& sink);
    // dead private AppendToString(char*, size_t) removed
};

// stats.cc — before:
void AppendToString(const Stats& stats, std::string& str) {
    StrAppend(&str, "min = ", stats.min(), ...);
}

// stats.cc — after:
#include "stringlib.h"
void AppendToString(const Stats& stats, StringBuffer& sink) {
    sink.Append("min = ", stats.min(), " max = ", stats.max(),
                " avg = ", stats.avg(), " stdev = ", stats.stdev(),
                " p50 = ", stats.Percentile(50), " p90 = ", stats.Percentile(90),
                " p99 = ", stats.Percentile(99));
}
```

### `lua_math.cc` callsite migration

The 3 `DebugString` callsites become `Append` calls:

```cpp
// Before (lua_math.cc:215):
auto* v = AsUserdata<FVec2>(state, 1);
FixedStringBuffer<32> buf;
v->DebugString(buf);
lua_pushlstring(state, buf.str(), buf.size());

// After:
auto* v = AsUserdata<FVec2>(state, 1);
FixedStringBuffer<64> buf;
buf.Append(*v);
lua_pushlstring(state, buf.str(), buf.size());
```

Note: buffer size increased from 32 to 64 to match the formatted output with commas and 3-decimal precision (e.g., `{ -123.456, -789.012 }` = 28 chars for FVec2, up to ~55 chars for FVec4).

## Include cleanup

After removing `operator<<` and `std::string&` from `AppendToString`:

| File | Remove | Keep |
|------|--------|------|
| `src/vec.h` | `#include <ostream>`, `#include <string>` | `#include "stringlib.h"` (already present) |
| `src/mat.h` | `#include <ostream>` | `#include "stringlib.h"` (already present) |
| `src/stats.h` | `#include <string>` | Add `class StringBuffer;` forward decl |
| `src/stats.cc` | | Add `#include "stringlib.h"` |

## Deleted: `StrCat`, `StrAppend`, and `Alphanumeric(const std::string&)`

These had zero callers in the codebase and were removed:

- **`StrCat`** and **`StrAppend`** — string-building utilities that returned/modified `std::string`. All callsites already use `StringBuffer` or `LOG()` directly.
- **`Alphanumeric(const std::string&)`** — constructor overload for `std::string`. Only needed to support passing `std::string` values to `StrCat`/`StrAppend`.

This allowed removing `#include <string>` from `stringlib.h`.

## File changes summary

| File | What changes |
|------|-------------|
| `src/stringlib.h` | `HasAppendString` checks `StringBuffer&`; `Alphanumeric` buf grows to 128, custom type constructor moves out-of-line |
| `src/vec.h` | Remove `<ostream>`, `<string>`. Delete 9 `operator<<`, 9 `DebugString`. Rewrite 9 `AppendToString` to use `StringBuffer&` |
| `src/mat.h` | Remove `<ostream>`. Delete 9 `operator<<`. Rewrite 9 `AppendToString` to use `StringBuffer&`, fix 6 member->friend |
| `src/stats.h` | Remove `<string>`, add `class StringBuffer;` forward decl. Change friend decl to `StringBuffer&`. Delete dead private member |
| `src/stats.cc` | Add `#include "stringlib.h"`. Rewrite `AppendToString` to use `sink.Append(...)` |
| `src/lua_math.cc` | 3 lines: `v->DebugString(buf)` -> `buf.Append(*v)`. Increase buffer sizes from 32 to 64 |

## Adding `AppendToString` to new types

After this change, the pattern for making any struct printable is:

```cpp
struct MyThing {
    int id;
    float value;

    friend void AppendToString(const MyThing& t, StringBuffer& sink) {
        sink.Append("MyThing{id=", t.id, ", value=", t.value, "}");
    }
};

// Now works everywhere:
LOG("got ", thing);                         // via StringBuffer::Append -> AppendToString
FixedStringBuffer<64> buf(thing);           // via StringBuffer::Append -> AppendToString
```

No additional includes needed beyond `stringlib.h`. No `operator<<`, no `<ostream>`, no `std::string` sink.
