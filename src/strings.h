#pragma once
#ifndef _GAME_STRINGS_H
#define _GAME_STRINGS_H

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

namespace G {

void PrintDouble(double val, char* buffer, size_t size);

class StringBuffer;

namespace internal_strings {

template <typename T, typename = void>
struct HasAppendString : public std::false_type {};

template <typename T>
struct HasAppendString<
    T, std::enable_if_t<std::is_void_v<decltype(AppendToString(
           std::declval<const T&>(), std::declval<std::string&>()))>>>
    : public std::true_type {};

class Alphanumeric {
 public:
  Alphanumeric(float f) {
    PrintDouble(f, buf_, sizeof(buf_));
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

  Alphanumeric(const char* c) : piece_(c) {}

  Alphanumeric(const unsigned char* c)
      : piece_(reinterpret_cast<const char*>(c)) {}
  Alphanumeric(const std::string& c) : piece_(c) {}
  Alphanumeric(std::string_view c) : piece_(c) {}

  template <typename T,
            typename = typename std::enable_if_t<HasAppendString<T>::value>>
  Alphanumeric(const T& t, std::string&& output = {}) {
    AppendToString(t, output);
    piece_ = std::string_view(output);
  }

  Alphanumeric(char c) {
    buf_[0] = c;
    piece_ = std::string_view(buf_);
  }
  Alphanumeric(const Alphanumeric&) = delete;
  Alphanumeric& operator=(const Alphanumeric&) = delete;

  std::string_view piece() const { return piece_; }

 private:
  char buf_[32] = {0};
  std::string_view piece_;
};

}  // namespace internal_strings

template <typename... T>
std::string StrCat(T... ts) {
  return [&](std::initializer_list<std::string_view> ss) {
    std::string result;
    size_t total = 0;
    for (auto& s : ss) total += s.size();
    result.reserve(total);
    for (auto& s : ss) result.append(s.data(), s.size());
    return result;
  }({internal_strings::Alphanumeric(ts).piece()...});
}

template <typename... T>
void StrAppend(std::string* buf, T... ts) {
  [&](std::initializer_list<std::string_view> ss) {
    size_t total = 0;
    for (auto& s : ss) total += s.size();
    buf->reserve(std::max(buf->size() * 2, buf->size() + total));
    for (auto& s : ss) buf->append(s.data(), s.size());
  }({internal_strings::Alphanumeric(ts).piece()...});
}

class StringBuffer {
 public:
  StringBuffer(char* buf, size_t size) : buf_(buf), size_(size) {
    buf_[pos_] = '\0';
  }

  template <typename... T>
  StringBuffer& Append(T... ts) {
    [&](std::initializer_list<std::string_view> ss) {
      for (auto& s : ss) AppendStr(s);
    }({internal_strings::Alphanumeric(ts).piece()...});
    buf_[pos_] = '\0';
    return *this;
  }

  void Append(StringBuffer& buf) { Append(buf.str()); }

  StringBuffer& AppendF(const char* fmt, ...) {
    va_list l;
    va_start(l, fmt);
    VAppendF(fmt, l);
    va_end(l);
    return *this;
  }

  template <typename... T>
  StringBuffer& Set(T... ts) {
    pos_ = 0;
    return Append(std::forward<T>(ts)...);
  }

  StringBuffer& SetF(const char* fmt, ...) {
    pos_ = 0;
    va_list l;
    va_start(l, fmt);
    VAppendF(fmt, l);
    va_end(l);
    return *this;
  }

  const char* str() const { return buf_; }

  operator const char*() const { return buf_; }

  size_t size() const { return pos_; }

  std::string_view piece() const { return std::string_view(str(), size()); }

  void Clear() { pos_ = 0; }
  bool empty() const { return pos_ == 0; }

 private:
  void VAppendF(const char* fmt, va_list l) {
    int needed = vsnprintf(&buf_[pos_], size_ - pos_, fmt, l);
    pos_ = std::min(size_, pos_ + needed);
  }

  void AppendStr(std::string_view s) {
    const size_t length = capacity(s.size());
    std::memcpy(buf_ + pos_, s.data(), length);
    pos_ += length;
  }

  size_t remaining() const { return pos_ >= size_ ? 0 : (size_ - pos_); }
  size_t capacity(size_t cs) const { return std::min(cs, remaining()); }

  char* buf_;
  size_t pos_ = 0, size_;
};

template <size_t N>
class FixedStringBuffer final : public StringBuffer {
 public:
  FixedStringBuffer() : StringBuffer(buf_, N) {}

  template <typename... Ts>
  explicit FixedStringBuffer(Ts... ts) : StringBuffer(buf_, N) {
    Append(std::forward<Ts>(ts)...);
  }

 private:
  char buf_[N + 1];
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

}  // namespace G

#endif  // _GAME_STRINGS_H
