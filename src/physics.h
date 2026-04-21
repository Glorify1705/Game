#pragma once
#ifndef _PHYSICS_H
#define _PHYSICS_H

#include <array>
#include <string_view>

#include "array.h"
#include "box2d/box2d.h"
#include "math.h"
#include "string_table.h"
#include "vec.h"

namespace G {

class Camera;
class PhysicsDebugDraw;
class Renderer;

// Body type for physics bodies.
enum class PhysicsBodyType : uint8_t {
  // Fully simulated. Responds to forces, impulses, gravity, collisions.
  kDynamic = 0,
  // Does not move. Infinite mass. Used for ground, walls, platforms.
  kStatic = 1,
  // Moves at a set velocity but unaffected by forces or collisions.
  kKinematic = 2,
};

// Options for shape creation. Controls material, filtering, and sensing.
struct PhysicsShapeOptions {
  // Mass per unit area. Higher = heavier for the same size.
  float density = 2.0f;
  // Surface friction coefficient (0 = ice, 1 = sandpaper).
  float friction = 0.3f;
  // Bounciness (0 = no bounce, 1 = perfectly elastic).
  float restitution = 0.0f;
  // Sensor shapes detect overlap without physical response.
  bool sensor = false;
  // Body type (dynamic, static, or kinematic).
  PhysicsBodyType body_type = PhysicsBodyType::kDynamic;
  // Collision category bit (what this shape is).
  uint16_t category = 0x0001;
  // Collision mask bits (what this shape collides with).
  uint16_t mask = 0xFFFF;
};

class Physics final : public b2ContactListener {
 public:
  inline static constexpr float kPixelsPerMeter = 60;
  struct Handle {
    b2Body *handle;
    uintptr_t userdata;
  };

  explicit Physics(FVec2 pixel_dimensions, float pixels_per_meter,
                   Allocator *allocator);

  // Registers named collision categories. Each name is assigned a unique bit.
  // Max 16 categories (uint16_t). Must be called before creating bodies.
  void SetCollisionCategories(Slice<std::string_view> names);

  // Resolves a category name to its bit value. Returns 0 if not found.
  uint16_t ResolveCategory(std::string_view name) const;

  // Resolves a list of category names to an OR'd bitmask.
  uint16_t ResolveMask(Slice<std::string_view> names) const;

  void UpdateDimensions(IVec2 pixel_dimensions);

  using ContactCallback = void (*)(uintptr_t, uintptr_t, void *);
  void SetBeginContactCallback(ContactCallback contact_callback,
                               void *userdata);
  void SetEndContactCallback(ContactCallback contact_callback, void *userdata);

  using DestroyCallback = void (*)(uintptr_t, void *);
  void SetDestroyCallback(DestroyCallback destroy_callback, void *userdata);

  void Update(float dt);
  void SetOrigin(FVec2 origin);

  void BeginContact(b2Contact *c) override;
  void EndContact(b2Contact *) override;

  Handle AddBox(FVec2 top_left, FVec2 top_right, float angle,
                uintptr_t userdata, PhysicsShapeOptions options = {});
  Handle AddCircle(FVec2 position, double radius, uintptr_t userdata,
                   PhysicsShapeOptions options = {});

  void DestroyHandle(Handle handle);

  // Destroy all dynamic bodies, keeping the ground. Used during hot-reload.
  void Clear();

  void ApplyLinearImpulse(Handle handle, FVec2 v);

  void Rotate(Handle handle, float angle);

  void ApplyTorque(Handle handle, float torque);

  void ApplyForce(Handle handle, FVec2 v);

  FVec2 GetPosition(Handle handle) const;

  float GetAngle(Handle handle) const;

  // Teleports a body to a new position, preserving angle and velocity.
  void SetPosition(Handle handle, FVec2 position);

  // Returns the linear velocity in pixel-space.
  FVec2 GetLinearVelocity(Handle handle) const;

  // Sets the linear velocity in pixel-space.
  void SetLinearVelocity(Handle handle, FVec2 v);

  // Returns the angular velocity in radians/s.
  float GetAngularVelocity(Handle handle) const;

  // Sets the angular velocity in radians/s.
  void SetAngularVelocity(Handle handle, float v);

  // Sets linear damping (drag). 0 = no damping, higher = more drag.
  void SetLinearDamping(Handle handle, float damping);

