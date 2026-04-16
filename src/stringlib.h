#pragma once
#ifndef _GAME_STRINGLIB_H
#define _GAME_STRINGLIB_H

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <type_traits>

#include "constants.h"

namespace G {

class Allocator;

// Tag type for constructing a FixedStringBuffer that silently truncates.
struct Truncating {};
inline constexpr Truncating kTruncating;

void PrintDouble(double val, char* buffer, size_t size);

class StringBuffer;

// Type-erased allocator operations for StringBuffer growth. Defined in
// stringlib.cc where Allocator is a complete type.
void* StringBufferAlloc(void* allocator, size_t size);
void* StringBufferRealloc(void* allocator, void* ptr, size_t old_size,
                          size_t new_size);
void StringBufferDealloc(void* allocator, void* ptr, size_t size);

namespace internal_strings {

template <typename T, typename = void>
struct HasAppendString : public std::false_type {};

template <typename T>
struct HasAppendString<
    T, std::enable_if_t<std::is_void_v<decltype(AppendToString(
           std::declval<const T&>(), std::declval<StringBuffer&>()))>>>
    : public std::true_type {};

class Alphanumeric {
 public:
  Alphanumeric(float f) {
    PrintDouble(static_cast<double>(f), buf_, sizeof(buf_));
    piece_ = std::string_view(buf_);
  }
  Alphanumeric(int i) {
    snprintf(buf_, sizeof(buf_), "%d", i);
    piece_ = std::string_view(buf_);
  }
  Alphanumeric(long i) {
    snprintf(buf_, sizeof(buf_), "%ld", i);
    piece_ = std::string_view(buf_);
  }
  Alphanumeric(unsigned int i) {
    snprintf(buf_, sizeof(buf_), "%u", i);
    piece_ = std::string_view(buf_);
  }

  Alphanumeric(unsigned long long i) {
    snprintf(buf_, sizeof(buf_), "%llu", i);
    piece_ = std::string_view(buf_);
  }

  Alphanumeric(long long i) {
    snprintf(buf_, sizeof(buf_), "%lld", i);
    piece_ = std::string_view(buf_);
  }

  Alphanumeric(long unsigned int i) {
    snprintf(buf_, sizeof(buf_), "%lu", i);
    piece_ = std::string_view(buf_);
  }

  Alphanumeric(double d) {
    PrintDouble(d, buf_, sizeof(buf_));
    piece_ = std::string_view(buf_);
  }

  Alphanumeric(void* b) {
    snprintf(buf_, sizeof(buf_), "0x%016llx",
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(b)));
    piece_ = std::string_view(buf_);
  }

  Alphanumeric(const char* c) : piece_(c) {}

  Alphanumeric(const unsigned char* c)
      : piece_(reinterpret_cast<const char*>(c)) {}
  Alphanumeric(std::string_view c) : piece_(c) {}

  Alphanumeric(char c) {
    buf_[0] = c;
    piece_ = std::string_view(buf_);
  }
  Alphanumeric(const Alphanumeric&) = delete;
  Alphanumeric& operator=(const Alphanumeric&) = delete;

  std::string_view view() const { return piece_; }

 private:
  char buf_[32] = {0};
  std::string_view piece_;
};

}  // namespace internal_strings

// Writes formatted text into a caller-provided character buffer. Does not own
// the buffer. Supports type-safe variadic Append and printf-style AppendF.
class StringBuffer {
 public:
  // Callback type for growable subclasses. Returns true if the buffer was
  // grown and the caller should retry the append.
  using GrowFn = bool (*)(StringBuffer* self, size_t needed);

  StringBuffer(char* buf, size_t size) : buf_(buf), size_(size) {
    buf_[pos_] = '\0';
  }

  // Appends one or more values to the buffer.
  template <typename... T>
  StringBuffer& Append(const T&... ts) {
    (AppendOne(ts), ...);
    buf_[pos_] = '\0';
    return *this;
  }

  // Appends the contents of another StringBuffer.
  StringBuffer& AppendBuffer(const StringBuffer& buf) {
    AppendStr(buf.view());
    buf_[pos_] = '\0';
    return *this;
  }

  // Appends raw bytes from a buffer.
  StringBuffer& AppendBuffer(void* buf, size_t size) {
    AppendRaw(buf, size);
    return *this;
  }

  // Appends a printf-style formatted string.
  __attribute__((format(printf, 2, 3))) StringBuffer& AppendF(const char* fmt,
                                                              ...) {
    va_list l;
    va_start(l, fmt);
    VAppendF(fmt, l);
    va_end(l);
    return *this;
  }

  // Clears the buffer and appends the given values.
  template <typename... T>
  StringBuffer& Set(const T&... ts) {
    pos_ = 0;
    return Append(ts...);
  }

