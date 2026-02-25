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
    buffer_[start_] = std::move(t);
    if (full_) {
      end_ = Inc(end_);
    }
    start_ = Inc(start_);
    full_ = start_ == end_;
  }

  T Pop() {
    DCHECK(!empty());
    T result = std::move(buffer_[end_]);
    end_ = Inc(end_);
    full_ = false;
    return result;
  }

  T& operator[](size_t i) {
    DCHECK(i < size());
    return buffer_[Inc(end_, i)];
  }

  const T& operator[](size_t i) const {
    DCHECK(i < size());
    return buffer_[Inc(end_, i)];
  }

  T& front() {
    DCHECK(!empty());
    return buffer_[end_];
  }

  const T& front() const {
    DCHECK(!empty());
    return buffer_[end_];
  }

  T& back() {
    DCHECK(!empty());
    return buffer_[Dec(start_)];
  }

  const T& back() const {
    DCHECK(!empty());
    return buffer_[Dec(start_)];
  }

  bool full() const { return full_; }
  bool empty() const { return !full_ && start_ == end_; }

  size_t capacity() const { return buffer_.size(); }

  size_t size() const {
    if (full_) return buffer_.size();
    return (start_ < end_) ? (buffer_.size() + start_ - end_) : (start_ - end_);
  }

 private:
  size_t Inc(size_t v, size_t i = 1) const { return (v + i) % buffer_.size(); }
  size_t Dec(size_t v) const { return (v == 0) ? buffer_.size() - 1 : v - 1; }
  FixedArray<T> buffer_;
  size_t start_ = 0, end_ = 0;
  bool full_ = false;
};

}  // namespace G

#endif  // _GAME_CIRCULAR_BUFFER_H
