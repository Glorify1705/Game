#pragma once
#ifndef _PHYSICS_H
#define _PHYSICS_H

#include "array.h"
#include "math.h"
#include "vec.h"

class Physics {
 public:
  struct Handle {
    uint32_t id;
  };

  Handle AddBox(FVec2 top_left, FVec2 bottom_right, FVec2 initial_position);

  FVec2 GetPosition(Handle handle);
  FVec2 GetAngle(Handle handle);

  void ApplyForce(Handle handle, FVec2 force);

  void Turn(Handle handle, float angle);

  void Update(float dt);

 private:
  struct Body {
    FVec2 position = FVec2::Zero();
    FVec2 velocity = FVec2::Zero();
    FVec2 acceleration = FVec2::Zero();
    float mass = 1.0;
  };
  struct Box final : public Rectangle, public Body {};

  FixedArray<Box, 256> boxes_;
};

#endif