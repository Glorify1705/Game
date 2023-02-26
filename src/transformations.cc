#include "transformations.h"

#include <cmath>

FMat4x4 Ortho(float l, float r, float t, float b) {
  auto mat = FMat4x4::Identity();
  mat.mut(0, 0) = 2.0 / (r - l);
  mat.mut(1, 1) = 2.0 / (t - b);
  mat.mut(0, 3) = -(r - l) / (r + l);
  mat.mut(1, 3) = -(t - b) / (t + b);
  return mat;
}

FMat4x4 TranslationXY(float tx, float ty) {
  auto mat = FMat4x4::Identity();
  mat.mut(0, 3) = tx;
  mat.mut(1, 3) = ty;
  return mat;
}

FMat4x4 RotationZ(float angle) {
  auto mat = FMat4x4::Identity();
  mat.mut(0, 0) = cosf(angle);
  mat.mut(0, 1) = -sinf(angle);
  mat.mut(1, 0) = sinf(angle);
  mat.mut(1, 1) = cosf(angle);
  return mat;
}

FMat4x4 ScaleXY(float sx, float sy) {
  auto mat = FMat4x4::Identity();
  mat.mut(0, 0) = sx;
  mat.mut(1, 1) = sy;
  return mat;
}