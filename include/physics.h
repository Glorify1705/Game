#pragma once
#ifndef _PHYSICS_H
#define _PHYSICS_H

#include "array.h"
#include "box2d/box2d.h"
#include "math.h"
#include "vec.h"

namespace G {

class Physics final : public b2ContactListener {
 public:
  inline static constexpr float kPixelsPerMeter = 60;
  struct Handle {
    b2Body *handle;
  };

  explicit Physics(FVec2 pixel_dimensions, float pixels_per_meter);

  void Update(float dt);
  void SetOrigin(FVec2 origin);

  void BeginContact(b2Contact *) override { LOG("Colliding begin"); }

  void EndContact(b2Contact *) override { LOG("Colliding end"); }

  Handle AddBox(FVec2 top_left, FVec2 top_right, float angle);

  void ApplyLinearImpulse(Handle handle, FVec2 v);

  void Rotate(Handle handle, float angle);

  void ApplyTorque(Handle handle, float torque);

  void ApplyForce(Handle handle, FVec2 v);

  FVec2 GetPosition(Handle handle) const;

  float GetAngle(Handle handle) const;

 private:
  FVec2 From(b2Vec2 v) const;
  b2Vec2 To(FVec2 v) const;

  float pixels_per_meter_;
  FVec2 world_dimensions_;
  b2World world_;
  b2Body *ground_;
};

}  // namespace G

#endif