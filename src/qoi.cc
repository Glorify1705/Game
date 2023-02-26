#include <cstddef>
#include <cstdlib>

static void* (*g_Alloc)(size_t size) = malloc;
static void (*g_Free)(void* ptr) = free;

void* QoiAlloc(size_t sz) { return g_Alloc(sz); }

void QoiFree(void* ptr) { return g_Free(ptr); }

#define QOI_MALLOC(sz) QoiAlloc(sz)
#define QOI_FREE(p) QoiFree(p)

#define QOI_IMPLEMENTATION
#include "qoi.h"

void SetQoiAlloc(void* (*alloc)(size_t size), void (*dealloc)(void* ptr)) {
  g_Alloc = alloc;
  g_Free = dealloc;
}