#include "stb_truetype.h"

#include <stdlib.h>

#include <cstddef>

void* StbttAlloc(void* /*ctx*/, int size, int /*align*/) {
  return malloc(size);
}

void StbttFree(void* /*ctx*/, void* ptr, int /*size*/) { free(ptr); }

const StbttAllocator kDefaultAllocator = {StbttAlloc, StbttFree, nullptr};

const StbttAllocator* g_allocator = &kDefaultAllocator;

StbttAllocator g_installed_allocator;

void stbtt_SetAllocator(StbttAllocator allocator) {
  g_installed_allocator = allocator;
  g_allocator = &g_installed_allocator;
}

// The installed allocator's own context is used rather than the per-call
// userdata: stbtt call sites in the engine do not thread a context through.
#define STBTT_malloc(x, u, a) g_allocator->Alloc(g_allocator->user, x, a)
#define STBTT_free(x, u, s) g_allocator->Free(g_allocator->user, x, s)

#include "stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