  // Clears the buffer and appends a printf-style formatted string.
  __attribute__((format(printf, 2, 3))) StringBuffer& SetF(const char* fmt,
                                                           ...) {
    pos_ = 0;
    va_list l;
    va_start(l, fmt);
    VAppendF(fmt, l);
    va_end(l);
    return *this;
  }

  // Returns the null-terminated string.
  const char* str() const { return buf_; }

  explicit operator const char*() const { return buf_; }

  // Returns the number of bytes written.
  size_t size() const { return pos_; }

  // Returns a string_view of the written content.
  std::string_view view() const { return std::string_view(str(), size()); }

  // Resets the buffer to empty.
  void Clear() { pos_ = 0; }

  // Returns true if no bytes have been written.
  bool empty() const { return pos_ == 0; }

  // Returns the number of bytes that can still be written.
  size_t remaining() const { return pos_ >= size_ ? 0 : (size_ - pos_); }

  // Returns the total buffer capacity.
  size_t capacity() const { return size_; }

  // Allows this buffer to silently truncate on overflow. By default, overflow
  // triggers an assertion in debug builds.
  void AllowTruncation() { allow_truncation_ = true; }

  // Enables Append(some_string_buffer) via the AppendToString protocol.
  friend void AppendToString(const StringBuffer& b, StringBuffer& sink) {
    sink.AppendStr(b.view());
    sink.buf_[sink.pos_] = '\0';
  }

 protected:
  // Redirects the buffer pointer. Used by growable subclasses after realloc.
  void ResetBuffer(char* buf, size_t size) {
    buf_ = buf;
    size_ = size;
  }

  // Sets the growth callback. Used by growable subclasses.
  void SetGrowFn(GrowFn fn) { grow_fn_ = fn; }

 private:
  template <typename T>
  void AppendOne(const T& t) {
    if constexpr (internal_strings::HasAppendString<T>::value) {
      AppendToString(t, *this);
    } else {
      AppendStr(internal_strings::Alphanumeric(t).view());
    }
  }

  void VAppendF(const char* fmt, va_list l) {
    // Format string is validated by __attribute__((format)) on callers.
    // NOLINTNEXTLINE(clang-diagnostic-format-nonliteral)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    va_list copy;
    va_copy(copy, l);
    int needed = vsnprintf(&buf_[pos_], size_ - pos_, fmt, l);
    size_t uneeded = static_cast<size_t>(needed);
    if (uneeded > size_ - pos_ && grow_fn_ && grow_fn_(this, pos_ + uneeded)) {
      // Buffer grew. Retry the format into the new space.
      vsnprintf(&buf_[pos_], size_ - pos_, fmt, copy);
      pos_ += uneeded;
    } else if (uneeded > size_ - pos_) {
#ifdef GAME_WITH_ASSERTS
      assert(allow_truncation_ &&
             "StringBuffer truncated in AppendF (buffer too small)");
#endif
      pos_ = size_ > 0 ? size_ - 1 : 0;
    } else {
      pos_ += uneeded;
    }
    va_end(copy);
#pragma GCC diagnostic pop
  }

  void AppendStr(std::string_view s) {
    if (s.size() > remaining()) {
      if (grow_fn_ && grow_fn_(this, pos_ + s.size())) {
        // Growth succeeded. Fall through to the memcpy below.
      } else {
#ifdef GAME_WITH_ASSERTS
        assert(allow_truncation_ &&
               "StringBuffer truncated (buffer too small)");
#endif
        s = s.substr(0, remaining());
      }
    }
    std::memcpy(buf_ + pos_, s.data(), s.size());
    pos_ += s.size();
  }

  void AppendRaw(const void* data, size_t size) {
    if (size > remaining()) {
      if (grow_fn_ && grow_fn_(this, pos_ + size)) {
        // Growth succeeded.
      } else {
#ifdef GAME_WITH_ASSERTS
        assert(allow_truncation_ &&
               "StringBuffer truncated (buffer too small)");
#endif
        size = remaining();
      }
    }
    std::memcpy(buf_ + pos_, data, size);
    pos_ += size;
  }

  char* buf_;
  size_t pos_ = 0;
  size_t size_;
  GrowFn grow_fn_ = nullptr;
  bool allow_truncation_ = false;
};

// Fixed-capacity string buffer with inline storage. Optionally grows through
// an Allocator when the inline capacity is exceeded.
template <size_t N = 256>
class FixedStringBuffer final : public StringBuffer {
 public:
  // Stack-only mode. Truncates on overflow (with assert in debug builds).
  FixedStringBuffer() : StringBuffer(inline_buf_, N) {}

  // Stack-only mode with initial content.
  template <typename... Ts>
  explicit FixedStringBuffer(Ts... ts) : StringBuffer(inline_buf_, N) {
    Append(std::forward<Ts>(ts)...);
  }

  // Stack-only mode that silently truncates on overflow.
  explicit FixedStringBuffer(Truncating) : StringBuffer(inline_buf_, N) {
    AllowTruncation();
  }

