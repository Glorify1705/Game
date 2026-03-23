#pragma once
#ifndef _GAME_IMAGE_H
#define _GAME_IMAGE_H

#include <cstdint>

#include "allocators.h"
#include "error.h"
#include "filesystem.h"

namespace G {

enum class QoiColorspace : uint8_t { kSrgb = 0, kLinear = 1 };

struct QoiDesc {
  uint64_t width;
  uint64_t height;
  uint8_t channels;
  QoiColorspace colorspace;
};

size_t MemoryNeededToEncode(const QoiDesc *desc);

void *QoiEncode(const void *data, const QoiDesc *desc, int *out_len,
                Allocator *allocator);

ErrorOr<void> QoiEncode(const void *data, const QoiDesc *desc, int *out_len,
                        void *buffer);

void *QoiDecode(const void *data, int size, QoiDesc *desc, int channels,
                Allocator *allocator);

ErrorOr<void> WritePixelsToImage(const char *filename, uint8_t *data,
                                 size_t width, size_t height,
                                 Filesystem *filesystem, Allocator *allocator);

}  // namespace G

#endif  // _GAME_IMAGE_H
