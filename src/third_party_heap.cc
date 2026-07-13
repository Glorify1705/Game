#include "third_party_heap.h"

#include <mimalloc.h>

#include <cstdlib>

#include "allocators.h"
#include "logging.h"

namespace G {

namespace {

// 64 KiB, MI_ARENA_SLICE_SIZE: mimalloc arenas require slice alignment.
constexpr size_t kSliceSize = size_t{64} * 1024;

mi_arena_id_t arena_id;
bool initialized = false;

// Each thread allocates from its own heap inside the shared exclusive
// arena; mimalloc requires heaps to be used from their creating thread but
// supports frees from any thread.
thread_local mi_heap_t* thread_heap = nullptr;

mi_heap_t* ThreadHeap() {
  DCHECK(initialized, "InitThirdPartyHeap was not called");
  if (thread_heap == nullptr) {
    thread_heap = mi_heap_new_in_arena(arena_id);
    CHECK(thread_heap != nullptr, "Failed to create third-party heap");
  }
  return thread_heap;
}

}  // namespace

void InitThirdPartyHeap(size_t size) {
  CHECK(!initialized, "InitThirdPartyHeap called twice");
  // One-shot startup allocation, intentionally leaked: the mimalloc arena
  // registered below cannot be unregistered, so the block must outlive
  // process shutdown.
  auto* buffer = static_cast<uint8_t*>(malloc(size));
  CHECK(buffer != nullptr, "Failed to allocate third-party heap");
  const size_t addr = reinterpret_cast<size_t>(buffer);
  const size_t aligned = Align(addr, kSliceSize);
  size -= aligned - addr;
  size &= ~(kSliceSize - 1);
  CHECK(mi_manage_os_memory_ex(reinterpret_cast<void*>(aligned), size,
                               /*is_committed=*/true, /*is_pinned=*/false,
                               /*is_zero=*/false, /*numa_node=*/-1,
                               /*exclusive=*/true, &arena_id),
        "Failed to register third-party heap arena");
  initialized = true;
}

void* ThirdPartyMalloc(size_t size) {
  return mi_heap_malloc(ThreadHeap(), size);
}

void* ThirdPartyCalloc(size_t count, size_t size) {
  return mi_heap_calloc(ThreadHeap(), count, size);
}

void* ThirdPartyRealloc(void* ptr, size_t size) {
  return mi_heap_realloc(ThreadHeap(), ptr, size);
}

void ThirdPartyFree(void* ptr) { mi_free(ptr); }

}  // namespace G
