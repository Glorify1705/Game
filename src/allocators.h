#pragma once
#ifndef _GAME_ALLOCATORS_H
#define _GAME_ALLOCATORS_H

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include "logging.h"

namespace G {

inline static size_t Align(size_t n, size_t m) {
  return (n + m - 1) & ~(m - 1);
};

template <class T, class Allocator>
class STLAllocatorWrapper {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using propagate_on_container_move_assignment = std::true_type;
  template <class U>
  struct rebind {
    using other = STLAllocatorWrapper<U, Allocator>;
  };
  using is_always_equal = std::false_type;

  STLAllocatorWrapper(Allocator* allocator = nullptr)
      : allocator_(*allocator) {}

  Allocator& allocator() const { return allocator_; }

  pointer address(reference x) const { return &x; }
  const_pointer address(const_reference x) const { return &x; }
  pointer allocate(size_type n, const void* /*hint*/ = nullptr) {
    return allocator_.template NewArray<T>(n);
  }
  void deallocate(T* p, std::size_t n) { allocator_.Dealloc(p, n); }
  size_type max_size() const {
    return std::numeric_limits<size_type>::max() / sizeof(value_type);
  }

  template <class U, class... Args>
  void construct(U* p, Args&&... args) {
    ::new (const_cast<void*>(static_cast<const void*>(p)))
        U(std::forward<Args>(args)...);
  }

  template <class U>
  void destroy(U* p) {
    p->~U();
  }

  bool operator==(const STLAllocatorWrapper& other) const {
    return &allocator_ == &other.allocator_;
  }
  bool operator!=(const STLAllocatorWrapper& other) const {
    return &allocator_ != &other.allocator_;
  }

 private:
  Allocator& allocator_;
};

template <typename BaseAllocator>
class TopAllocator {
 public:
  template <typename T>
  T* New() {
    return static_cast<T*>(
        static_cast<BaseAllocator*>(this)->Alloc(sizeof(T), alignof(T)));
  }

  template <typename T>
  T* NewArray(size_t num) {
    return static_cast<T*>(
        static_cast<BaseAllocator*>(this)->Alloc(num * sizeof(T), alignof(T)));
  }
};

class SystemAllocator : public TopAllocator<SystemAllocator> {
 public:
  void* Alloc(size_t size, size_t /*align*/) { return std::malloc(size); }

  void Dealloc(void* p, size_t /*sz*/) { std::free(p); }

  void* Realloc(void* p, size_t /*old_size*/, size_t new_size,
                size_t /*align*/) {
    return std::realloc(p, new_size);
  }

  static SystemAllocator* Instance() {
    static SystemAllocator allocator;
    return &allocator;
  }
};

class StackAllocator : public TopAllocator<StackAllocator> {
 public:
  StackAllocator(void* ptr, size_t size) {
    ptr_ = reinterpret_cast<uintptr_t>(ptr);
    end_ = ptr_ + size;
    pos_ = ptr_;
  }

  void* Alloc(size_t size, size_t align) {
    DCHECK(ptr_ + size <= end_, "Out of memory");
    pos_ = Align(pos_, align);
    void* result = reinterpret_cast<void*>(pos_);
    pos_ += size;
    return result;
  }

  void Dealloc(void* p, size_t sz) {
    if (p == nullptr) return;
    auto pos = reinterpret_cast<uintptr_t>(p);
    if (pos + sz == pos_) pos_ = pos;
  }

  void* Realloc(void* p, size_t old_size, size_t new_size, size_t align) {
    auto pos = reinterpret_cast<uintptr_t>(p);
    if (pos + old_size == pos_) {
      pos_ = pos + new_size;
      return reinterpret_cast<void*>(pos);
    } else {
      pos_ = Align(pos_, align);
      std::memcpy(reinterpret_cast<void*>(pos_), p, old_size);
      auto* result = reinterpret_cast<void*>(pos_);
      pos_ += new_size;
      return result;
    }
  }

  size_t used_memory() const { return pos_ - ptr_; }
  size_t total_memory() const { return end_ - ptr_; }

 private:
  uintptr_t ptr_;
  uintptr_t pos_;
  uintptr_t end_;
};

template <size_t Size>
class StaticAllocator : public StackAllocator {
 public:
  StaticAllocator() : StackAllocator(buffer_, Size) {}

  void* Alloc(size_t size, size_t align) {
    return StackAllocator::Alloc(size, align);
  }
  void Dealloc(void* p, size_t sz) { return StackAllocator::Dealloc(p, sz); }
  void* Realloc(void* p, size_t old_size, size_t new_size, size_t align) {
    return StackAllocator::Realloc(p, old_size, new_size, align);
  }

 private:
  alignas(std::max_align_t) uint8_t buffer_[Size];
};

template <class C, class Allocator>
struct WrapAllocator;

template <template <class, class> class C, class T1, class A, class Allocator>
struct WrapAllocator<C<T1, A>, Allocator> {
  using type =
      C<T1, STLAllocatorWrapper<typename C<T1, A>::value_type, Allocator>>;
};

template <template <class, class, class> class C, class T1, class T2, class A,
          class Allocator>
struct WrapAllocator<C<T1, T2, A>, Allocator> {
  using type =
      C<T1, T2,
        STLAllocatorWrapper<typename C<T1, T2, A>::value_type, Allocator>>;
};

template <class C, class Allocator>
using WithAllocator = typename WrapAllocator<C, Allocator>::type;

}  // namespace G

#endif