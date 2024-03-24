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
    uintptr_t userdata;
  };

  explicit Physics(FVec2 pixel_dimensions, float pixels_per_meter,
                   Allocator *allocator);

  void UpdateDimensions(IVec2 pixel_dimensions);

  using ContactCallback = void (*)(uintptr_t, uintptr_t, void *);
  void SetBeginContactCallback(ContactCallback contact_callback,
                               void *userdata);
  void SetEndContactCallback(ContactCallback contact_callback, void *userdata);

  using DestroyCallback = void (*)(uintptr_t, void *);
  void SetDestroyCallback(DestroyCallback destroy_callback, void *userdata);

  void Update(float dt);
  void SetOrigin(FVec2 origin);

  void BeginContact(b2Contact *c);
  void EndContact(b2Contact *);

  Handle AddBox(FVec2 top_left, FVec2 top_right, float angle,
                uintptr_t userdata);
  Handle AddCircle(FVec2 position, double radius, uintptr_t userdata);

  void DestroyHandle(Handle handle);

  void ApplyLinearImpulse(Handle handle, FVec2 v);

  void Rotate(Handle handle, float angle);

  void ApplyTorque(Handle handle, float torque);

  void ApplyForce(Handle handle, FVec2 v);

  FVec2 GetPosition(Handle handle) const;

  float GetAngle(Handle handle) const;

  void CreateGround();

 private:
  static void DefaultDestroy(uintptr_t, void *) {}

  static void DefaultContact(uintptr_t, uintptr_t, void *) {}

  b2Allocator box2d_allocator_;

  FVec2 From(b2Vec2 v) const;
  b2Vec2 To(FVec2 v) const;

  const float pixels_per_meter_;
  FVec2 world_dimensions_;
  b2World world_;
  b2Body *ground_ = nullptr;

  ContactCallback begin_contact_callback_ = DefaultContact;
  void *begin_contact_userdata_ = this;

  ContactCallback end_contact_callback_ = DefaultContact;
  void *end_contact_userdata_ = this;

  DestroyCallback destroy_callback_ = DefaultDestroy;
  void *destroy_userdata_ = this;
};

}  // namespace G

#endif