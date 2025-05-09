#pragma once
#ifndef _GAME_IMAGE_H
#define _GAME_IMAGE_H

#include <cstdint>

#include "allocators.h"
#include "filesystem.h"
#include "stringlib.h"

namespace G {

#define QOI_SRGB 0
#define QOI_LINEAR 1

struct QoiDesc {
  uint64_t width;
  uint64_t height;
  uint8_t channels;
  uint8_t colorspace;
};

size_t MemoryNeededToEncode(const QoiDesc *desc);

void *QoiEncode(const void *data, const QoiDesc *desc, int *out_len,
                Allocator *allocator);

void QoiEncode(const void *data, const QoiDesc *desc, int *out_len,
               void *buffer, bool *error);

void *QoiDecode(const void *data, int size, QoiDesc *desc, int channels,
                Allocator *allocator);

bool WritePixelsToImage(const char *filename, uint8_t *data, size_t width,
                        size_t height, Filesystem *filesystem,
                        StringBuffer *err, Allocator *allocator);

}  // namespace G

#endif  // _GAME_IMAGE_H
