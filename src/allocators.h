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

// The design is based on
// https://www.microsoft.com/en-us/research/uploads/prod/2019/06/mimalloc-tr-v1.pdf
class ShardedFreeListAllocator : public Allocator {
 public:
  ShardedFreeListAllocator(void* buffer, size_t size)
      : buffer_(reinterpret_cast<uintptr_t>(buffer)), end_(buffer_ + size) {
    buffer_ = Align(buffer_, kSegmentSize);
    beg_ = buffer_;
    small_pages_ = nullptr;
    medium_pages_ = nullptr;
    full_pages_ = nullptr;
    page_idx_ = segment_idx_ = 0;
    std::fill(free_.begin(), free_.end(), nullptr);
  }

  void* Alloc(size_t size, size_t align) override ALLOCATOR_NO_ALIAS {
    // All pages from this allocator are aligned at at least 32 byte boundary
    // (or more). So we should not need alignment.
    DCHECK(align <= 32);
    size = AllocSize(size);
    size_t bucket = GetPageBucket(size);
    auto* page = free_[bucket];
    if (page == nullptr || page->next == nullptr) {
      return SlowAlloc(page, size);
    }
    return page->Alloc();
  }

  void Dealloc(void* ptr, size_t size) override {
    if (ptr == nullptr) return;
    size = AllocSize(size);
    const auto ptr_address = reinterpret_cast<uintptr_t>(ptr);
    auto* segment = &segments_[(ptr_address - beg_) >> kSegmentShift];
    Page* page = &segment->pages[(ptr_address - segment->base_address) >>
                                 segment->page_shift];
    auto* block = reinterpret_cast<Block*>(ptr_address);
    block->next = page->free;
    page->free = block;
    page->used_blocks--;
    if (page->used_blocks == 0) {
      // The page is fully freed, add it to the free list pages for that page
      // kind.
      UnlinkAndAddToList(page, GetPageList(page->block_size));
    } else {
      // Add it to the page list of the bucket to the front, since it has space.
      UnlinkAndAddToList(page, &free_[GetPageBucket(size)]);
    }
  }

  void* Realloc(void* p, size_t old_size, size_t new_size,
                size_t align) override {
    size_t alloc = AllocSize(old_size);
    if (new_size <= alloc) return p;
    auto* result = Alloc(new_size, align);
    std::memcpy(result, p, old_size);
    Dealloc(p, old_size);
    return result;
  }

 private:
  inline static constexpr size_t kSmallAlloc = Kilobytes(4);
  inline static constexpr size_t kMediumAlloc = Megabytes(2);

  inline static constexpr size_t kSmallBucketSize = 32;
  inline static constexpr size_t kMediumBucketSize = Kilobytes(4);

  inline static constexpr size_t kSmallPageSize = Kilobytes(64);
  inline static constexpr size_t kMediumPageSize = Kilobytes(512);
  inline static constexpr size_t kHugePageSize = Megabytes(4);

  inline static constexpr size_t kSegmentShift = 22;
  inline static constexpr size_t kSegmentSize = Megabytes(4);
  static_assert((1 << kSegmentShift) == kSegmentSize);

  inline static constexpr size_t kSmallPageShift = 16;
  static_assert((1 << kSmallPageShift) == kSmallPageSize);

  static constexpr size_t AllocSize(size_t a) {
    if (a < kSmallAlloc) return Align(a, kSmallBucketSize);
    if (a < kMediumAlloc) return Align(a, kMediumBucketSize);
    return Align(a, kSegmentSize);
  }

  struct Block {
    Block* next;
  };

  struct Page {
    uint16_t segment_idx;
    uint8_t page_offset;
    uint16_t num_blocks;
    uint32_t block_size;
    uint16_t used_blocks = 0;
    Page* next = nullptr;
    Page* prev = nullptr;
    Block* free = nullptr;

    void* Alloc() {
      auto* b = free;
      if (b == nullptr) return nullptr;
      used_blocks++;
      free = b->next;
      return b;
    }
  };

  // Segments are 4 Mb aligned.
  struct Segment {
    uintptr_t base_address;
    size_t page_shift;
    size_t page_size;
    // Page metadata.
    Page* pages;
  };

  size_t GetPageBucket(size_t alloc) {
    if (alloc <= kSmallAlloc) {
      return (Align(alloc, kSmallBucketSize) / kSmallBucketSize - 1);
    }
    alloc -= kSmallPageSize;
    if (alloc <= kMediumAlloc) {
      return 32 + Align(alloc, kMediumBucketSize) / kMediumBucketSize;
    }
    // Huge allocation.
    return free_.size() - 1;
  }

  size_t GetPageSize(size_t alloc) {
    if (alloc <= kSmallAlloc) return kSmallPageSize;
    if (alloc <= kMediumAlloc) return kMediumPageSize;
    return kHugePageSize;
  }

