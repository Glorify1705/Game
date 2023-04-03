#pragma once
#ifndef _FIXED_ARRAY_H
#define _FIXED_ARRAY_H

#include <array>
#include <cstddef>
#include <iterator>

#include "allocators.h"
#include "bits.h"
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

 private:
  Allocator allocator_;
  T* buffer_;
  size_t elems_ = 0;
};

template <typename T, typename Allocator>
class DynArray {
 public:
  template <typename, typename>
  friend class DynArray;

  DynArray(Allocator* allocator) : allocator_(allocator) {}
  ~DynArray() { allocator_->Dealloc(buffer_, capacity_); }

  template <typename Alloc>
  DynArray(DynArray<T, Alloc>&& other) {
    Move(other);
  }

  template <typename Alloc>
  DynArray& operator=(DynArray<T, Alloc>&& other) {
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

  T* begin() { return buffer_; }
  T* end() { return buffer_ + elems_; }

  const T* cbegin() const { return data(); }
  const T* cend() const { return data() + elems_; }

  size_t size() const { return elems_; }
  size_t capacity() const { return capacity_; }
  size_t bytes() const { return elems_ * sizeof(T); }

  void Resize(size_t size) {
    size_t new_capacity = NextPow2(size);
    elems_ = size;
    if (buffer_ == nullptr) {
      capacity_ = new_capacity;
      buffer_ = allocator_->template NewArray<T>(capacity_);
    } else if (capacity_ < new_capacity) {
      buffer_ = static_cast<T*>(
          allocator_->Realloc(buffer_, capacity_ * sizeof(T),
                              new_capacity * sizeof(T), alignof(T)));
      capacity_ = new_capacity;
    }
  }

 private:
  template <typename Alloc>
  void Move(DynArray<T, Alloc>&& other) {
    if (buffer_ != nullptr) {
      allocator_->Dealloc(buffer_, capacity_ * sizeof(T));
    }
    if constexpr (std::is_same_v<Allocator, Alloc>) {
      if (allocator_ == other.allocator_) {
        buffer_ = other.buffer_;
        allocator_ = other.allocator_;
      } else {
        CopyBuffer(other);
        other.allocator_->Dealloc(other.buffer_, other.capacity_ * sizeof(T));
      }
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

  template <typename Alloc>
  void CopyBuffer(const DynArray<T, Alloc>& other) {
    buffer_ = allocator_->template NewArray<T>(other.capacity_);
    std::memcpy(buffer_, other.buffer_, other.elems_ * sizeof(T));
  }

  void ResizeIfNeeded() {
    if (buffer_ == nullptr) {
      capacity_ = 16;
      buffer_ = allocator_->template NewArray<T>(capacity_);
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

}  // namespace G

#endif  // _FIXED_ARRAY_H