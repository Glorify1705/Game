#pragma once
#ifndef _GAME_ALLOCATORS_H
#define _GAME_ALLOCATORS_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "logging.h"

namespace G {

class BumpAllocator {
 public:
  explicit BumpAllocator(size_t size);

  void* Alloc(size_t size, size_t align);
  template <typename T>
  T* AllocArray(size_t size) {
    return reinterpret_cast<T*>(Alloc(size * sizeof(T), alignof(T)));
  }

  void Dealloc(void* /*ptr*/, size_t /*size*/) {}

  size_t used() const { return pos_ - ptr_; }
  size_t total() const { return end_ - ptr_; }

 private:
  inline static size_t Align(size_t n, size_t m) {
    return (n + m - 1) & ~(m - 1);
  };

  uintptr_t ptr_ = 0;
  uintptr_t pos_ = 0;
  uintptr_t end_ = 0;
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

  BumpAllocator& allocator() const { return allocator_; }

  pointer address(reference x) const { return &x; }
  const_pointer address(const_reference x) const { return &x; }
  pointer allocate(size_type n, const void* /*hint*/ = nullptr) {
    return allocator_.template AllocArray<T>(n);
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