#pragma once
#ifndef _PHYSICS_H
#define _PHYSICS_H

#include "array.h"
#include "box2d/box2d.h"
#include "math.h"
#include "vec.h"

class Physics final : public b2ContactListener {
 public:
  inline static constexpr float kPixelsPerMeter = 60;
  struct Handle {
    b2Body *handle;
  };

  Physics();

  void Update(float dt);
  void SetOrigin(FVec2 origin);

  void BeginContact(b2Contact *) override {}

  void EndContact(b2Contact *) override {}

  Handle AddBox(FVec2 top_left, FVec2 top_right, float angle);

  void ApplyLinearVelocity(Handle handle, FVec2 v);

  void Rotate(Handle handle, float angle);

  FVec2 GetPosition(Handle handle) const;

  float GetAngle(Handle handle) const;

 private:
  b2World world_;
};

#endif