#pragma once
#ifndef _GAME_CAMERA_H
#define _GAME_CAMERA_H

#include "mat.h"
#include "vec.h"

namespace G {

// 2D camera with smooth follow, deadzone, bounds clamping, shake, and parallax.
class Camera {
 public:
  // Advances follow-lerp, bounds clamping, and shake decay.
  void Update(float dt, FVec2 viewport);

  // Returns the view matrix for rendering with optional parallax scaling.
  FMat4x4 GetViewMatrix(FVec2 viewport, FVec2 parallax) const;

  // Converts screen coordinates to world coordinates.
  FVec2 ToWorld(FVec2 screen, FVec2 viewport) const;

  // Converts world coordinates to screen coordinates.
  FVec2 ToScreen(FVec2 world, FVec2 viewport) const;

  // Sets the camera position in world coordinates.
  void SetPosition(float x, float y) { position_ = FVec2(x, y); }

  // Returns the camera position in world coordinates.
  FVec2 GetPosition() const { return position_; }

  // Moves the camera by a delta in world coordinates.
  void Move(float dx, float dy) {
    position_.x += dx;
    position_.y += dy;
  }

  // Sets the zoom level. Values > 1 zoom in, < 1 zoom out.
  void SetZoom(float z) { zoom_ = z; }

  // Returns the current zoom level.
  float GetZoom() const { return zoom_; }

  // Sets the camera rotation in radians.
  void SetRotation(float angle) { rotation_ = angle; }

  // Returns the current camera rotation in radians.
  float GetRotation() const { return rotation_; }

  // Sets the position the camera should follow each frame.
  void Follow(float x, float y) {
    follow_target_ = FVec2(x, y);
    following_ = true;
  }

  // Stops following the target.
  void Unfollow() { following_ = false; }

  // Sets the smoothing factor for following (0 = no movement, 1 = instant).
  void SetLerp(float lx, float ly) { lerp_ = FVec2(lx, ly); }

  // Sets a deadzone as a fraction of the viewport (0-1).
  void SetDeadzone(float half_w, float half_h) {
    deadzone_ = FVec2(half_w, half_h);
    deadzone_enabled_ = true;
  }

  // Removes the deadzone.
  void ClearDeadzone() { deadzone_enabled_ = false; }

  // Sets world bounds the camera viewport cannot exceed.
  void SetBounds(float x, float y, float w, float h) {
    bounds_start_ = FVec2(x, y);
    bounds_size_ = FVec2(w, h);
    bounds_enabled_ = true;
  }

  // Removes world bounds.
  void ClearBounds() { bounds_enabled_ = false; }

  // Returns the follow target position.
  FVec2 GetFollowTarget() const { return follow_target_; }

  // Returns true if the camera is following a target.
  bool IsFollowing() const { return following_; }

  // Returns the lerp smoothing factors.
  FVec2 GetLerp() const { return lerp_; }

  // Returns the deadzone half-size (fraction of viewport).
  FVec2 GetDeadzone() const { return deadzone_; }

  // Returns true if a deadzone is active.
  bool HasDeadzone() const { return deadzone_enabled_; }

  // Returns the world bounds origin.
  FVec2 GetBoundsStart() const { return bounds_start_; }

  // Returns the world bounds size.
  FVec2 GetBoundsSize() const { return bounds_size_; }

  // Returns true if world bounds are active.
  bool HasBounds() const { return bounds_enabled_; }

  // Returns the current shake intensity.
  float GetShakeIntensity() const { return shake_intensity_; }

  // Returns the remaining shake timer.
  float GetShakeTimer() const { return shake_timer_; }

  // Returns the current shake offset applied to the camera.
  FVec2 GetShakeOffset() const { return shake_offset_; }

  // Starts a screen shake. Only replaces if the new shake is stronger.
  void Shake(float intensity, float duration, float frequency) {
    if (intensity >= shake_intensity_) {
      shake_intensity_ = intensity;
      shake_duration_ = duration;
      shake_timer_ = duration;
      shake_frequency_ = frequency;
    }
  }

 private:
  FVec2 position_ = FVec2::Zero();
  float zoom_ = 1.0f;
  float rotation_ = 0.0f;

  FVec2 follow_target_ = FVec2::Zero();
  bool following_ = false;
  FVec2 lerp_ = FVec2(1.0f, 1.0f);

  FVec2 deadzone_ = FVec2::Zero();
  bool deadzone_enabled_ = false;

  FVec2 bounds_start_ = FVec2::Zero();
  FVec2 bounds_size_ = FVec2::Zero();
  bool bounds_enabled_ = false;

  float shake_intensity_ = 0.0f;
  float shake_duration_ = 0.0f;
  float shake_timer_ = 0.0f;
  float shake_frequency_ = 8.0f;
  FVec2 shake_offset_ = FVec2::Zero();
};

}  // namespace G

#endif  // _GAME_CAMERA_H
