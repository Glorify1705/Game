#pragma once
#ifndef _GAME_ERROR_H
#define _GAME_ERROR_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

#include "logging.h"

namespace G {

// Lightweight error value. No heap allocation.
// Carries either an errno code or a string literal message, plus the source
// location where the error was created. File/line are captured automatically
// via __builtin_FILE()/__builtin_LINE() default arguments.
class [[nodiscard]] Error {
 public:
  constexpr Error() = default;

  constexpr static Error Errno(int code, const char* file = __builtin_FILE(),
                               uint32_t line = __builtin_LINE()) {
    DCHECK(code != 0);
    Error e;
    e.code_ = code;
    e.file_ = file;
    e.line_ = line;
    return e;
  }

  template <size_t N>
  constexpr static Error Message(const char (&literal)[N],
                                 const char* file = __builtin_FILE(),
                                 uint32_t line = __builtin_LINE()) {
    Error e;
    e.message_ = std::string_view(literal, N - 1);
    e.file_ = file;
    e.line_ = line;
    return e;
  }

  constexpr int code() const { return code_; }
  constexpr bool is_errno() const { return code_ != 0; }
  constexpr bool ok() const { return code_ == 0 && message_.empty(); }
  constexpr std::string_view message() const { return message_; }
  constexpr std::string_view file() const { return file_; }
  constexpr uint32_t line() const { return line_; }

 private:
  std::string_view message_;
  std::string_view file_;
  int code_ = 0;
  uint32_t line_ = 0;
};

// Tagged union result type: holds either a T value or an E error, never both.
// Move-only. [[nodiscard]] to prevent ignoring errors.
template <typename T, typename E = Error>
class [[nodiscard]] ErrorOr {
 public:
  using ValueType = T;
  using ErrorType = E;

  // Construct from value (implicit).
  template <typename U, typename = std::enable_if_t<
                            std::is_constructible_v<T, U&&> &&
                            !std::is_same_v<std::decay_t<U>, E> &&
                            !std::is_same_v<std::decay_t<U>, ErrorOr>>>
  constexpr ErrorOr(U&& value)
      : value_(std::forward<U>(value)), is_error_(false) {}

  // Construct from error (implicit).
  constexpr ErrorOr(E&& error) : error_(std::move(error)), is_error_(true) {}
  constexpr ErrorOr(const E& error) : error_(error), is_error_(true) {}

  // Move only.
  constexpr ErrorOr(ErrorOr&& other) : is_error_(other.is_error_) {
    if (is_error_) {
      ::new (&error_) E(std::move(other.error_));
    } else {
      ::new (&value_) T(std::move(other.value_));
    }
  }
  ErrorOr(const ErrorOr&) = delete;
  ErrorOr& operator=(const ErrorOr&) = delete;
  ErrorOr& operator=(ErrorOr&&) = delete;

  constexpr bool is_error() const { return is_error_; }

  constexpr T& value() {
    DCHECK(!is_error_);
    return value_;
  }

  constexpr const T& value() const {
    DCHECK(!is_error_);
    return value_;
  }

  constexpr E& error() {
    DCHECK(is_error_);
    return error_;
  }

  constexpr const E& error() const {
    DCHECK(is_error_);
    return error_;
  }

  constexpr T release_value() { return std::move(value()); }
  constexpr E release_error() { return std::move(error()); }

  ~ErrorOr() {
    if (is_error_) {
      error_.~E();
    } else {
      value_.~T();
    }
  }

 private:
  union {
    T value_;
    E error_;
  };
  bool is_error_;
};

// Specialization for void — functions that succeed or fail with no return
// value. Success is a default-constructed Error (ok() == true).
// Returned as `return {};` on success.
template <>
class [[nodiscard]] ErrorOr<void, Error> {
 public:
  using ValueType = void;
  using ErrorType = Error;

  constexpr ErrorOr() = default;
  constexpr ErrorOr(Error error) : error_(error) {}

  constexpr ErrorOr(ErrorOr&&) = default;
  ErrorOr(const ErrorOr&) = delete;
  ErrorOr& operator=(const ErrorOr&) = delete;
  ErrorOr& operator=(ErrorOr&&) = delete;

  constexpr bool is_error() const { return !error_.ok(); }

  constexpr Error& error() {
    DCHECK(is_error());
    return error_;
  }

  constexpr const Error& error() const {
    DCHECK(is_error());
    return error_;
  }

  constexpr Error release_error() { return error(); }
  constexpr void release_value() {}

 private:
  Error error_;
};

// TRY evaluates an ErrorOr<T> expression. If it holds an error, returns
// the error from the enclosing function. Otherwise, yields the value.
// Equivalent to Zig's `try` keyword.
// Uses GCC statement expressions (supported by GCC, Clang, Emscripten).
#define TRY(expression)                            \
  ({                                               \
    auto&& _temporary_result = (expression);       \
    if (_temporary_result.is_error()) [[unlikely]] \
      return _temporary_result.release_error();    \
    _temporary_result.release_value();             \
  })

// MUST unwraps an ErrorOr<T> expression. If it holds an error, crashes
// with CHECK (includes file/line info). Otherwise, yields the value.
// Equivalent to Zig's `catch unreachable`.
#define MUST(expression)                                                       \
  ({                                                                           \
    auto&& _temporary_result = (expression);                                   \
    CHECK(!_temporary_result.is_error(), _temporary_result.error().message()); \
    _temporary_result.release_value();                                         \
  })

}  // namespace G

#endif  // _GAME_ERROR_H
