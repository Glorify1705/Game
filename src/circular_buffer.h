#pragma once
#ifndef _GAME_CIRCULAR_BUFFER_H
#define _GAME_CIRCULAR_BUFFER_H

#include <array>
#include <iterator>

#include "allocators.h"
#include "array.h"
#include "logging.h"

namespace G {

template <typename T>
class CircularBuffer {
 public:
  CircularBuffer(size_t size, Allocator* allocator) : buffer_(size, allocator) {
    buffer_.Resize(buffer_.capacity());
  }

  void Push(T t) {
    DCHECK(!full());
    buffer_[start_] = std::move(t);
    if (full_) {
      end_ = Inc(end_);
    }
    start_ = Inc(start_);
    full_ = start_ == end_;
  }

  T& Pop() {
    DCHECK(!empty());
    auto& result = front();
    end_ = Inc(end_);
    full_ = false;
    return result;
  }

  T& operator[](size_t i) {
    DCHECK(i < buffer_.capacity());
    return buffer_[Inc(end_, i)];
  }
  T& front() {
    DCHECK(!empty());
    return buffer_[end_];
  }
  T& back() {
    DCHECK(!empty());
    return buffer_[start_];
  }

  bool full() const { return full_; }
  bool empty() const { return size() == 0; }

  size_t size() const {
    if (full_) return buffer_.size();
    return (start_ < end_) ? (buffer_.size() + start_ - end_) : (start_ - end_);
  }

 private:
  size_t Inc(size_t v, size_t i = 1) { return (v + i) % buffer_.size(); }
  FixedArray<T> buffer_;
  size_t start_ = 0, end_ = 0;
  bool full_ = false;
};

}  // namespace G

#endif  // _GAME_CIRCULAR_BUFFER_H
