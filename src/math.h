#pragma once
#ifndef _GAME_MATH_H
#define _GAME_MATH_H

#include "array.h"
#include "vec.h"
inline float Sign(float val) { return (0 < val) - (val < 0); }

struct Rectangle {
  std::array<FVec2, 4> v;
};

bool CheckOverlap(const Rectangle& a, const Rectangle& b);

#endif  // _GAME_MATH_H