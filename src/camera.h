#pragma once
#ifndef _GAME_CAMERA_H
#define _GAME_CAMERA_H

#include "mat.h"
#include "vec.h"

namespace G {

struct Camera {
  void Update(float dt, FVec2 viewport);

  FMat4x4 GetViewMatrix(FVec2 viewport, FVec2 parallax) const;

  FVec2 ToWorld(FVec2 screen, FVec2 viewport) const;
  FVec2 ToScreen(FVec2 world, FVec2 viewport) const;

  // Position.
  FVec2 position;

  // Zoom and rotation.
  float zoom = 1.0f;
  float rotation = 0.0f;

  // Follow.
  FVec2 follow_target;
  bool following = false;
  FVec2 lerp = FVec2(1.0f, 1.0f);

  // Deadzone (fraction of viewport, 0-1).
  FVec2 deadzone;
  bool deadzone_enabled = false;

  // Bounds.
  FVec2 bounds_start;
  FVec2 bounds_size;
  bool bounds_enabled = false;

  // Shake.
  float shake_intensity = 0.0f;
  float shake_duration = 0.0f;
  float shake_timer = 0.0f;
  float shake_frequency = 8.0f;
  FVec2 shake_offset;
};

}  // namespace G

#endif  // _GAME_CAMERA_H
