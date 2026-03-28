#pragma once
#ifndef _GAME_INLINED_ARRAY_H
#define _GAME_INLINED_ARRAY_H

#include <cstring>
#include <utility>

#include "allocators.h"
#include "logging.h"

namespace G {

// A dynamic array that stores up to N elements inline (inside the
// struct itself) and spills to the allocator only when N is exceeded.
// This avoids allocation entirely for the common case of "usually
// small" collections.
//
// When the array exceeds N elements, the inline storage is abandoned
// and a heap buffer is used instead. The heap buffer follows the same
// 1.5x growth strategy as DynArray.
template <typename T, size_t N>
class InlinedArray {
  static_assert(N > 0, "N must be at least 1");

 public:
  explicit InlinedArray(Allocator* allocator) : allocator_(allocator) {}

  ~InlinedArray() {
    if (is_heap()) {
      allocator_->Dealloc(heap_.data, heap_.capacity * sizeof(T));
    }
  }

  InlinedArray(const InlinedArray&) = delete;
  InlinedArray& operator=(const InlinedArray&) = delete;

  void Push(const T& t) {
    EnsureCapacity();
    ::new (data() + size()) T(t);
    set_size(size() + 1);
  }

  void Push(T&& t) {
    EnsureCapacity();
    ::new (data() + size()) T(std::move(t));
    set_size(size() + 1);
  }

  template <typename... Args>
  void Emplace(Args&&... args) {
    EnsureCapacity();
    ::new (data() + size()) T(std::forward<Args>(args)...);
    set_size(size() + 1);
  }

  void Pop() {
    DCHECK(size() > 0);
    set_size(size() - 1);
  }

  void Clear() { set_size(0); }

  T& operator[](size_t i) {
    DCHECK(i < size(), i, " vs ", size());
    return data()[i];
  }

  const T& operator[](size_t i) const {
    DCHECK(i < size(), i, " vs ", size());
    return data()[i];
  }

  T& back() {
    DCHECK(size() > 0);
    return data()[size() - 1];
  }

  const T& back() const {
    DCHECK(size() > 0);
    return data()[size() - 1];
  }

  T* data() { return is_heap() ? heap_.data : reinterpret_cast<T*>(inline_); }

  const T* data() const {
    return is_heap() ? heap_.data : reinterpret_cast<const T*>(inline_);
  }

  size_t size() const { return size_ & kSizeMask; }
  size_t capacity() const { return is_heap() ? heap_.capacity : N; }
  bool empty() const { return size() == 0; }

  T* begin() { return data(); }
  T* end() { return data() + size(); }
  const T* begin() const { return data(); }
  const T* end() const { return data() + size(); }

 private:
  static constexpr uint32_t kHeapFlag = 0x80000000u;
  static constexpr uint32_t kSizeMask = 0x7FFFFFFFu;

  bool is_heap() const { return size_ & kHeapFlag; }

  void set_size(size_t s) {
    size_ = static_cast<uint32_t>(s) | (size_ & kHeapFlag);
  }

  void set_heap(bool h) {
    if (h)
      size_ |= kHeapFlag;
    else
      size_ &= kSizeMask;
  }

  void EnsureCapacity() {
    size_t s = size();
    if (!is_heap() && s < N) return;
    if (is_heap() && s < heap_.capacity) return;
    SpillOrGrow();
  }

  void SpillOrGrow() {
    size_t s = size();
    if (!is_heap()) {
      // Spill from inline to heap.
      size_t new_cap = N + (N >> 1);
      if (new_cap < N + 1) new_cap = N + 1;
      T* buf =
          static_cast<T*>(allocator_->Alloc(new_cap * sizeof(T), alignof(T)));
      std::memcpy(buf, inline_, s * sizeof(T));
      heap_.data = buf;
      heap_.capacity = static_cast<uint32_t>(new_cap);
      set_heap(true);
    } else {
      // Grow heap buffer.
      size_t old_cap = heap_.capacity;
      size_t new_cap = old_cap + (old_cap >> 1);
      heap_.data = static_cast<T*>(allocator_->Realloc(
          heap_.data, old_cap * sizeof(T), new_cap * sizeof(T), alignof(T)));
      heap_.capacity = static_cast<uint32_t>(new_cap);
    }
  }

  Allocator* allocator_;

  // High bit of size_ encodes is_heap flag.
  uint32_t size_ = 0;

  struct HeapStorage {
    T* data;
    uint32_t capacity;
  };

  union {
    alignas(T) char inline_[N * sizeof(T)];
    HeapStorage heap_;
  };
};

}  // namespace G

#endif  // _GAME_INLINED_ARRAY_H
