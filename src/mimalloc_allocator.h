#pragma once
#ifndef _GAME_MIMALLOC_ALLOCATOR_H
#define _GAME_MIMALLOC_ALLOCATOR_H

#include <mimalloc.h>

#include "allocators.h"

namespace G {

class MimallocAllocator final : public Allocator {
 public:
  static constexpr size_t kSliceSize = 64 * 1024;  // MI_ARENA_SLICE_SIZE

  MimallocAllocator(void* buffer, size_t size) {
    // mimalloc requires 64 KiB slice alignment.
    size_t addr = reinterpret_cast<size_t>(buffer);
    size_t aligned = Align(addr, kSliceSize);
    size -= (aligned - addr);
    size &= ~(kSliceSize - 1);
    mi_manage_os_memory_ex(reinterpret_cast<void*>(aligned), size,
                           /*is_committed=*/true, /*is_pinned=*/false,
                           /*is_zero=*/false, /*numa_node=*/-1,
                           /*exclusive=*/true, &arena_id_);
    heap_ = mi_heap_new_in_arena(arena_id_);
  }

  ~MimallocAllocator() override { mi_heap_destroy(heap_); }

  void* Alloc(size_t size, size_t align) override {
    return mi_heap_malloc_aligned(heap_, size, align);
  }

  void Dealloc(void* p, size_t) override { mi_free(p); }

  void* Realloc(void* p, size_t, size_t new_size, size_t align) override {
    return mi_heap_realloc_aligned(heap_, p, new_size, align);
  }

 private:
  mi_arena_id_t arena_id_;
  mi_heap_t* heap_;
};

}  // namespace G

#endif  // _GAME_MIMALLOC_ALLOCATOR_H
