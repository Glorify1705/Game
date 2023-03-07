#ifndef _GAME_DEBUG_FONT_H
#define _GAME_DEBUG_FONT_H

#include <cstddef>

namespace G {

struct DebugFont {
  const unsigned char* data;
  size_t length;
};

DebugFont GetDebugFont();

}  // namespace G

#endif