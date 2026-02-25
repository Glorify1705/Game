#pragma once
#ifndef _GAME_ALLOCATORS_H
#define _GAME_ALLOCATORS_H

#include <valgrind/memcheck.h>
#include <valgrind/valgrind.h>

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "bits.h"
#include "defer.h"
#include "logging.h"
#include "units.h"

#if defined(__clang__)
#if defined(__has_feature) && __has_feature(address_sanitizer)
#define __SANITIZE_ADDRESS__
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
extern "C" {
void __asan_poison_memory_region(void const volatile* addr, size_t size);
void __asan_unpoison_memory_region(void const volatile* addr, size_t size);
}
#define INSTRUMENT_FOR_ASAN
#define ASAN_POISON_MEMORY_REGION(addr, size) \
  __asan_poison_memory_region((addr), (size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) \
  __asan_unpoison_memory_region((addr), (size))
#else
#define ASAN_POISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#endif

#define ALLOCATOR_NO_ALIAS __attribute__((malloc))

namespace G {

inline static constexpr size_t kMaxAlign = alignof(std::max_align_t);

class Allocator {
 public:
  virtual ~Allocator() = default;

  virtual void* Alloc(size_t size, size_t align) = 0;

  virtual void Dealloc(void* p, size_t sz) = 0;

  virtual void* Realloc(void* p, size_t old_size, size_t new_size,
                        size_t align) = 0;

  template <typename T, typename... Args>
  T* New(Args... args) {
    T* ptr = reinterpret_cast<T*>(Alloc(sizeof(T), alignof(T)));
    ::new (ptr) T(std::forward<Args>(args)...);
    return ptr;
  }

  std::string_view StrDup(std::string_view s) {
    char* result = reinterpret_cast<char*>(Alloc(s.size(), 1));
    std::memcpy(result, s.data(), s.size());
    return std::string_view(result, s.size());
  }

  template <typename T, typename... Args>
  T* BraceInit(Args... args) {
    T* ptr = reinterpret_cast<T*>(Alloc(sizeof(T), alignof(T)));
    ::new (ptr) T{std::forward<Args>(args)...};
    return ptr;
  }

  template <typename T>
  T* NewArray(size_t n) {
    return reinterpret_cast<T*>(Alloc(n * sizeof(T), alignof(T)));
  }

  template <typename T>
  void DeallocArray(T* ptr, size_t n) {
    Dealloc(ptr, n * sizeof(T));
  }

  template <typename T>
  void Destroy(T* ptr) {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      ptr->~T();
    }
    Dealloc(ptr, sizeof(T));
  }
};

class SystemAllocator final : public Allocator {
 public:
  void* Alloc(size_t size, size_t /*align*/) override ALLOCATOR_NO_ALIAS {
    return std::malloc(size);
  }

  void Dealloc(void* p, size_t /*sz*/) override {
    if (p != nullptr) std::free(p);
  }

  void* Realloc(void* p, size_t /*old_size*/, size_t new_size,
                size_t /*align*/) override {
    return std::realloc(p, new_size);
  }

  static SystemAllocator* Instance() {
    static SystemAllocator allocator;
    return &allocator;
  }
};

class ArenaAllocator : public Allocator {
 public:
  ArenaAllocator(uint8_t* buffer, size_t size) {
    ASAN_POISON_MEMORY_REGION(buffer, size);
    auto start = reinterpret_cast<uintptr_t>(buffer);
    pos_ = Align(start, kMaxAlign);
    beginning_ = pos_;
    end_ = start + size;
  }

  ArenaAllocator(Allocator* a, size_t size)
      : ArenaAllocator(reinterpret_cast<uint8_t*>(a->Alloc(size, kMaxAlign)),
                       size) {
    allocator_ = a;
  }

  ~ArenaAllocator() override {
    if (allocator_ != nullptr) {
      auto* p = reinterpret_cast<void*>(beginning_);
      allocator_->Dealloc(p, end_ - beginning_);
    }
  }

  void* Alloc(size_t size, size_t align) override ALLOCATOR_NO_ALIAS {
    // Always align to std::max_align_t.
    size = Align(size, kMaxAlign);
    if (pos_ + size > end_) {
      return nullptr;
    }
    auto* result = reinterpret_cast<void*>(pos_);
    pos_ += size;
    ASAN_UNPOISON_MEMORY_REGION(result, size);
    return result;
  }

  void Dealloc(void* ptr, size_t size) override {
    if (ptr == nullptr) return;
    size = Align(size, kMaxAlign);
    auto p = reinterpret_cast<uintptr_t>(ptr);
    if (p + size == pos_) pos_ = p;
    ASAN_POISON_MEMORY_REGION(ptr, size);
  }

  void* Realloc(void* p, size_t old_size, size_t new_size,
                size_t align) override {
    auto* res = Alloc(new_size, align);
    std::memcpy(res, p, old_size);
    return res;
  }

  void Reset() {
    ASAN_POISON_MEMORY_REGION(reinterpret_cast<void*>(pos_), end_ - pos_);
    pos_ = beginning_;
  }

  size_t used_memory() const { return pos_ - beginning_; }
  size_t total_memory() const { return end_ - beginning_; }

 private:
  Allocator* allocator_ = nullptr;
  uintptr_t beginning_;
  uintptr_t pos_;
  uintptr_t end_;
};

template <size_t Size>
class StaticAllocator final : public ArenaAllocator {
 public:
  StaticAllocator() : ArenaAllocator(buffer_, Size) {}

 private:
  alignas(std::max_align_t) uint8_t buffer_[Size];
};

template <typename T>
class FreeList {
 public:
  FreeList(Allocator* a) : allocator_(a) {}

  T* Alloc() {
    if (free_ == nullptr) {
      auto* block = reinterpret_cast<Block*>(
          allocator_->Alloc(sizeof(Block), alignof(Block)));
      block->next = free_;
      free_ = block;
    }
    DCHECK(free_ != nullptr, "Out of memory");
    T* ptr = reinterpret_cast<T*>(free_);
    free_ = free_->next;
    return ptr;
  }

  template <typename... Args>
  T* New(Args... args) {
    T* ptr = Alloc();
    ::new (ptr) T(std::forward<Args>(args)...);
    return ptr;
  }

  void Dealloc(T* ptr) {
    auto* p = reinterpret_cast<Block*>(ptr);
    p->next = free_;
    free_ = p;
  }

 private:
  union Block {
    Block* next;
    T t;
  };

  Allocator* allocator_;
  Block* free_ = nullptr;
};

template <typename T>
class BlockAllocator {
 public:
  explicit BlockAllocator(Allocator* allocator, size_t blocks)
      : allocator_(allocator), num_blocks_(blocks) {
    blocks_ =
        static_cast<Block*>(allocator->Alloc(blocks * kBlockSize, alignof(T)));
    free_list_ = blocks_;
    auto block_at = [&](size_t i) {
      auto p = reinterpret_cast<uintptr_t>(blocks_);
      p += i * kBlockSize;
      return reinterpret_cast<Block*>(p);
    };
    for (size_t i = 0; i + 1 < blocks; ++i) {
      block_at(i)->next = block_at(i + 1);
    }
    block_at(blocks - 1)->next = nullptr;
    ASAN_POISON_MEMORY_REGION(blocks_, blocks * kBlockSize);
  }

  ~BlockAllocator() { allocator_->Dealloc(blocks_, num_blocks_ * kBlockSize); }

  T* AllocBlock() ALLOCATOR_NO_ALIAS {
    ASAN_UNPOISON_MEMORY_REGION(free_list_, kBlockSize);
    if (free_list_ == nullptr) return nullptr;
    Block* result = free_list_;
    free_list_ = free_list_->next;
    ::new (result) T();
    return reinterpret_cast<T*>(result);
  }

  void DeallocBlock(T* ptr) {
    auto* p = reinterpret_cast<Block*>(ptr);
    p->next = free_list_;
    free_list_ = p;
    ASAN_POISON_MEMORY_REGION(ptr, kBlockSize);
  }

 private:
  union Block {
    Block* next;
    T t;
  };

  inline static constexpr size_t kBlockSize = sizeof(Block);

  Allocator* allocator_;
  size_t num_blocks_;
  Block* free_list_;
  Block* blocks_;
};

}  // namespace G

#endif  // _GAME_ALLOCATORS_H
