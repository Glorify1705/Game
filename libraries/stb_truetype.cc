#include "stb_truetype.h"

#include <stdlib.h>

#include <cstddef>

void* StbttAlloc(void* /*ctx*/, int size, int /*align*/) {
  return malloc(size);
}

void StbttFree(void* /*ctx*/, void* ptr, int /*size*/) { free(ptr); }

StbttAllocator kDefaultAllocator = {StbttAlloc, StbttFree, nullptr};

StbttAllocator* kGlobalAllocator = &kDefaultAllocator;

StbttAllocator kInstalledAllocator;

void stbtt_SetAllocator(StbttAllocator allocator) {
  kInstalledAllocator = allocator;
  kGlobalAllocator = &kInstalledAllocator;
}

// The installed allocator's own context is used rather than the per-call
// userdata: stbtt call sites in the engine do not thread a context through.
#define STBTT_malloc(x, u, a) \
  kGlobalAllocator->Alloc(kGlobalAllocator->user, x, a)
#define STBTT_free(x, u, s) kGlobalAllocator->Free(kGlobalAllocator->user, x, s)

#include "stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
