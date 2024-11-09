#pragma once
#ifndef _GAME_ALLOCATORS_H
#define _GAME_ALLOCATORS_H

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

namespace G {

inline static std::size_t Align(std::size_t n, std::size_t m) {
  return (n + m - 1) & ~(m - 1);
};

class Allocator {
 public:
  virtual void* Alloc(std::size_t size, std::size_t align) = 0;

  virtual void Dealloc(void* p, std::size_t sz) = 0;

  virtual void* Realloc(void* p, std::size_t old_size, std::size_t new_size,
                        std::size_t align) = 0;

  virtual void Reset() = 0;
};

template <typename T>
void Destroy(Allocator* allocator, T* ptr) {
  if constexpr (!std::is_trivially_destructible_v<T>) {
    ptr->~T();
  }
  allocator->Dealloc(ptr, sizeof(T));
}

template <typename T, typename... Args>
T* New(Allocator* allocator, Args... args) {
  T* ptr = reinterpret_cast<T*>(allocator->Alloc(sizeof(T), alignof(T)));
  ::new (ptr) T(std::forward<Args>(args)...);
  return ptr;
}

inline std::string_view StrDup(Allocator* allocator, std::string_view s) {
  char* result = reinterpret_cast<char*>(allocator->Alloc(s.size(), 1));
  std::memcpy(result, s.data(), s.size());
  return std::string_view(result, s.size());
}

template <typename T, typename... Args>
T* BraceInit(Allocator* allocator, Args... args) {
  T* ptr = reinterpret_cast<T*>(allocator->Alloc(sizeof(T), alignof(T)));
  ::new (ptr) T{std::forward<Args>(args)...};
  return ptr;
}

template <typename T>
T* NewArray(size_t n, Allocator* allocator) {
  return reinterpret_cast<T*>(allocator->Alloc(n * sizeof(T), alignof(T)));
}

template <typename T>
void DeallocArray(T* ptr, std::size_t n, Allocator* allocator) {
  allocator->Dealloc(ptr, n * sizeof(T));
}

template <class T, class Allocator>
class STLAllocatorWrapper {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
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
  void* Alloc(std::size_t size, std::size_t /*align*/) override {
    return std::malloc(size);
  }

  void Dealloc(void* p, std::size_t /*sz*/) override {
    if (p != nullptr) std::free(p);
  }

  void* Realloc(void* p, std::size_t /*old_size*/, std::size_t new_size,
                std::size_t /*align*/) override {
    return std::realloc(p, new_size);
  }

  void Reset() override { /*pass*/
  }

  static SystemAllocator* Instance() {
    static SystemAllocator allocator;
    return &allocator;
  }
};

class BumpAllocator : public Allocator {
 public:
  BumpAllocator(void* buffer, std::size_t size) {
    pos_ = reinterpret_cast<uintptr_t>(buffer);
    beginning_ = pos_;
    end_ = pos_ + size;
  }

  void* Alloc(std::size_t size, std::size_t align) override {
    if (pos_ + size > end_) {
      return nullptr;
    }
    pos_ = Align(pos_, align);
    auto* result = reinterpret_cast<void*>(pos_);
    pos_ += size;
    return result;
  }
  void Dealloc(void* ptr, std::size_t sz) override {
    if (ptr == nullptr) return;
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    if ((p + sz) == pos_) pos_ = p;
  }
  void* Realloc(void* p, std::size_t old_size, std::size_t new_size,
                std::size_t align) override {
    auto* res = Alloc(new_size, align);
    std::memcpy(res, p, old_size);
    return res;
  }

  void Reset() { pos_ = beginning_; }

  std::size_t used_memory() const { return pos_ - beginning_; }
  std::size_t total_memory() const { return end_ - beginning_; }

 private:
  uintptr_t beginning_;
  uintptr_t pos_;
  uintptr_t end_;
};

template <size_t Size>
class StaticAllocator final : public BumpAllocator {
 public:
  StaticAllocator() : BumpAllocator(buffer_, Size) {}

 private:
  alignas(std::max_align_t) uint8_t buffer_[Size];
};

class ArenaAllocator final : public Allocator {
 public:
  ArenaAllocator(Allocator* allocator, std::size_t size)
      : allocator_(allocator),
        buffer_(allocator->Alloc(size, /*align=*/alignof(std::max_align_t))),
        size_(size),
        a_(buffer_, size) {}

  ~ArenaAllocator() { allocator_->Dealloc(buffer_, size_); }

  void* Alloc(std::size_t size, std::size_t align) override {
    return a_.Alloc(size, align);
  }
  void Dealloc(void* ptr, std::size_t sz) override { a_.Dealloc(ptr, sz); }
  void* Realloc(void* p, std::size_t old_size, std::size_t new_size,
                std::size_t align) override {
    return a_.Realloc(p, old_size, new_size, align);
  }

  void Reset() override { a_.Reset(); }

 private:
  Allocator* const allocator_;
  void* const buffer_;
  std::size_t size_;
  BumpAllocator a_;
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
