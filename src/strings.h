#pragma once
#ifndef _GAME_STRINGS_H
#define _GAME_STRINGS_H

#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

namespace G {
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
    snprintf(buf_, sizeof(buf_), "%.2f", f);
    piece_ = std::string_view(buf_);
  }
  Alphanumeric(int i) {
    snprintf(buf_, sizeof(buf_), "%d", i);
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
    snprintf(buf_, sizeof(buf_), "%.2lf", d);
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

  Alphanumeric(char c) = delete;
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

template <size_t N>
class StringBuffer {
 public:
  StringBuffer() = default;
  template <typename... Ts>
  explicit StringBuffer(Ts... ts) {
    Append(std::forward<Ts>(ts)...);
  }

  template <typename... T>
  void Append(T... ts) {
    [&](std::initializer_list<std::string_view> ss) {
      for (auto& s : ss) AppendStr(s);
    }({internal_strings::Alphanumeric(ts).piece()...});
  }

  template <typename... T>
  void Set(T... ts) {
    pos_ = 0;
    Append(std::forward<T>(ts)...);
  }

  const char* str() const { return buf_; }

  operator const char*() const { return buf_; }

  size_t size() const { return pos_; }

  std::string_view piece() const { return std::string_view(str(), size()); }

  void Clear() { pos_ = 0; }
  bool empty() const { return pos_ == 0; }

 private:
  void AppendStr(std::string_view s) {
    const size_t length = cap(s.size());
    std::memcpy(buf_ + pos_, s.data(), length);
    pos_ += length;
  }

  size_t remaining() const { return pos_ >= N ? 0 : (N - pos_); }
  size_t cap(size_t cs) const { return std::min(cs, remaining()); }

  char buf_[N + 1] = {0};
  size_t pos_ = 0;
};

bool HasSuffix(std::string_view str, std::string_view suffix);
bool ConsumeSuffix(std::string_view* str, std::string_view suffix);

}  // namespace G

#endif  // _GAME_STRINGS_H
