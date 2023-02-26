#ifndef _GAME_DEBUG_FONT_H
#define _GAME_DEBUG_FONT_H

#include <cstddef>

struct DebugFont {
  const unsigned char* data;
  size_t length;
};

DebugFont GetDebugFont();

#endif