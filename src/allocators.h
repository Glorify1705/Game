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
#include "logging.h"
#include "units.h"

namespace G {

inline static constexpr size_t Align(size_t n, size_t m) {
  return (n + m - 1) & ~(m - 1);
};

class Allocator {
 public:
  virtual ~Allocator() = default;

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

class ArenaAllocator : public Allocator {
 public:
  inline static constexpr size_t kMaxAlign = alignof(std::max_align_t);

  ArenaAllocator(uint8_t* buffer, size_t size) {
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
    if (allocator_ != nullptr)
      allocator_->Dealloc(allocator_, end_ - beginning_);
  }

  void* Alloc(size_t size, size_t align) override {
    // Always align to std::max_align_t.
    size = Align(size, kMaxAlign);
    if (pos_ + size > end_) {
      return nullptr;
    }
    auto* result = reinterpret_cast<void*>(pos_);
    pos_ += size;
    return result;
  }

  void Dealloc(void* ptr, size_t size) override {
    if (ptr == nullptr) return;
    size = Align(size, kMaxAlign);
    auto p = reinterpret_cast<uintptr_t>(ptr);
    if (p + size == pos_) pos_ = p;
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
  inline static constexpr size_t kBlockSize = sizeof(T);

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
  }

  ~BlockAllocator() { allocator_->Dealloc(blocks_, num_blocks_ * kBlockSize); }

  T* AllocBlock() {
    if (free_list_ == nullptr) return nullptr;
    Block* result = free_list_;
    free_list_ = free_list_->next;
    return reinterpret_cast<T*>(result);
  }

  void DeallocBlock(T* ptr) {
    auto* p = reinterpret_cast<Block*>(ptr);
    p->next = free_list_;
    free_list_ = p;
  }

 private:
  struct Block {
    Block* next;
  };

  Allocator* allocator_;
  size_t num_blocks_;
  Block* free_list_;
  Block* blocks_;
};

#if 0
// The design is based on https://www.microsoft.com/en-us/research/uploads/prod/2019/06/mimalloc-tr-v1.pdf
class ShardedFreeListAllocator : public Allocator {
 public:
  ShardedFreeListAllocator(void* buffer, size_t size) : buffer_(reinterpret_cast<uintptr_t>(buffer)), size_(size) {
    std::fill(pages_.begin(), pages_.end(), nullptr);
    full_pages_ = nullptr;
    segments_ = nullptr;
  }

  // TODO: Write. Would break the block into segments.
  // void Donate(void* p, size_t size, void* ud, void(*free)(void*, size_t, void*));

  void* Alloc(size_t size, size_t align) override {
    // All pages from this allocator are aligned at at least 128 byte boundary (or more).
    // So we should not need alignment.
    DCHECK(align < 128);
    size_t bucket = GetPageBucket(size);
    auto* page = pages_[bucket];
    if (page == nullptr) return SlowAlloc(page, size);
    Block* block = page->free;
    if (block == nullptr) return SlowAlloc(page, size);
    page->free = block->next;
    return block;
  }

  void Dealloc(void* ptr, size_t sz) override {
    const size_t bucket = GetPageBucket(sz);
    uintptr_t address = Address(ptr);
    auto* segment = Ptr<Segment>(address & kSegmentSize);
    if (segment == nullptr) return;
    uintptr_t segment_address = Address(segment);
    Page* page = &segment->pages[(address - segment_address) >> segment->page_shift];
    auto* block = Ptr<Block>(address);
    block->next = page->free;
    page->free = block;
    page->used_blocks--;
    if (page->used_blocks == 0) PageFree(page, bucket);
  }

  void* Realloc(void* p, size_t old_size, size_t new_size,
                size_t align) override {
    size_t alloc = AllocSize(old_size);
    if (new_size <= alloc) {
      return p;
    }
    auto* result = Alloc(new_size, align);
    std::memcpy(result, p, old_size);
    Dealloc(p, old_size);
    return result;
  }

 private:

  inline static constexpr size_t kSmallAlloc = Kilobytes(4);
  inline static constexpr size_t kMediumAlloc = Megabytes(2);

  inline static constexpr size_t kSmallBucketSize = 128;
  inline static constexpr size_t kMediumBucketSize = Kilobytes(4);

  inline static constexpr size_t kSmallPageSize = Kilobytes(64);
  inline static constexpr size_t kMediumPageSize = Kilobytes(512);
  inline static constexpr size_t kHugePage = Megabytes(4);

  inline static constexpr size_t kSegmentSize = Megabytes(4);

  template<typename T>
  constexpr static uintptr_t Address(T* p) { return reinterpret_cast<uintptr_t>(p); }

  template<typename T>
  constexpr static T* Ptr(uintptr_t p) { return reinterpret_cast<T*>(p); }

  static constexpr size_t AllocSize(size_t a) { return NextPow2(a); }

  // TODO: Consider that the segment's first page needs to have less space for the metadata.

  enum class PageClass { kSmall, kMedium, kHuge };

  struct Block {
    Block* next;
  };

  struct Page {
    size_t num_blocks;
    size_t block_size;
    size_t used_blocks = 0;
    Page* next;
    Page* prev;
    Block* free = nullptr;
    uint8_t* page_area;
  };

  // Segments are 4 Mb aligned.
  struct Segment {
    size_t page_size;
    size_t num_pages;
    size_t used_pages;
    size_t page_shift;
    PageClass page_class;
    // Segment queue.
    Segment* prev;
    Segment* next;
    // Page metadata.
    Page pages[1];
    // Page area is after the metadata.

