#include "camera.h"

#include <cmath>

#include "transformations.h"

namespace G {

void Camera::Update(float dt_ms, FVec2 viewport) {
  const float dt = dt_ms / 1000.0f;

  if (following) {
    FVec2 target = follow_target;

    if (deadzone_enabled) {
      float half_w = deadzone.x * viewport.x / zoom;
      float half_h = deadzone.y * viewport.y / zoom;
      if (target.x > position.x + half_w) {
        target.x = target.x - half_w;
      } else if (target.x < position.x - half_w) {
        target.x = target.x + half_w;
      } else {
        target.x = position.x;
      }
      if (target.y > position.y + half_h) {
        target.y = target.y - half_h;
      } else if (target.y < position.y - half_h) {
        target.y = target.y + half_h;
      } else {
        target.y = position.y;
      }
    }

    // Framerate-independent lerp: 1 - (1 - lerp)^(dt * 60).
    float factor_x = 1.0f - powf(1.0f - lerp.x, dt * 60.0f);
    float factor_y = 1.0f - powf(1.0f - lerp.y, dt * 60.0f);
    position.x += (target.x - position.x) * factor_x;
    position.y += (target.y - position.y) * factor_y;
  }

  if (bounds_enabled) {
    float half_w = (viewport.x / zoom) * 0.5f;
    float half_h = (viewport.y / zoom) * 0.5f;
    if (position.x - half_w < bounds_start.x) {
      position.x = bounds_start.x + half_w;
    }
    if (position.y - half_h < bounds_start.y) {
      position.y = bounds_start.y + half_h;
    }
    if (position.x + half_w > bounds_start.x + bounds_size.x) {
      position.x = bounds_start.x + bounds_size.x - half_w;
    }
    if (position.y + half_h > bounds_start.y + bounds_size.y) {
      position.y = bounds_start.y + bounds_size.y - half_h;
    }
  }

  if (shake_timer > 0.0f) {
    shake_timer -= dt;
    if (shake_timer <= 0.0f) {
      shake_timer = 0.0f;
      shake_offset = FVec2(0.0f, 0.0f);
    } else {
      float progress = shake_timer / shake_duration;
      float t = (shake_duration - shake_timer);
      float angle_x = t * shake_frequency * 6.2831853f;
      float angle_y = t * shake_frequency * 6.2831853f * 1.3f;
      shake_offset.x = sinf(angle_x) * shake_intensity * progress;
      shake_offset.y = cosf(angle_y) * shake_intensity * progress;
    }
  }
}

FMat4x4 Camera::GetViewMatrix(FVec2 viewport, FVec2 parallax) const {
  // translate(viewport/2) * scale(zoom) * rotate(rotation)
  //   * translate(-position * parallax) * translate(-shake_offset)
  FMat4x4 mat = TranslationXY(viewport.x * 0.5f, viewport.y * 0.5f);
  mat = mat * ScaleXY(zoom, zoom);
  if (rotation != 0.0f) {
    mat = mat * RotationZ(rotation);
  }
  mat = mat * TranslationXY(-position.x * parallax.x - shake_offset.x,
                            -position.y * parallax.y - shake_offset.y);
  return mat;
}

FVec2 Camera::ToWorld(FVec2 screen, FVec2 viewport) const {
  // Inverse: translate(position + shake) * rotate(-rotation)
  //   * scale(1/zoom) * translate(-viewport/2)
  float sx = screen.x - viewport.x * 0.5f;
  float sy = screen.y - viewport.y * 0.5f;
  sx /= zoom;
  sy /= zoom;
  if (rotation != 0.0f) {
    float c = cosf(-rotation);
    float s = sinf(-rotation);
    float rx = sx * c - sy * s;
    float ry = sx * s + sy * c;
    sx = rx;
    sy = ry;
  }
  sx += position.x + shake_offset.x;
  sy += position.y + shake_offset.y;
  return FVec2(sx, sy);
}

FVec2 Camera::ToScreen(FVec2 world, FVec2 viewport) const {
  float wx = world.x - position.x - shake_offset.x;
  float wy = world.y - position.y - shake_offset.y;
  if (rotation != 0.0f) {
    float c = cosf(rotation);
    float s = sinf(rotation);
    float rx = wx * c - wy * s;
    float ry = wx * s + wy * c;
    wx = rx;
    wy = ry;
  }
  wx *= zoom;
  wy *= zoom;
  wx += viewport.x * 0.5f;
  wy += viewport.y * 0.5f;
  return FVec2(wx, wy);
}

}  // namespace G
