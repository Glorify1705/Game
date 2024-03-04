#pragma once
#ifndef _GAME_CIRCULAR_BUFFER_H
#define _GAME_CIRCULAR_BUFFER_H

#include <array>
#include <iterator>

#include "allocators.h"
#include "logging.h"

namespace G {

template <typename T>
class CircularBuffer {
 public:
  CircularBuffer(size_t size, Allocator* allocator) : buffer_(size, allocator) {
    buffer_.Resize(buffer_.capacity());
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
    DCHECK(i < buffer_.capacity());
    return buffer_[Inc(end_, i)];
  }
  T& front() { return buffer_[end_]; }
  T& back() { return buffer_[start_]; }

  bool full() const { return full_; }
  bool empty() const { return size() == 0; }

  size_t size() const {
    return (start_ <= end_) ? (buffer_.size() + start_ - end_)
                            : (start_ - end_);
  }

 private:
  size_t Inc(size_t v, size_t i = 1) { return (v + i) % buffer_.size(); }
  FixedArray<T> buffer_;
  size_t start_ = 0, end_ = 0;
  bool full_ = false;
};

}  // namespace G

#endif  // _GAME_CIRCULAR_BUFFER_H