    // Return the start of the page area.
    uint8_t* PageArea() const {
      uintptr_t p = Address(this) + offsetof(Segment, pages) + num_pages * sizeof(Page);
      // Ensure alignment so we never need to align a page.
      return Ptr<uint8_t>(Align(p, sizeof(std::max_align_t)));
    }

    size_t HeaderSize() const {
      return sizeof(Page) * (num_pages - 1) + sizeof(Segment);
    }

    Page* AllocPage(size_t block_size) {
      if (used_pages == num_pages) return nullptr;
      auto* page = &pages[used_pages];
      page->block_size = block_size;
      page->num_blocks = page_size / num_pages;
      size_t i = 0;
      Block* block = nullptr;
      for (uintptr_t p = Address(page->page_area); i < page->num_blocks; p += block_size, i++) {
        auto* curr = Ptr<Block>(p);
        curr->next = nullptr;
        if (block != nullptr) block->next = curr;
        block = curr;
      }
      used_pages++;
      return page;
    }
  };

  // Allocate a segment with one page of the required size.
  // The result is aligned to 4 megabytes.
  Segment* AllocSegment(size_t page_size) {
    if (page_size >= kMediumPageSize) {
      // Allocate a huge segment.
      page_size = Align(page_size + sizeof(Segment), Megabytes(4));
      CHECK((buffer_ + page_size) < size_, "OOM");
      auto* segment = reinterpret_cast<Segment*>(buffer_);
      segment->page_size = page_size;
      segment->page_shift = 22;
      segment->num_pages = 1;
      segment->next = nullptr;
      segment->prev = nullptr;
      buffer_ += page_size; 
      return segment;
    }
    if (page_size >= kSmallPageSize) {
      CHECK((buffer_ + kSegmentSize) < size_, "OOM");
      // Allocate a segment with medium pages.
      const size_t segment_size = kSegmentSize;
      const size_t page_size = kMediumPageSize;
      auto* segment = reinterpret_cast<Segment*>(buffer_);
      segment->page_size = Megabytes(4);
      segment->num_pages = (segment_size / page_size);
      segment->page_shift = 22;
      segment->next = nullptr;
      segment->prev = nullptr;
      buffer_ += segment_size;
      return segment;
    }
    // Allocate a segment with small pages.
    const size_t segment_size = kSegmentSize;
    CHECK((buffer_ + kSegmentSize) < size_, "OOM");
    auto* segment = reinterpret_cast<Segment*>(buffer_);
    segment->page_size = Kilobytes(64);
    segment->num_pages = (segment_size / page_size);
    segment->page_shift = 16;  // 64 k.
    segment->next = nullptr;
    segment->prev = nullptr;
    buffer_ += segment_size;
    return segment;
  }

  size_t GetPageBucket(size_t alloc) {
    if (alloc < kSmallAlloc) return Align(alloc, kSmallBucketSize);
    alloc -= kSmallAlloc;
    if (alloc < kMediumAlloc) return 32 + Align(alloc, kMediumBucketSize);
    // Huge allocation.
    return pages_.size() - 1;
  }

  size_t GetPageSize(size_t alloc) {
    if (alloc < kSmallAlloc) return kSmallPageSize;
    if (alloc < kMediumAlloc) return kMediumPageSize;
    return kHugePageSize;
  }

  void PageFree(Page* page, size_t bucket) {
    size_t page_bucket = GetPageBucket(page->block_size);
    if (pages_[page_bucket] == nullptr) {
      pages_[page_bucket] = page;
    } else {
      // Remove from the list (whether the full pages list or the regular list).
      // And add it to the front.
      if (page->prev != nullptr) page->prev->next = page->next;
      if (page->next != nullptr) page->next->prev = page->prev;
      // Move free page to the front.
      auto* tmp = pages_[page_bucket];
      page->next = tmp;
      pages_[page_bucket] = page;
    }
  }

  void* SlowAlloc(Page* page, size_t alloc) {
    const size_t page_bucket = GetPageBucket(alloc);
    // Try to find a free page in the list.
    for (auto* curr = page; curr != nullptr; curr = curr->next) {
      if (curr->free != nullptr) {
        page = curr;
        break;
      } 
      // Take the entry out of the list and move it to full list
      // so we ignore it for following allocations.
      if (curr->prev) curr->prev->next = curr->next; 
      if (curr->next) curr->next->prev = curr->prev;
      curr->prev = nullptr;
      curr->next = nullptr;
      if (full_pages_ != nullptr) {
        full_pages_->prev = curr;
        curr->next = full_pages_;
      }
      full_pages_ = curr;
    }
    if (page == nullptr || page->free == nullptr) {
      Segment* segment = nullptr;
      for (auto* segment = segments_; segment != nullptr; segment = segment->next) {

      }
      if (segment == nullptr) segment = AllocSegment();
      segment->next = segments_;
      segments_ = segment;
      page = segment->AllocPage();
    }
    auto* tmp = pages_[page_bucket];
    if (page->prev != nullptr) page->prev->next = page->next;
    if (page->next != nullptr) page->next->prev = page->prev;
    if (tmp != nullptr) tmp->prev = page;
    page->prev = nullptr;
    page->next = tmp;
    pages_[page_bucket] = page;
    page->used_blocks++;
    auto* b = page->free;
    page->free = b->next;
    return b;
  }

  // Small allocations: 0 to 4k in 128 byte jumps, so 32. Page is 64k. 
  // Medium allocations: 4k to 2M in 4k jumps, 1024. Page is 512k.
  // Last one is for huge (> 4M) pages.
  std::array<Page*, 32 + 512 + 1> pages_;
  // Linked list of full pages.
  Page* full_pages_;
  Segment* segments_;
  uintptr_t buffer_;
  size_t size_;
};
#endif

}  // namespace G

#endif
