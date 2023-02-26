#pragma once
#ifndef _GAME_MATH_H
#define _GAME_MATH_H

#include "vec.h"
inline float Sign(float val) { return (0 < val) - (val < 0); }

struct Rectangle {
  FVec2 top_left, bottom_right;
  float angle;
};

bool CheckOverlap(const Rectangle& a, const Rectangle& b);

#endif  // _GAME_MATH_H