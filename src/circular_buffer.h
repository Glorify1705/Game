#pragma once
#ifndef _GAME_CIRCULAR_BUFFER_H
#define _GAME_CIRCULAR_BUFFER_H

#include <array>
#include <iterator>

#include "allocators.h"
#include "logging.h"

namespace G {

template <typename T, size_t N>
class CircularBuffer {
  static_assert(!(N & (N - 1)), "Circular Buffer Size is not a power of two");

 public:
  CircularBuffer(Allocator* allocator) : buffer_(allocator) {
    buffer_.Reserve(N);
  }

  void Push(T t) {
    buffer_[start_] = std::move(t);
    start_ = Inc(start_);
    if (start_ == end_) full_ = true;
  }

  T& Pop() {
    auto& result = front();
    end_ = Inc(end_);
    full_ = false;
    return result;
  }

  T& operator[](size_t i) {
    DCHECK(i < N);
    return buffer_[Inc(end_, i)];
  }
  T& front() { return buffer_[end_]; }

  bool full() const { return full_; }
  bool empty() const { return size() == 0; }

  size_t size() const {
    return (start_ <= end_) ? (N + start_ - end_) : (start_ - end_);
  }

 private:
  static constexpr size_t Inc(size_t v, size_t i = 1) { return (v + i) % N; }
  FixedArray<T, N> buffer_;
  size_t start_ = 0, end_ = 0;
  bool full_ = false;
};

}  // namespace G

#endif  // _GAME_CIRCULAR_BUFFER_H