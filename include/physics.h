#pragma once
#ifndef _PHYSICS_H
#define _PHYSICS_H

#include "array.h"
#include "box2d/box2d.h"
#include "lookup_table.h"
#include "math.h"
#include "vec.h"

namespace G {

class Physics final : public b2ContactListener {
 public:
  inline static constexpr float kPixelsPerMeter = 60;
  struct Handle {
    b2Body *handle;
    uintptr_t userdata;
  };

  explicit Physics(FVec2 pixel_dimensions, float pixels_per_meter);

  using ContactCallback = void (*)(uintptr_t, uintptr_t, void *);
  void SetContactCallback(ContactCallback contact_callback, void *userdata);

  using DestroyCallback = void (*)(uintptr_t, void *);
  void SetDestroyCallback(DestroyCallback destroy_callback, void *userdata);

  void Update(float dt);
  void SetOrigin(FVec2 origin);

  void BeginContact(b2Contact *c);

  void EndContact(b2Contact *);

  Handle AddBox(FVec2 top_left, FVec2 top_right, float angle,
                uintptr_t userdata);

  void DestroyHandle(Handle handle);

  void ApplyLinearImpulse(Handle handle, FVec2 v);

  void Rotate(Handle handle, float angle);

  void ApplyTorque(Handle handle, float torque);

  void ApplyForce(Handle handle, FVec2 v);

  FVec2 GetPosition(Handle handle) const;

  float GetAngle(Handle handle) const;

 private:
  static void DefaultDestroy(uintptr_t, void *) {}

  static void DefaultContact(uintptr_t, uintptr_t, void *) {}

  FVec2 From(b2Vec2 v) const;
  b2Vec2 To(FVec2 v) const;

  const float pixels_per_meter_;
  FVec2 world_dimensions_;
  b2World world_;
  b2Body *ground_;

  ContactCallback contact_callback_ = DefaultContact;
  void *contact_userdata_ = this;

  DestroyCallback destroy_callback_ = DefaultDestroy;
  void *destroy_userdata_ = this;
};

}  // namespace G

#endif