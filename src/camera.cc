#include "camera.h"

#include <cmath>

#include "transformations.h"

namespace G {

void Camera::Update(float dt, FVec2 viewport) {
  if (following_) {
    FVec2 target = follow_target_;

    if (deadzone_enabled_) {
      float half_w = deadzone_.x * viewport.x / zoom_;
      float half_h = deadzone_.y * viewport.y / zoom_;
      if (target.x > position_.x + half_w) {
        target.x = target.x - half_w;
      } else if (target.x < position_.x - half_w) {
        target.x = target.x + half_w;
      } else {
        target.x = position_.x;
      }
      if (target.y > position_.y + half_h) {
        target.y = target.y - half_h;
      } else if (target.y < position_.y - half_h) {
        target.y = target.y + half_h;
      } else {
        target.y = position_.y;
      }
    }

    // Framerate-independent lerp: 1 - (1 - lerp)^(dt * 60).
    float factor_x = 1.0f - powf(1.0f - lerp_.x, dt * 60.0f);
    float factor_y = 1.0f - powf(1.0f - lerp_.y, dt * 60.0f);
    position_.x += (target.x - position_.x) * factor_x;
    position_.y += (target.y - position_.y) * factor_y;
  }

  if (bounds_enabled_) {
    float half_w = (viewport.x / zoom_) * 0.5f;
    float half_h = (viewport.y / zoom_) * 0.5f;
    if (position_.x - half_w < bounds_start_.x) {
      position_.x = bounds_start_.x + half_w;
    }
    if (position_.y - half_h < bounds_start_.y) {
      position_.y = bounds_start_.y + half_h;
    }
    if (position_.x + half_w > bounds_start_.x + bounds_size_.x) {
      position_.x = bounds_start_.x + bounds_size_.x - half_w;
    }
    if (position_.y + half_h > bounds_start_.y + bounds_size_.y) {
      position_.y = bounds_start_.y + bounds_size_.y - half_h;
    }
  }

  if (shake_timer_ > 0.0f) {
    shake_timer_ -= dt;
    if (shake_timer_ <= 0.0f) {
      shake_timer_ = 0.0f;
      shake_offset_ = FVec2(0.0f, 0.0f);
    } else {
      float progress = shake_timer_ / shake_duration_;
      float t = (shake_duration_ - shake_timer_);
      float angle_x = t * shake_frequency_ * 6.2831853f;
      float angle_y = t * shake_frequency_ * 6.2831853f * 1.3f;
      shake_offset_.x = sinf(angle_x) * shake_intensity_ * progress;
      shake_offset_.y = cosf(angle_y) * shake_intensity_ * progress;
    }
  }
}

FMat4x4 Camera::GetViewMatrix(FVec2 viewport, FVec2 parallax) const {
  // translate(viewport/2) * scale(zoom) * rotate(rotation)
  //   * translate(-position * parallax) * translate(-shake_offset)
  FMat4x4 mat = TranslationXY(viewport.x * 0.5f, viewport.y * 0.5f);
  mat = mat * ScaleXY(zoom_, zoom_);
  if (rotation_ != 0.0f) {
    mat = mat * RotationZ(rotation_);
  }
  mat = mat * TranslationXY(-position_.x * parallax.x - shake_offset_.x,
                            -position_.y * parallax.y - shake_offset_.y);
  return mat;
}

FVec2 Camera::ToWorld(FVec2 screen, FVec2 viewport) const {
  // Inverse: translate(position + shake) * rotate(-rotation)
  //   * scale(1/zoom) * translate(-viewport/2)
  float sx = screen.x - viewport.x * 0.5f;
  float sy = screen.y - viewport.y * 0.5f;
  sx /= zoom_;
  sy /= zoom_;
  if (rotation_ != 0.0f) {
    float c = cosf(-rotation_);
    float s = sinf(-rotation_);
    float rx = sx * c - sy * s;
    float ry = sx * s + sy * c;
    sx = rx;
    sy = ry;
  }
  sx += position_.x + shake_offset_.x;
  sy += position_.y + shake_offset_.y;
  return FVec2(sx, sy);
}

FVec2 Camera::ToScreen(FVec2 world, FVec2 viewport) const {
  float wx = world.x - position_.x - shake_offset_.x;
  float wy = world.y - position_.y - shake_offset_.y;
  if (rotation_ != 0.0f) {
    float c = cosf(rotation_);
    float s = sinf(rotation_);
    float rx = wx * c - wy * s;
    float ry = wx * s + wy * c;
    wx = rx;
    wy = ry;
  }
  wx *= zoom_;
  wy *= zoom_;
  wx += viewport.x * 0.5f;
  wy += viewport.y * 0.5f;
  return FVec2(wx, wy);
}

}  // namespace G