  Page** GetPageList(size_t block_size) {
    if (block_size <= kSmallAlloc) return &small_pages_;
    if (block_size <= kMediumAlloc) return &medium_pages_;
    return &free_[free_.size() - 1];
  }

  void UnlinkAndAddToList(Page* page, Page** list) {
    if (page->next != nullptr) page->next->prev = page->prev;
    if (page->prev != nullptr) page->prev->next = page->next;
    page->prev = page->next = nullptr;
    if (*list != nullptr) {
      (*list)->prev = page;
      page->next = (*list);
    }
    *list = page;
  }

  // Allocate a segment, and return the first page.
  Page* AllocSegment(size_t page_size) {
    auto* segment = &segments_[segment_idx_];
    segment->base_address = buffer_;
    size_t num_pages = 0;
    Page** list;
    if (page_size > kMediumPageSize) {
      DCHECK((buffer_ + page_size) <= end_);
      // Allocate a huge segment.
      page_size = Align(page_size, kSegmentSize);
      num_pages = 1;
      list = &free_.back();
      buffer_ += page_size;
    } else if (page_size > kSmallPageSize) {
      DCHECK((buffer_ + kSegmentSize) <= end_);
      // Allocate a medium segment.
      segment->page_size = kMediumPageSize;
      segment->page_shift = kSegmentShift;  // 512k
      num_pages = kSegmentSize / kMediumPageSize;
      list = &medium_pages_;
      buffer_ += kSegmentSize;
    } else {
      // Allocate a small segment.
      DCHECK((buffer_ + kSegmentSize) <= end_);
      segment->page_size = kSmallPageSize;
      segment->page_shift = kSmallPageShift;  // 64 k.
      num_pages = kSegmentSize / kSmallPageSize;
      list = &small_pages_;
      buffer_ += kSegmentSize;
    }
    // Allocate all the pages in the segment.
    auto* p = &pages_[page_idx_++];
    std::memset(p, 0, sizeof(Page));
    p->segment_idx = segment_idx_;
    segment->pages = p;
    for (size_t i = 1; i < num_pages; ++i) {
      auto* page = &pages_[page_idx_++];
      *page = *p;
      page->page_offset = i;
      p->next = page;
      page->prev = p;
      p = page;
    }
    p->next = nullptr;
    *list = segment->pages;
    segment_idx_++;
    return segment->pages;
  }

  void* SlowAlloc(Page* page, size_t block_size) {
    // Try to find a free page in the list.
    const size_t page_bucket = GetPageBucket(block_size);
    for (auto* curr = free_[page_bucket]; curr != nullptr; curr = curr->next) {
      if (curr->free == nullptr) {
        // Take the entry out of the list and move it to full list
        // so we ignore it for following allocations.
        UnlinkAndAddToList(curr, &full_pages_);
        continue;
      }
      page = curr;
      // Add it to the front of the list for next time.
      auto* block = page->Alloc();
      if (page->used_blocks < page->num_blocks) {
        UnlinkAndAddToList(page, &free_[page_bucket]);
      }
      return block;
    }
    // There are no free pages.
    // See if we have a page in the appropiate page list.
    auto** page_list = GetPageList(block_size);
    if (*page_list != nullptr) page = *page_list;
    const size_t page_size = GetPageSize(block_size);
    // Didnt find any free pages in the list either.
    // Add a new segment with free pages.
    if (page == nullptr) page = AllocSegment(page_size);
    auto* segment = &segments_[page->segment_idx];
    // We have a page. Put it in front of the free list we needed.
    page->block_size = block_size;
    auto base = reinterpret_cast<uintptr_t>(
        segment->base_address + segment->page_size * page->page_offset);
    // Add all the entries in the list of blocks to the free list.
    page->num_blocks = segment->page_size / page->block_size;
    auto block_at = [&](size_t i) {
      return reinterpret_cast<Block*>(base + i * block_size);
    };
    for (size_t i = 0; i + 1 < page->num_blocks; ++i) {
      block_at(i)->next = block_at(i + 1);
    }
    block_at(page->num_blocks - 1)->next = nullptr;
    page->free = block_at(0);
    UnlinkAndAddToList(page, &free_[page_bucket]);
    return page->Alloc();
  }

  // Small allocations: 0 to 4k in 32 byte jumps, so 128. Page is 64k.
  // Medium allocations: 4k to 4M in 4k jumps, 1024. Page is 512k.
  // Last one is for huge (> 4M) pages. Segments are always 4M aligned.
  std::array<Page*, 128 + 1024 + 1> free_;
  // Linked list of full pages.
  Page* small_pages_;
  Page* medium_pages_;
  Page* full_pages_;
  // 4M segments --> max 4G memory.
  Segment segments_[1024];
  // 64 pages per segment, 1024 segments means 64k pages max.
  Page pages_[65536];
  // Dummy page so we can check allocs.
  Page dummy_;
  size_t page_idx_, segment_idx_;
  uintptr_t beg_;
  uintptr_t buffer_;
  uintptr_t end_;
};

}  // namespace G

#endif
