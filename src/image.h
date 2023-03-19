#pragma once
#ifndef _GAME_IMAGE_H
#define _GAME_IMAGE_H

#include "libraries/qoi.h"
#include "libraries/stb_image.h"

namespace G {

void SetImageAlloc(void* (*alloc)(size_t));

void SetImageFree(void (*free)(void*));

void SetImageRealloc(void* (*realloc)(void*, size_t, size_t));

}  // namespace G

#endif  // _GAME_IMAGE_H