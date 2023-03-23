#include <cstddef>
#include <cstdlib>

#include "logging.h"

void* BadRealloc(void* ptr, size_t /*old_size*/, size_t new_size) {
  return std::realloc(ptr, new_size);
}

static void* (*g_Alloc)(size_t) = std::malloc;
static void (*g_Free)(void*) = std::free;
static void* (*g_Realloc)(void*, size_t, size_t) = BadRealloc;

static void* ImageAlloc(size_t size) { return g_Alloc(size); }

static void ImageFree(void* ptr) { return g_Free(ptr); }

static void* ImageRealloc(void* ptr, size_t old_size, size_t new_size) {
  return g_Realloc(ptr, old_size, new_size);
}

#define QOI_MALLOC(sz) ImageAlloc(sz)
#define QOI_FREE(p) ImageFree(p)

#define QOI_IMPLEMENTATION
#include "libraries/qoi.h"

#define STBI_MALLOC(sz) ImageAlloc(sz)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) ImageRealloc(p, oldsz, newsz)
#define STBI_FREE(p) ImageFree(p)
#define STB_IMAGE_IMPLEMENTATION
#include "libraries/stb_image.h"

namespace G {

void SetImageAlloc(void* (*alloc)(size_t)) { g_Alloc = alloc; }

void SetImageFree(void (*free)(void*)) { g_Free = free; }

void SetImageRealloc(void* (*realloc)(void*, size_t, size_t)) {
  g_Realloc = realloc;
}

bool WritePixelsToImage(const char* filename, uint8_t* data, size_t width,
                        size_t height) {
  CHECK(HasSuffix(filename, ".qoi"));
  qoi_desc desc;
  desc.width = width;
  desc.height = height;
  desc.channels = 4;
  return qoi_write(filename, data, &desc);
}

}  // namespace G