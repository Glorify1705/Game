#pragma once
#ifndef _GAME_TRANSFORMATIONS_H
#define _GAME_TRANSFORMATIONS_H

#include "mat.h"
#include "vec.h"

FMat4x4 Ortho(float l, float r, float t, float b);

FMat4x4 TranslationXY(float tx, float ty);

FMat4x4 RotationZ(float angle);

FMat4x4 ScaleXY(float sx, float sy);

#endif  // _GAME_TRANSFORMATIONS_H