#pragma once
#ifndef _FIXED_ARRAY_H
#define _FIXED_ARRAY_H

#include <array>
#include <cstddef>
#include <iterator>

#include "allocators.h"
#include "logging.h"

namespace G {

template <typename T, size_t Elements,
          typename Allocator = StaticAllocator<Elements * sizeof(T)>>
class FixedArray {
 public:
  inline static constexpr size_t TotalSize = Elements * sizeof(T);

  FixedArray()
      : buffer_(static_cast<T*>(allocator_.Alloc(TotalSize, alignof(T)))) {}
  ~FixedArray() { allocator_.Dealloc(buffer_, TotalSize); }

  void Push(T&& t) {
    DCHECK(elems_ < Elements);
    ::new (&buffer_[elems_]) T(std::move(t));
    elems_++;
  }
  void Push(const T& t) {
    DCHECK(elems_ < Elements);
    ::new (&buffer_[elems_]) T(t);
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
    return buffer_[elems_ - 1];
  }
  T& operator[](size_t index) {
    DCHECK(index < elems_, index, " vs ", elems_);
    return buffer_[index];
  }
  const T& operator[](size_t index) const {
    DCHECK(index < elems_, index, " vs ", elems_);
    return *buffer_[index];
  }

  T* data() { return buffer_; }

  T* begin() { return buffer_; }
  T* end() { return buffer_ + elems_; }

  const T* cbegin() const { return data(); }
  const T* cend() const { return data() + elems_; }

  size_t size() const { return elems_; }
  size_t capacity() const { return Elements; }
  size_t bytes() const { return elems_ * sizeof(T); }

  void Resize(size_t size) { elems_ = size; }

 private:
  Allocator allocator_;
  T* buffer_;
  size_t elems_ = 0;
};

}  // namespace G

#endif  // _FIXED_ARRAY_H