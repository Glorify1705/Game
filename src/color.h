#pragma once
#ifndef _GAME_COLOR_H
#define _GAME_COLOR_H

#include <cstdint>
#include <string_view>

#include "vec.h"

namespace G {

struct Color {
  uint8_t r, g, b, a;

  FVec4 ToFloat() const {
    return FVec(r / 255.0, g / 255.0, b / 255.0, a / 255.0);
  }

  static Color White() { return {255, 255, 255, 255}; }
  static Color Zero() { return {0, 0, 0, 0}; }
};

Color ColorFromTable(std::string_view color);

}  // namespace G

#endif  // _GAME_COLOR_H