  // Stack-only mode that silently truncates, with initial content.
  template <typename... Ts>
  FixedStringBuffer(Truncating, Ts... ts) : StringBuffer(inline_buf_, N) {
    AllowTruncation();
    Append(std::forward<Ts>(ts)...);
  }

  // Growable mode. Overflows to heap via the given allocator.
  explicit FixedStringBuffer(Allocator* overflow)
      : StringBuffer(inline_buf_, N), overflow_(overflow) {
    SetGrowFn(&GrowCallback);
  }

  // Growable mode with initial content.
  template <typename... Ts>
  FixedStringBuffer(Allocator* overflow, Ts... ts)
      : StringBuffer(inline_buf_, N), overflow_(overflow) {
    SetGrowFn(&GrowCallback);
    Append(std::forward<Ts>(ts)...);
  }

  ~FixedStringBuffer() {
    if (heap_buf_)
      StringBufferDealloc(overflow_, heap_buf_, heap_capacity_ + 1);
  }

  FixedStringBuffer(const FixedStringBuffer&) = delete;
  FixedStringBuffer& operator=(const FixedStringBuffer&) = delete;

 private:
  // Growth callback invoked by StringBuffer when the buffer is full.
  static bool GrowCallback(StringBuffer* self, size_t needed) {
    auto* buf = static_cast<FixedStringBuffer<N>*>(self);
    if (!buf->overflow_) return false;

    // 1.5x growth so freed blocks can be reused (Fibonacci property).
    size_t new_cap = buf->heap_capacity_ ? buf->heap_capacity_ : N;
    while (new_cap <= needed) new_cap = new_cap + new_cap / 2;

    char* new_buf;
    if (buf->heap_buf_) {
      new_buf = static_cast<char*>(
          StringBufferRealloc(buf->overflow_, buf->heap_buf_,
                              buf->heap_capacity_ + 1, new_cap + 1));
    } else {
      new_buf =
          static_cast<char*>(StringBufferAlloc(buf->overflow_, new_cap + 1));
      std::memcpy(new_buf, buf->inline_buf_, buf->size() + 1);
    }
    buf->heap_buf_ = new_buf;
    buf->heap_capacity_ = new_cap;
    buf->ResetBuffer(new_buf, new_cap);
    return true;
  }

  char inline_buf_[N + 1];
  void* overflow_ = nullptr;
  char* heap_buf_ = nullptr;
  size_t heap_capacity_ = 0;
};

// Named aliases for common buffer sizes.
using Str = FixedStringBuffer<>;
using PathBuffer = FixedStringBuffer<kMaxPathLength>;
using LogBuffer = FixedStringBuffer<kMaxLogLineLength>;
using SqlBuffer = FixedStringBuffer<1024>;
using SmallBuffer = FixedStringBuffer<64>;

// Ensures a string_view is null-terminated for C API calls. Zero-copy when the
// byte after the view is already '\0'; copies into an inline buffer otherwise.
template <size_t N = kMaxPathLength>
class NullTerminated {
 public:
  explicit NullTerminated(std::string_view sv) {
    assert(sv.size() < N && "string_view too large for NullTerminated buffer");
    if (sv.data()[sv.size()] == '\0') {
      ptr_ = sv.data();
    } else {
      std::memcpy(buf_, sv.data(), sv.size());
      buf_[sv.size()] = '\0';
      ptr_ = buf_;
    }
  }
  // Returns the null-terminated string.
  const char* c_str() const { return ptr_; }
  operator const char*() const { return ptr_; }

 private:
  char buf_[N + 1];
  const char* ptr_;
};

bool HasSuffix(std::string_view str, std::string_view suffix);
bool ConsumeSuffix(std::string_view* str, std::string_view suffix);
bool HasPrefix(std::string_view str, std::string_view prefix);
bool ConsumePrefix(std::string_view* str, std::string_view prefix);

inline std::string_view Basename(std::string_view p) {
  size_t pos = p.size() - 1;
  for (; pos != 0 && p[pos] != '/';) {
    pos--;
  }
  return p[pos] == '/' ? p.substr(pos + 1) : p;
}

inline std::string_view WithoutExt(std::string_view p) {
  size_t pos = p.size() - 1;
  for (; pos != 0 && p[pos] != '.';) {
    pos--;
  }
  return p[pos] == '.' ? p.substr(0, pos) : p;
}

inline std::string_view Extension(std::string_view p) {
  size_t pos = p.size() - 1;
  for (; pos != 0 && p[pos] != '.';) {
    pos--;
  }
  return (pos == 0 && p[pos] != '.') ? p : p.substr(pos + 1);
}

// Duplicate a string_view into a null-terminated arena allocation.
const char* StrDupZ(Allocator* allocator, std::string_view s);

}  // namespace G

#endif  // _GAME_STRINGLIB_H
