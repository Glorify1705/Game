#pragma once
#ifndef _GAME_IMAGE_H
#define _GAME_IMAGE_H

#include "allocators.h"

namespace G {

#define QOI_SRGB 0
#define QOI_LINEAR 1

struct QoiDesc {
  unsigned int width;
  unsigned int height;
  unsigned char channels;
  unsigned char colorspace;
};

void *QoiEncode(const void *data, const QoiDesc *desc, int *out_len,
                Allocator *allocator);

void *QoiDecode(const void *data, int size, QoiDesc *desc, int channels,
                Allocator *allocator);

bool WritePixelsToImage(const char *filename, uint8_t *data, size_t width,
                        size_t height, Allocator *allocator);

}  // namespace G

#endif  // _GAME_IMAGE_H