#pragma once
#ifndef _FIXED_ARRAY_H
#define _FIXED_ARRAY_H

#include <array>
#include <cstddef>
#include <iterator>

#include "logging.h"

namespace G {

template <typename T, size_t Size>
class FixedArray {
 public:
  void Push(T&& t) {
    DCHECK(elems_ < Size);
    buffer_[elems_++] = std::move(t);
  }
  void Push(const T& t) {
    DCHECK(elems_ < Size);
    buffer_[elems_++] = t;
  }

  void Pop() {
    DCHECK(elems_ > 0);
    elems_--;
  }
  void Clear() { elems_ = 0; }

  T& back() {
    DCHECK(elems_ > 0);
    return buffer_[elems_ - 1];
  }
  T& operator[](size_t index) {
    DCHECK(index < elems_, index, " vs", elems_);
    return buffer_[index];
  }
  const T& operator[](size_t index) const {
    DCHECK(index < elems_, index, " vs", elems_);
    return buffer_[index];
  }

  T* begin() { return &buffer_[0]; }
  T* end() { return buffer_ + elems_; }

  T* data() { return buffer_; }

  const T* cbegin() const { return &buffer_[0]; }
  const T* cend() const { return buffer_ + elems_; }

  size_t size() const { return elems_; }
  size_t capacity() const { return Size; }
  size_t bytes() const { return elems_ * sizeof(T); }
  bool empty() const { return elems_ == 0; }

  void Resize(size_t size) { elems_ = size; }

 private:
  T buffer_[Size];

  size_t elems_ = 0;
};

}  // namespace G

#endif  // _FIXED_ARRAY_H