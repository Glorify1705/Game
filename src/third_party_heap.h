#pragma once
#ifndef _GAME_THIRD_PARTY_HEAP_H
#define _GAME_THIRD_PARTY_HEAP_H

#include <cstddef>

namespace G {

// A fixed-size, thread-safe, malloc-compatible heap for third-party
// libraries (SDL, PhysFS) whose allocation callbacks carry no size on free
// and are invoked from arbitrary threads. Backed by an exclusive mimalloc
// arena over a single upfront block: allocations never fall back to the OS,
// so exhausting the budget returns null instead of breaking memory
// determinism.
//
// Call once at startup before any use of the libraries it backs. The block
// is intentionally leaked: mimalloc has no arena-unregister API, so the
// memory must outlive process shutdown (same pattern as the engine arena).
void InitThirdPartyHeap(size_t size);

// malloc-compatible entry points backed by the heap. Safe to call from any
// thread; each calling thread lazily gets its own mimalloc heap inside the
// shared arena, and cross-thread frees are handled by mimalloc.
void* ThirdPartyMalloc(size_t size);
void* ThirdPartyCalloc(size_t count, size_t size);
void* ThirdPartyRealloc(void* ptr, size_t size);
void ThirdPartyFree(void* ptr);

}  // namespace G

#endif  // _GAME_THIRD_PARTY_HEAP_H
