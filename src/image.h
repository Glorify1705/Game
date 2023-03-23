#pragma once
#ifndef _GAME_IMAGE_H
#define _GAME_IMAGE_H

#include "libraries/qoi.h"
#include "libraries/stb_image.h"

namespace G {

void SetImageAlloc(void* (*alloc)(size_t));

void SetImageFree(void (*free)(void*));

void SetImageRealloc(void* (*realloc)(void*, size_t, size_t));

bool WritePixelsToImage(const char* filename, uint8_t* data, size_t width,
                        size_t height);

}  // namespace G

#endif  // _GAME_IMAGE_H