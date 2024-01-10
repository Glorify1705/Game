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

class Allocator {
 public:
  virtual void* Alloc(size_t size, size_t align) = 0;

  virtual void Dealloc(void* p, size_t sz) = 0;

  virtual void* Realloc(void* p, size_t old_size, size_t new_size,
                        size_t align) = 0;
};

template <typename T>
T* New(Allocator* allocator) {
  return reinterpret_cast<T*>(allocator->Alloc(sizeof(T), alignof(T)));
}

template <typename T>
void Destroy(Allocator* allocator, T* ptr) {
  ptr->~T();
  allocator->Dealloc(ptr, sizeof(T));
}

template <typename T, typename... Args>
T* DirectInit(Allocator* allocator, Args... args) {
  auto* ptr = New<T>(allocator);
  ::new (ptr) T(std::forward<Args>(args)...);
  return ptr;
}

template <typename T, typename... Args>
T* BraceInit(Allocator* allocator, Args... args) {
  auto* ptr = New<T>(allocator);
  ::new (ptr) T{std::forward<Args>(args)...};
  return ptr;
}

template <typename T>
T* NewArray(size_t n, Allocator* allocator) {
  return reinterpret_cast<T*>(allocator->Alloc(n * sizeof(T), alignof(T)));
}

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
    return NewArray<T>(n, &allocator_);
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

class SystemAllocator final : public Allocator {
 public:
  void* Alloc(size_t size, size_t /*align*/) override {
    return std::malloc(size);
  }

  void Dealloc(void* p, size_t /*sz*/) override { std::free(p); }

  void* Realloc(void* p, size_t /*old_size*/, size_t new_size,
                size_t /*align*/) override {
    return std::realloc(p, new_size);
  }

  static SystemAllocator* Instance() {
    static SystemAllocator allocator;
    return &allocator;
  }
};

template <size_t Size>
class StaticAllocator final : public Allocator {
 public:
  void* Alloc(size_t size, size_t align) override {
    DCHECK(pos_ + size <= Size);
    pos_ = Align(pos_, align);
    auto* result = reinterpret_cast<void*>(&buffer_[pos_]);
    pos_ += size;
    return result;
  }
  void Dealloc(void* /*p*/, size_t /*sz*/) override {
    // Pass.
  }
  void* Realloc(void* p, size_t old_size, size_t new_size,
                size_t align) override {
    auto* res = Alloc(new_size, align);
    std::memcpy(res, p, old_size);
    return res;
  }

  size_t used_memory() const { return pos_; }
  size_t total_memory() const { return Size; }

 private:
  alignas(std::max_align_t) uint8_t buffer_[Size];
  size_t pos_ = 0;
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