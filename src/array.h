#pragma once
#ifndef _FIXED_ARRAY_H
#define _FIXED_ARRAY_H

#include <stddef.h>

#include <array>
#include <iterator>

#include "allocators.h"
#include "bits.h"
#include "logging.h"

namespace G {

template <typename T>
class FixedArray {
 public:
  FixedArray(size_t n, Allocator* allocator) : allocator_(allocator), size_(n) {
    buffer_ = allocator->NewArray<T>(size_);
  }
  ~FixedArray() { allocator_->DeallocArray<T>(buffer_, size_); }

  void Push(T&& t) {
    EnsureBufferIsAvailable();
    DCHECK(elems_ < size_, elems_, " vs ", size_);
    ::new (&buffer_[elems_]) T(std::move(t));
    elems_++;
  }
  void Push(const T& t) {
    EnsureBufferIsAvailable();
    DCHECK(elems_ < size_, elems_, " vs ", size_);
    ::new (&buffer_[elems_]) T(t);
    elems_++;
  }

  T* Insert(const T* ptr, size_t n) {
    EnsureBufferIsAvailable();
    DCHECK(elems_ + n < size_, "cannot fit ", n, " elements");
    auto* result = &buffer_[elems_];
    std::memcpy(result, ptr, n * sizeof(T));
    elems_ += n;
    return result;
  }

  void Resize(size_t s) { elems_ = s; }

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
    return buffer_[index];
  }

  T* data() { return buffer_; }
  const T* cdata() const { return buffer_; }

  T* begin() const { return buffer_; }
  T* end() const { return buffer_ + elems_; }

  const T* cbegin() const { return buffer_; }
  const T* cend() const { return buffer_ + elems_; }

  size_t size() const { return elems_; }
  size_t capacity() const { return size_; }
  size_t bytes() const { return elems_ * sizeof(T); }

 private:
  void EnsureBufferIsAvailable() {
    if (buffer_ == nullptr) {
      buffer_ = allocator_->NewArray<T>(size_);
    }
  }

  Allocator* allocator_;
  T* buffer_;
  size_t elems_ = 0;
  const size_t size_;
};

template <typename T>
class DynArray {
 public:
  DynArray(Allocator* allocator) : allocator_(allocator) {}

  DynArray(size_t size, Allocator* allocator) : allocator_(allocator) {
    capacity_ = size;
    buffer_ =
        static_cast<T*>(allocator_->Alloc(capacity_ * sizeof(T), alignof(T)));
  }

  ~DynArray() {
    if (allocator_) allocator_->Dealloc(buffer_, capacity_ * sizeof(T));
  }

  DynArray(DynArray<T>&& other) { Move(other); }

  DynArray& operator=(DynArray<T>&& other) {
    Move(std::move(other));
    return *this;
  }

  template <typename... Args>
  void Emplace(Args... args) {
    ResizeIfNeeded();
    ::new (&buffer_[elems_]) T(std::forward<Args>(args)...);
    elems_++;
  }

  void Push(T&& t) {
    ResizeIfNeeded();
    ::new (&buffer_[elems_]) T(std::move(t));
    elems_++;
  }

  void Push(const T& t) {
    ResizeIfNeeded();
    ::new (&buffer_[elems_]) T(t);
    elems_++;
  }

  void Insert(const T* ptr, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      Push(ptr[i]);
    }
  }

  void Pop() {
    DCHECK(elems_ > 0);
    elems_--;
  }

  void Clear() {
    allocator_->DeallocArray(buffer_, capacity_);
    elems_ = 0;
    buffer_ = nullptr;
    capacity_ = 0;
  }

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
    return buffer_[index];
  }

  T* data() { return buffer_; }
  const T* cdata() const { return buffer_; }

  T* begin() { return buffer_; }
  T* end() { return buffer_ + elems_; }

  const T* cbegin() const { return buffer_; }
  const T* cend() const { return buffer_ + elems_; }

  size_t size() const { return elems_; }
  size_t capacity() const { return capacity_; }
  size_t bytes() const { return elems_ * sizeof(T); }

  void Reserve(size_t size) {
    size_t new_capacity = NextPow2(size);
    if (buffer_ == nullptr) {
      capacity_ = new_capacity;
      buffer_ = allocator_->NewArray<T>(capacity_);
    } else if (capacity_ < new_capacity) {
      buffer_ = static_cast<T*>(
          allocator_->Realloc(buffer_, capacity_ * sizeof(T),
                              new_capacity * sizeof(T), alignof(T)));
      capacity_ = new_capacity;
    }
  }

 private:
  void Move(DynArray<T>&& other) {
    if (buffer_ != nullptr) {
      allocator_->Dealloc(buffer_, capacity_ * sizeof(T));
    }
    if (allocator_ == other.allocator_) {
      buffer_ = other.buffer_;
    } else {
      CopyBuffer(other);
      other.allocator_->Dealloc(other.buffer_, other.capacity_ * sizeof(T));
    }
    elems_ = other.elems_;
    capacity_ = other.capacity_;
    other.elems_ = other.capacity_ = 0;
    other.buffer_ = nullptr;
    other.allocator_ = nullptr;
  }

  void CopyBuffer(const DynArray<T>& other) {
    buffer_ = allocator_->NewArray<T>(other.capacity_);
    std::memcpy(buffer_, other.buffer_, other.elems_ * sizeof(T));
  }

  void ResizeIfNeeded() {
    if (buffer_ == nullptr) {
      capacity_ = 16;
      buffer_ = allocator_->NewArray<T>(capacity_);
    } else if (elems_ == capacity_) {
      const size_t new_capacity = capacity_ * 2;
      buffer_ = static_cast<T*>(
          allocator_->Realloc(buffer_, capacity_ * sizeof(T),
                              new_capacity * sizeof(T), alignof(T)));
      capacity_ = new_capacity;
    }
  }

  Allocator* allocator_;
  T* buffer_ = nullptr;
  size_t elems_ = 0;
  size_t capacity_ = 0;
};

template <typename T>
class ArrayView {
 public:
  explicit ArrayView(const T* array, size_t size)
      : array_(array), size_(size){};

  using const_iterator = const T*;

  const_iterator begin() const { return array_; }
  const_iterator end() const { return array_ + size_; }

 private:
  const T* const array_;
  const size_t size_;
};

template <typename T>
ArrayView<T> MakeArrayView(const DynArray<T>& a) {
  return ArrayView(a.cdata(), a.size());
}

template <typename T>
ArrayView<T> MakeArrayView(const FixedArray<T>& a) {
  return ArrayView(a.cdata(), a.size());
}

}  // namespace G

#endif  // _FIXED_ARRAY_H
