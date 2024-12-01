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
void DeallocArray(T* ptr, size_t n, Allocator* allocator) {
  allocator->Dealloc(ptr, n * sizeof(T));
}

class SystemAllocator final : public Allocator {
 public:
  void* Alloc(size_t size, size_t /*align*/) override {
    return std::malloc(size);
  }

  void Dealloc(void* p, size_t /*sz*/) override {
    if (p != nullptr) std::free(p);
  }

  void* Realloc(void* p, size_t /*old_size*/, size_t new_size,
                size_t /*align*/) override {
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
  BumpAllocator(void* buffer, size_t size) {
    pos_ = reinterpret_cast<uintptr_t>(buffer);
    beginning_ = pos_;
    end_ = pos_ + size;
  }

  void* Alloc(size_t size, size_t align) override {
    if (pos_ + size > end_) {
      return nullptr;
    }
    pos_ = Align(pos_, align);
    auto* result = reinterpret_cast<void*>(pos_);
    pos_ += size;
    return result;
  }
  void Dealloc(void* ptr, size_t sz) override {
    if (ptr == nullptr) return;
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    if ((p + sz) == pos_) pos_ = p;
  }
  void* Realloc(void* p, size_t old_size, size_t new_size,
                size_t align) override {
    auto* res = Alloc(new_size, align);
    std::memcpy(res, p, old_size);
    return res;
  }

  void Reset() override { pos_ = beginning_; }

  size_t used_memory() const { return pos_ - beginning_; }
  size_t total_memory() const { return end_ - beginning_; }

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
  ArenaAllocator(Allocator* allocator, size_t size)
      : allocator_(allocator),
        buffer_(allocator->Alloc(size, /*align=*/alignof(std::max_align_t))),
        size_(size),
        a_(buffer_, size) {}

  ~ArenaAllocator() { allocator_->Dealloc(buffer_, size_); }

  void* Alloc(size_t size, size_t align) override {
    return a_.Alloc(size, align);
  }
  void Dealloc(void* ptr, size_t sz) override { a_.Dealloc(ptr, sz); }
  void* Realloc(void* p, size_t old_size, size_t new_size,
                size_t align) override {
    return a_.Realloc(p, old_size, new_size, align);
  }

  void Reset() override { a_.Reset(); }

 private:
  Allocator* const allocator_;
  void* const buffer_;
  size_t size_;
  BumpAllocator a_;
};

}  // namespace G

#endif
