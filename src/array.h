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
    ::new (at(elems_)) T(std::move(t));
    elems_++;
  }
  void Push(const T& t) {
    DCHECK(elems_ < Size);
    ::new (at(elems_)) T(t);
    elems_++;
  }

  void Pop() {
    DCHECK(elems_ > 0);
    elems_--;
  }
  void Clear() { elems_ = 0; }

  bool empty() const { return elems_ == 0; }

  T& back() {
    DCHECK(elems_ > 0);
    return *at(elems_ - 1);
  }
  T& operator[](size_t index) {
    DCHECK(index < elems_, index, " vs ", elems_);
    return *at(index);
  }
  const T& operator[](size_t index) const {
    DCHECK(index < elems_, index, " vs ", elems_);
    return *at(index);
  }

  T* data() { return at(0); }

  T* begin() { return data(); }
  T* end() { return data() + elems_; }

  const T* cbegin() const { return data(); }
  const T* cend() const { return data() + elems_; }

  size_t size() const { return elems_; }
  size_t capacity() const { return Size; }
  size_t bytes() const { return elems_ * sizeof(T); }

  void Resize(size_t size) { elems_ = size; }

 private:
  T* at(size_t i) { return reinterpret_cast<T*>(&buffer_[i * sizeof(T)]); }

  alignas(T) uint8_t buffer_[Size * sizeof(T)];
  size_t elems_ = 0;
};

}  // namespace G

#endif  // _FIXED_ARRAY_H