  // Sets angular damping (rotational drag).
  void SetAngularDamping(Handle handle, float damping);

  // Scales the effect of world gravity on this body. 0 = no gravity, 1 =
  // normal gravity, -1 = anti-gravity.
  void SetGravityScale(Handle handle, float scale);

  // Flags a body for continuous collision detection. Use on fast-moving
  // bodies (projectiles) to prevent tunneling through thin geometry.
  void SetBullet(Handle handle, bool bullet);

  // Prevents a body from rotating. Useful for bullets and characters.
  void SetFixedRotation(Handle handle, bool fixed);

  // Returns whether the body has fixed rotation.
  bool GetFixedRotation(Handle handle) const;

  // Creates a static ground body. If walls is true, adds edge fixtures
  // around the screen perimeter.
  void CreateGround(bool walls = true);

  // Sets the world gravity vector in pixels/s^2.
  void SetWorldGravity(FVec2 gravity);

  // Returns the world gravity vector in pixels/s^2.
  FVec2 GetWorldGravity() const;

  // Sets the solver iteration counts per time step.
  void SetIterations(int velocity_iterations, int position_iterations);

  // Returns the pixels-per-meter scale factor (read-only after construction).
  float GetPixelsPerMeter() const { return pixels_per_meter_; }

  // Raycast hit result returned to callers.
  struct RaycastHit {
    // The body that was hit.
    Handle handle;
    // The hit point in pixel-space.
    FVec2 point;
    // The surface normal at the hit point.
    FVec2 normal;
    // Parametric fraction along the ray (0 = start, 1 = end).
    float fraction;
  };

  // Casts a ray and returns the closest hit. Returns true if something was hit.
  bool Raycast(FVec2 from, FVec2 to, uint16_t mask, RaycastHit *out) const;

  // Casts a ray and returns all hits, sorted by fraction (closest first).
  // Results are appended to the output array. Returns the number of hits.
  int RaycastAll(FVec2 from, FVec2 to, uint16_t mask, RaycastHit *out,
                 int max_hits) const;

  // Returns the number of bodies in the physics world.
  int GetBodyCount() const { return world_.GetBodyCount(); }

  // Returns the number of joints in the physics world.
  int GetJointCount() const { return world_.GetJointCount(); }

  // Returns the number of contacts in the physics world.
  int GetContactCount() const { return world_.GetContactCount(); }

  // Returns the head of the body linked list for iteration.
  const b2Body *GetBodyList() const { return world_.GetBodyList(); }

  // Returns the velocity solver iteration count.
  int GetVelocityIterations() const { return velocity_iterations_; }

  // Returns the position solver iteration count.
  int GetPositionIterations() const { return position_iterations_; }

  // Enables or disables physics debug drawing with the given flags.
  void EnableDebugDraw(uint32 flags);

  // Disables physics debug drawing.
  void DisableDebugDraw();

  // Draws physics debug visualization if enabled. Call during the ImGui pass
  // so shapes render on top of canvases and post-processing.
  void DrawDebug(const Camera* camera, FVec2 viewport);

  // Returns true if debug drawing is enabled.
  bool debug_draw_enabled() const { return debug_draw_ != nullptr; }

  // Finds the body at a world pixel position. Returns nullptr if none found.
  b2Body* QueryPoint(FVec2 world_pixels);

  // Creates a mouse joint for dragging a body. Returns the joint handle.
  b2Joint* CreateMouseJoint(b2Body* body, FVec2 world_pixels);

  // Updates the mouse joint target to follow the cursor.
  void UpdateMouseJoint(b2Joint* joint, FVec2 world_pixels);

  // Destroys a mouse joint.
  void DestroyMouseJoint(b2Joint* joint);

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
  bool walls_ = false;
  PhysicsDebugDraw* debug_draw_ = nullptr;

  ContactCallback begin_contact_callback_ = DefaultContact;
  void *begin_contact_userdata_ = this;

  ContactCallback end_contact_callback_ = DefaultContact;
  void *end_contact_userdata_ = this;

  DestroyCallback destroy_callback_ = DefaultDestroy;
  void *destroy_userdata_ = this;

  // Solver iteration counts per time step.
  int velocity_iterations_ = 6;
  int position_iterations_ = 2;

  // Interned category names, indexed by bit position.
  std::array<uint32_t, 16> category_handles_ = {};
  int category_count_ = 0;
};

}  // namespace G

#endif