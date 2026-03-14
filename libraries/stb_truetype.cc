#include "stb_truetype.h"

#include <stdlib.h>

void* StbttAlloc(void* /*ctx*/, int size, int /*align*/) {
  return malloc(size);
}

void StbttFree(void* /*ctx*/, void* ptr, int /*size*/) { free(ptr); }

StbttAllocator kDefaultAllocator = {
    .Alloc = StbttAlloc,
    .Free = StbttFree,
};

StbttAllocator* kGlobalAllocator = &kDefaultAllocator;

#define STBTT_malloc(x, u, a) kGlobalAllocator->Alloc(u, x, a)
#define STBTT_free(x, u, s) kGlobalAllocator->Free(u, x, s)

#include "stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"