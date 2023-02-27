#pragma once
#ifndef _GAME_CIRCULAR_BUFFER_H
#define _GAME_CIRCULAR_BUFFER_H

#include <array>

#include "logging.h"

template <typename T, size_t N>
class FixedCircularBuffer {
  static_assert(!(N & (N - 1)), "Circular Buffer Size is not a power of two");

 public:
  void Push(T t) {
    buffer_[start_] = std::move(t);
    start_ = Inc(start_);
  }

  void Pop() { end_ = Inc(end_); }

  T& operator[](size_t i) {
    DCHECK(i < N);
    return buffer_[Inc(end_, i)];
  }
  T& back() { return buffer_[end_ - 1]; }

 private:
  static constexpr size_t Inc(size_t v, size_t i = 1) { return (v + i) % N; }
  std::array<T, N> buffer_;
  size_t start_ = 0, end_ = 0;
};

#endif  // _GAME_CIRCULAR_BUFFER_H