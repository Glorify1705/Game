#include "physics.h"

#include <algorithm>

#include "box2d/b2_distance_joint.h"
#include "box2d/b2_mouse_joint.h"
#include "box2d/b2_prismatic_joint.h"
#include "box2d/b2_revolute_joint.h"
#include "box2d/b2_weld_joint.h"
#include "box2d/b2_wheel_joint.h"
#include "physics_debug_draw.h"

namespace G {
namespace {

void* Box2dAlloc(void* ctx, int32_t size, int32_t align) {
  return static_cast<Allocator*>(ctx)->Alloc(size, align);
}

void Box2dFree(void* ctx, void* ptr, int32_t size) {
  static_cast<Allocator*>(ctx)->Dealloc(ptr, size);
}

b2BodyType ToBox2dBodyType(PhysicsBodyType type) {
  switch (type) {
    case PhysicsBodyType::kStatic:
      return b2_staticBody;
    case PhysicsBodyType::kKinematic:
      return b2_kinematicBody;
    default:
      return b2_dynamicBody;
  }
}

// b2RayCastCallback that keeps only the closest hit.
struct ClosestRaycastCallback : b2RayCastCallback {
  Physics::RaycastHit hit;
  uint16_t mask;
  bool found = false;
  float closest = 1.0f;

  float ReportFixture(b2Fixture* fixture, const b2Vec2& point,
                      const b2Vec2& normal, float fraction) override {
    if (mask != 0xFFFF) {
      uint16_t cat = fixture->GetFilterData().categoryBits;
      if ((cat & mask) == 0) return -1;
    }
    if (fraction < closest) {
      closest = fraction;
      found = true;
      b2Body* body = fixture->GetBody();
      hit.handle = Physics::Handle{body, body->GetUserData().pointer};
      hit.point = FVec(point.x, point.y);
      hit.normal = FVec(normal.x, normal.y);
      hit.fraction = fraction;
    }
    return fraction;
  }
};

// b2RayCastCallback that collects all hits up to a maximum.
struct AllRaycastCallback : b2RayCastCallback {
  Physics::RaycastHit* hits;
  int count = 0;
  int max;
  uint16_t mask;
  float ppm;

  float ReportFixture(b2Fixture* fixture, const b2Vec2& point,
                      const b2Vec2& normal, float fraction) override {
    if (mask != 0xFFFF) {
      uint16_t cat = fixture->GetFilterData().categoryBits;
      if ((cat & mask) == 0) return -1;
    }
    if (count >= max) return 0;
    b2Body* body = fixture->GetBody();
    hits[count].handle = Physics::Handle{body, body->GetUserData().pointer};
    hits[count].point = FVec(point.x, point.y) * ppm;
    hits[count].normal = FVec(normal.x, normal.y);
    hits[count].fraction = fraction;
    count++;
    return 1;
  }
};

}  // namespace

// Sets the Box2D allocator and returns the gravity vector. Called from the
// member initializer list so the allocator is installed before b2World's
// constructor runs (which allocates internal data structures).
b2Vec2 SetupBox2dAllocator(b2Allocator* alloc, Allocator* engine_alloc) {
  alloc->Alloc = Box2dAlloc;
  alloc->Free = Box2dFree;
  alloc->ctx = engine_alloc;
  b2SetAllocator(alloc);
  return b2Vec2(0, 0);
}

Physics::Physics(FVec2 pixel_dimensions, float pixels_per_meter,
                 Allocator* allocator)
    : pixels_per_meter_(pixels_per_meter),
      world_dimensions_(pixel_dimensions / pixels_per_meter),
      world_(SetupBox2dAllocator(&box2d_allocator_, allocator)) {
  world_.SetContactListener(this);
  world_.SetDestructionListener(this);
}

void Physics::SetCollisionCategories(Slice<std::string_view> names) {
  CHECK(names.size() <= 16, "Max 16 collision categories (got ", names.size(),
        ")");
  category_count_ = static_cast<int>(names.size());
  auto& st = StringTable::Instance();
  for (size_t i = 0; i < names.size(); i++) {
    category_handles_[i] = st.Intern(names[i]);
  }
}

uint16_t Physics::ResolveCategory(std::string_view name) const {
  if (category_count_ == 0) return 0;
  uint32_t h = StringTable::Instance().Intern(name);
  for (int i = 0; i < category_count_; i++) {
    if (category_handles_[i] == h) return static_cast<uint16_t>(1 << i);
  }
  return 0;
}

uint16_t Physics::ResolveMask(Slice<std::string_view> names) const {
  uint16_t mask = 0;
  for (size_t i = 0; i < names.size(); i++) {
    mask |= ResolveCategory(names[i]);
  }
  return mask;
}

void Physics::CreateGround(bool walls) {
  walls_ = walls;
  if (ground_ != nullptr) {
    auto* fixture = ground_->GetFixtureList();
    while (fixture) {
      auto* ptr = fixture;
      fixture = fixture->GetNext();
      ground_->DestroyFixture(ptr);
    }
    world_.DestroyBody(ground_);
  }
  b2BodyDef bd;
  bd.type = b2_staticBody;
  bd.position.Set(0.0f, 0.0f);
  bd.userData.pointer = 0;
  ground_ = world_.CreateBody(&bd);

  if (!walls) return;

  b2EdgeShape shape;

  b2FixtureDef sd;
  sd.shape = &shape;
  sd.density = 0.0f;
  sd.restitution = 0.4f;

  // Surround the screen.
  shape.SetTwoSided(b2Vec2(0, 0), b2Vec2(0, world_dimensions_.y));
  ground_->CreateFixture(&sd);
  shape.SetTwoSided(b2Vec2(0, world_dimensions_.y),
                    b2Vec2(world_dimensions_.x, world_dimensions_.y));
  ground_->CreateFixture(&sd);
  shape.SetTwoSided(b2Vec2(world_dimensions_.x, 0),
                    b2Vec2(world_dimensions_.x, world_dimensions_.y));
  ground_->CreateFixture(&sd);
  shape.SetTwoSided(b2Vec2(0, 0), b2Vec2(world_dimensions_.x, 0));
  ground_->CreateFixture(&sd);
}

void Physics::UpdateDimensions(IVec2 pixel_dimensions) {
  world_dimensions_ =
      FVec(pixel_dimensions.x, pixel_dimensions.y) / pixels_per_meter_;
  CreateGround(walls_);
}

void Physics::SetDestroyCallback(DestroyCallback callback, void* userdata) {
  destroy_callback_ = callback;
  destroy_userdata_ = userdata;
}

void Physics::BeginContact(b2Contact* c) {
  b2Body* a = c->GetFixtureA()->GetBody();
  b2Body* b = c->GetFixtureB()->GetBody();
  if (a->GetType() == b2_staticBody && b->GetType() == b2_staticBody) return;
  begin_contact_callback_(a->GetUserData().pointer, b->GetUserData().pointer,
                          begin_contact_userdata_);
}

void Physics::EndContact(b2Contact* c) {
  b2Body* a = c->GetFixtureA()->GetBody();
  b2Body* b = c->GetFixtureB()->GetBody();
  if (a->GetType() == b2_staticBody && b->GetType() == b2_staticBody) return;
  end_contact_callback_(a->GetUserData().pointer, b->GetUserData().pointer,
                        end_contact_userdata_);
}

void Physics::SetBeginContactCallback(ContactCallback callback,
                                      void* userdata) {
  begin_contact_callback_ = callback;
  begin_contact_userdata_ = userdata;
}

void Physics::SetEndContactCallback(ContactCallback callback, void* userdata) {
  end_contact_callback_ = callback;
  end_contact_userdata_ = userdata;
}

Physics::Handle Physics::AddBox(FVec2 top_left, FVec2 bottom_right, float angle,
                                uintptr_t userdata,
                                PhysicsShapeOptions options) {
  CHECK(ground_, "create_ground() must be called before add_box()");
  const b2Vec2 tl = To(top_left);
  const b2Vec2 br = To(bottom_right);
  b2BodyDef def;
  def.type = ToBox2dBodyType(options.body_type);
  def.position.Set((tl.x + br.x) / 2, (tl.y + br.y) / 2);
  def.userData.pointer = userdata;
  b2PolygonShape box;
  box.SetAsBox((br.x - tl.x) / 2, (br.y - tl.y) / 2, b2Vec2(0, 0), angle);
  b2Body* body = world_.CreateBody(&def);
  b2FixtureDef fixture;
  fixture.shape = &box;
  fixture.density = options.density;
  fixture.friction = options.friction;
  fixture.restitution = options.restitution;
  fixture.isSensor = options.sensor;
  fixture.filter.categoryBits = options.category;
  fixture.filter.maskBits = options.mask;
  body->CreateFixture(&fixture);
  if (options.body_type != PhysicsBodyType::kDynamic || options.sensor) {
    return Handle{body, userdata};
  }
  // Friction joint anchors the body to the ground to simulate top-down drag.
  // Without it, bodies slide forever since world gravity is zero.
  b2FrictionJointDef jd;
  float I = body->GetInertia();
  float mass = body->GetMass();
  jd.bodyA = ground_;
  jd.bodyB = body;
  jd.localAnchorA = body->GetPosition();
  jd.localAnchorB = body->GetLocalCenter();
  jd.collideConnected = true;
  const float gravity = 10.0f;
  const float radius = b2Sqrt(2.0f * I / mass);
  jd.maxForce = 0.5f * mass * gravity;
  jd.maxTorque = 0.2f * mass * radius * gravity;
  world_.CreateJoint(&jd);
  return Handle{body, userdata};
}

Physics::Handle Physics::AddCircle(FVec2 position, double radius,
                                   uintptr_t userdata,
                                   PhysicsShapeOptions options) {
  CHECK(ground_, "create_ground() must be called before add_circle()");
  const b2Vec2 p = To(position);
  b2BodyDef def;
  def.type = ToBox2dBodyType(options.body_type);
  def.position.Set(p.x, p.y);
  def.userData.pointer = userdata;
  b2CircleShape circle;
  // m_p is the circle's center in body-local space; the body itself is
  // already positioned at `def.position`, so the local offset is zero.
  // Radius is in meters, but the Lua API takes pixels — convert.
  circle.m_p = b2Vec2(0, 0);
  circle.m_radius = static_cast<float>(radius) / pixels_per_meter_;
  b2Body* body = world_.CreateBody(&def);
  b2FixtureDef fixture;
  fixture.shape = &circle;
  fixture.density = options.density;
  fixture.friction = options.friction;
  fixture.restitution = options.restitution;
  fixture.isSensor = options.sensor;
  fixture.filter.categoryBits = options.category;
  fixture.filter.maskBits = options.mask;
  body->CreateFixture(&fixture);
  if (options.body_type != PhysicsBodyType::kDynamic || options.sensor) {
    return Handle{body, userdata};
  }
  // Friction joint anchors the body to the ground to simulate top-down drag.
  // Without it, bodies slide forever since world gravity is zero.
  b2FrictionJointDef jd;
  float I = body->GetInertia();
  float mass = body->GetMass();
  jd.bodyA = ground_;
  jd.bodyB = body;
  jd.localAnchorA = body->GetPosition();
  jd.localAnchorB = body->GetLocalCenter();
  jd.collideConnected = true;
  const float gravity = 10.0f;
  const float torque_radius = b2Sqrt(2.0f * I / mass);
  jd.maxForce = 0.5f * mass * gravity;
  jd.maxTorque = 0.2f * mass * torque_radius * gravity;
  world_.CreateJoint(&jd);
  return Handle{body, userdata};
}

void Physics::SetOrigin(FVec2 origin) { world_.ShiftOrigin(To(origin)); }

void Physics::DestroyHandle(Handle handle) {
  destroy_callback_(handle.userdata, destroy_userdata_);
  world_.DestroyBody(handle.handle);
}

void Physics::Clear() {
  b2Body* body = world_.GetBodyList();
  while (body != nullptr) {
    b2Body* next = body->GetNext();
    if (body != ground_) {
      destroy_callback_(body->GetUserData().pointer, destroy_userdata_);
      world_.DestroyBody(body);
    }
    body = next;
  }
}

void Physics::Rotate(Handle handle, float angle) {
  auto* body = handle.handle;
  body->SetTransform(body->GetPosition(), body->GetAngle() + angle);
}

void Physics::SetAngle(Handle handle, float angle) {
  auto* body = handle.handle;
  body->SetTransform(body->GetPosition(), angle);
}

void Physics::ApplyTorque(Handle handle, float torque) {
  auto* body = handle.handle;
  body->ApplyTorque(torque, /*wake=*/true);
}

void Physics::ApplyForce(Handle handle, FVec2 v) {
  auto* body = handle.handle;
  body->ApplyForce(body->GetWorldVector(b2Vec2(v.x, v.y)),
                   body->GetWorldCenter(),
                   /*wake=*/true);
}

void Physics::ApplyForceWorld(Handle handle, FVec2 force) {
  auto* body = handle.handle;
  body->ApplyForce(b2Vec2(force.x, force.y), body->GetWorldCenter(),
                   /*wake=*/true);
}

void Physics::ApplyLinearImpulse(Handle handle, FVec2 v) {
  auto* body = handle.handle;
  body->ApplyLinearImpulse(To(v), body->GetWorldCenter(), /*wake=*/true);
}

FVec2 Physics::GetPosition(Handle handle) const {
  return From(handle.handle->GetPosition());
}

float Physics::GetAngle(Handle handle) const {
  return handle.handle->GetAngle();
}

void Physics::SetPosition(Handle handle, FVec2 position) {
  auto* body = handle.handle;
  body->SetTransform(To(position), body->GetAngle());
}

FVec2 Physics::GetLinearVelocity(Handle handle) const {
  return From(handle.handle->GetLinearVelocity());
}

void Physics::SetLinearVelocity(Handle handle, FVec2 v) {
  handle.handle->SetLinearVelocity(To(v));
}

float Physics::GetAngularVelocity(Handle handle) const {
  return handle.handle->GetAngularVelocity();
}

void Physics::SetAngularVelocity(Handle handle, float v) {
  handle.handle->SetAngularVelocity(v);
}

void Physics::SetLinearDamping(Handle handle, float damping) {
  handle.handle->SetLinearDamping(damping);
}

void Physics::SetAngularDamping(Handle handle, float damping) {
  handle.handle->SetAngularDamping(damping);
}

void Physics::SetGravityScale(Handle handle, float scale) {
  handle.handle->SetGravityScale(scale);
}

void Physics::SetBullet(Handle handle, bool bullet) {
  handle.handle->SetBullet(bullet);
}

void Physics::SetFixedRotation(Handle handle, bool fixed) {
  handle.handle->SetFixedRotation(fixed);
}

bool Physics::GetFixedRotation(Handle handle) const {
  return handle.handle->IsFixedRotation();
}

FVec2 Physics::From(b2Vec2 v) const {
  return FVec(v.x, v.y) * pixels_per_meter_;
}
b2Vec2 Physics::To(FVec2 v) const {
  return b2Vec2(v.x / pixels_per_meter_, v.y / pixels_per_meter_);
}

void Physics::SetWorldGravity(FVec2 gravity) {
  world_.SetGravity(
      b2Vec2(gravity.x / pixels_per_meter_, gravity.y / pixels_per_meter_));
}

FVec2 Physics::GetWorldGravity() const {
  b2Vec2 g = world_.GetGravity();
  return FVec(g.x, g.y) * pixels_per_meter_;
}

void Physics::SetIterations(int velocity_iterations, int position_iterations) {
  velocity_iterations_ = velocity_iterations;
  position_iterations_ = position_iterations;
}

bool Physics::Raycast(FVec2 from, FVec2 to, uint16_t mask,
                      RaycastHit* out) const {
  ClosestRaycastCallback cb;
  cb.mask = mask;
  // b2World::RayCast is logically const but not declared so in Box2D.
  const_cast<b2World&>(world_).RayCast(&cb, To(from), To(to));
  if (cb.found) {
    out->handle = cb.hit.handle;
    out->point = FVec(cb.hit.point.x, cb.hit.point.y) * pixels_per_meter_;
    out->normal = cb.hit.normal;
    out->fraction = cb.hit.fraction;
    return true;
  }
  return false;
}

int Physics::RaycastAll(FVec2 from, FVec2 to, uint16_t mask, RaycastHit* out,
                        int max_hits) const {
  AllRaycastCallback cb;
  cb.hits = out;
  cb.max = max_hits;
  cb.mask = mask;
  cb.ppm = pixels_per_meter_;
  const_cast<b2World&>(world_).RayCast(&cb, To(from), To(to));
  std::sort(out, out + cb.count, [](const RaycastHit& a, const RaycastHit& b) {
    return a.fraction < b.fraction;
  });
  return cb.count;
}

void Physics::Update(float dt) {
  world_.Step(dt, velocity_iterations_, position_iterations_);
}

namespace {

// Callback for QueryAABB that finds the first fixture containing a point.
class PointQueryCallback : public b2QueryCallback {
 public:
  b2Vec2 point;
  b2Fixture* found = nullptr;

  bool ReportFixture(b2Fixture* fixture) override {
    if (fixture->TestPoint(point)) {
      found = fixture;
      return false;
    }
    return true;
  }
};

}  // namespace

b2Body* Physics::QueryPoint(FVec2 world_pixels) {
  b2Vec2 point = To(world_pixels);
  constexpr float kQueryRadius = 0.1f;
  b2AABB aabb;
  aabb.lowerBound = point - b2Vec2(kQueryRadius, kQueryRadius);
  aabb.upperBound = point + b2Vec2(kQueryRadius, kQueryRadius);
  PointQueryCallback callback;
  callback.point = point;
  world_.QueryAABB(&callback, aabb);
  if (callback.found != nullptr) {
    return callback.found->GetBody();
  }
  return nullptr;
}

b2Joint* Physics::CreateMouseJoint(b2Body* body, FVec2 world_pixels) {
  b2Vec2 target = To(world_pixels);
  b2MouseJointDef def;
  def.bodyA = ground_;
  def.bodyB = body;
  def.target = target;
  def.maxForce = 1000.0f * body->GetMass();
  b2LinearStiffness(def.stiffness, def.damping, /*frequencyHz=*/5.0f,
                    /*dampingRatio=*/0.7f, def.bodyA, def.bodyB);
  body->SetAwake(true);
  return world_.CreateJoint(&def);
}

void Physics::UpdateMouseJoint(b2Joint* joint, FVec2 world_pixels) {
  auto* mouse_joint = static_cast<b2MouseJoint*>(joint);
  mouse_joint->SetTarget(To(world_pixels));
}

void Physics::DestroyMouseJoint(b2Joint* joint) {
  world_.DestroyJoint(joint);
}

void Physics::EnableDebugDraw(uint32 flags) {
  if (debug_draw_ == nullptr) {
    debug_draw_ = new PhysicsDebugDraw(pixels_per_meter_);
  }
  debug_draw_->SetFlags(flags);
  world_.SetDebugDraw(debug_draw_);
}

void Physics::DisableDebugDraw() {
  world_.SetDebugDraw(nullptr);
  delete debug_draw_;
  debug_draw_ = nullptr;
}

void Physics::DrawDebug(const Camera* camera, FVec2 viewport) {
  if (debug_draw_ == nullptr) return;
  debug_draw_->SetCamera(camera, viewport);
  // Strip the joint bit so Box2D doesn't draw friction joints (which connect
  // every dynamic body to the ground and create visual noise). We draw
  // tracked user joints manually below.
  uint32 flags = debug_draw_->GetFlags();
  debug_draw_->SetFlags(flags & ~b2Draw::e_jointBit);
  world_.DebugDraw();
  debug_draw_->SetFlags(flags);
  // Draw tracked joints only (skips auto-created friction joints).
  if ((flags & b2Draw::e_jointBit) != 0) {
    const b2Color joint_color(0.5f, 0.8f, 0.8f);
    for (int i = 0; i < kMaxJoints; i++) {
      b2Joint* j = joint_slots_[i].joint;
      if (j == nullptr) continue;
      b2Vec2 a = j->GetAnchorA();
      b2Vec2 b = j->GetAnchorB();
      debug_draw_->DrawSegment(a, b, joint_color);
      debug_draw_->DrawPoint(a, 4.0f, joint_color);
      debug_draw_->DrawPoint(b, 4.0f, joint_color);
    }
  }
}

JointHandle Physics::AllocJointSlot(b2Joint* joint) {
  for (int i = 0; i < kMaxJoints; i++) {
    if (joint_slots_[i].joint == nullptr) {
      joint_slots_[i].joint = joint;
      // Store index + 1 so that 0 means "untracked" (friction joints).
      joint->GetUserData().pointer = static_cast<uintptr_t>(i + 1);
      return JointHandle{static_cast<uint32_t>(i), joint_slots_[i].generation};
    }
  }
  CHECK(false, "Joint slot pool exhausted (max ", kMaxJoints, ")");
  return {};
}

b2Joint* Physics::ResolveJoint(JointHandle handle) const {
  if (handle.index >= static_cast<uint32_t>(kMaxJoints)) return nullptr;
  const auto& slot = joint_slots_[handle.index];
  if (slot.generation != handle.generation) return nullptr;
  return slot.joint;
}

void Physics::InvalidateJointSlot(b2Joint* joint) {
  uintptr_t tag = joint->GetUserData().pointer;
  if (tag == 0) return;  // Untracked joint (auto-created friction joint).
  uint32_t index = static_cast<uint32_t>(tag - 1);
  DCHECK(index < static_cast<uint32_t>(kMaxJoints));
  joint_slots_[index].joint = nullptr;
  joint_slots_[index].generation++;
}

void Physics::SayGoodbye(b2Joint* joint) { InvalidateJointSlot(joint); }

void Physics::SayGoodbye(b2Fixture*) {}

void Physics::DestroyJoint(JointHandle handle) {
  b2Joint* j = ResolveJoint(handle);
  if (j == nullptr) return;
  InvalidateJointSlot(j);
  world_.DestroyJoint(j);
}

JointHandle Physics::CreateRevoluteJoint(
    Handle a, Handle b, FVec2 world_anchor, bool enable_limit,
    float lower_angle, float upper_angle, bool enable_motor, float motor_speed,
    float max_motor_torque, bool collide_connected) {
  b2RevoluteJointDef def;
  def.Initialize(a.handle, b.handle, To(world_anchor));
  def.enableLimit = enable_limit;
  def.lowerAngle = lower_angle;
  def.upperAngle = upper_angle;
  def.enableMotor = enable_motor;
  def.motorSpeed = motor_speed;
  def.maxMotorTorque = max_motor_torque;
  def.collideConnected = collide_connected;
  return AllocJointSlot(world_.CreateJoint(&def));
}

JointHandle Physics::CreateDistanceJoint(
    Handle a, Handle b, FVec2 world_anchor_a, FVec2 world_anchor_b,
    float length, float frequency, float damping_ratio,
    bool collide_connected) {
  b2DistanceJointDef def;
  def.Initialize(a.handle, b.handle, To(world_anchor_a), To(world_anchor_b));
  if (length >= 0) {
    def.length = length / pixels_per_meter_;
  }
  if (frequency > 0) {
    b2LinearStiffness(def.stiffness, def.damping, frequency, damping_ratio,
                      a.handle, b.handle);
  }
  def.collideConnected = collide_connected;
  return AllocJointSlot(world_.CreateJoint(&def));
}

JointHandle Physics::CreateWeldJoint(Handle a, Handle b, FVec2 world_anchor,
                                     float frequency, float damping_ratio,
                                     bool collide_connected) {
  b2WeldJointDef def;
  def.Initialize(a.handle, b.handle, To(world_anchor));
  if (frequency > 0) {
    b2AngularStiffness(def.stiffness, def.damping, frequency, damping_ratio,
                       a.handle, b.handle);
  }
  def.collideConnected = collide_connected;
  return AllocJointSlot(world_.CreateJoint(&def));
}

JointHandle Physics::CreatePrismaticJoint(
    Handle a, Handle b, FVec2 world_anchor, FVec2 axis, bool enable_limit,
    float lower_translation, float upper_translation, bool enable_motor,
    float motor_speed, float max_motor_force, bool collide_connected) {
  b2PrismaticJointDef def;
  b2Vec2 axis_norm(axis.x, axis.y);
  axis_norm.Normalize();
  def.Initialize(a.handle, b.handle, To(world_anchor), axis_norm);
  def.enableLimit = enable_limit;
  def.lowerTranslation = lower_translation / pixels_per_meter_;
  def.upperTranslation = upper_translation / pixels_per_meter_;
  def.enableMotor = enable_motor;
  def.motorSpeed = motor_speed / pixels_per_meter_;
  def.maxMotorForce = max_motor_force;
  def.collideConnected = collide_connected;
  return AllocJointSlot(world_.CreateJoint(&def));
}

JointHandle Physics::CreateLuaMouseJoint(Handle body, FVec2 target,
                                         float max_force, float frequency,
                                         float damping_ratio) {
  CHECK(ground_, "create_ground() must be called before create_mouse_joint()");
  b2MouseJointDef def;
  def.bodyA = ground_;
  def.bodyB = body.handle;
  def.target = To(target);
  def.maxForce = max_force;
  if (frequency > 0) {
    b2LinearStiffness(def.stiffness, def.damping, frequency, damping_ratio,
                      ground_, body.handle);
  }
  body.handle->SetAwake(true);
  return AllocJointSlot(world_.CreateJoint(&def));
}

JointHandle Physics::CreateWheelJoint(
    Handle a, Handle b, FVec2 world_anchor, FVec2 axis, bool enable_motor,
    float motor_speed, float max_motor_torque, float frequency,
    float damping_ratio, bool collide_connected) {
  b2WheelJointDef def;
  b2Vec2 axis_norm(axis.x, axis.y);
  axis_norm.Normalize();
  def.Initialize(a.handle, b.handle, To(world_anchor), axis_norm);
  def.enableMotor = enable_motor;
  def.motorSpeed = motor_speed;
  def.maxMotorTorque = max_motor_torque;
  if (frequency > 0) {
    b2LinearStiffness(def.stiffness, def.damping, frequency, damping_ratio,
                      a.handle, b.handle);
  }
  def.collideConnected = collide_connected;
  return AllocJointSlot(world_.CreateJoint(&def));
}

float Physics::GetJointAngle(JointHandle handle) const {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j && j->GetType() == e_revoluteJoint);
  return static_cast<b2RevoluteJoint*>(j)->GetJointAngle();
}

float Physics::GetJointSpeed(JointHandle handle) const {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j);
  if (j->GetType() == e_revoluteJoint) {
    return static_cast<b2RevoluteJoint*>(j)->GetJointSpeed();
  }
  if (j->GetType() == e_prismaticJoint) {
    return static_cast<b2PrismaticJoint*>(j)->GetJointSpeed() *
           pixels_per_meter_;
  }
  CHECK(false, "GetJointSpeed: unsupported joint type");
  return 0;
}

float Physics::GetJointTranslation(JointHandle handle) const {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j && j->GetType() == e_prismaticJoint);
  return static_cast<b2PrismaticJoint*>(j)->GetJointTranslation() *
         pixels_per_meter_;
}

float Physics::GetCurrentLength(JointHandle handle) const {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j && j->GetType() == e_distanceJoint);
  return static_cast<b2DistanceJoint*>(j)->GetCurrentLength() *
         pixels_per_meter_;
}

void Physics::SetMotorSpeed(JointHandle handle, float speed) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j);
  switch (j->GetType()) {
    case e_revoluteJoint:
      static_cast<b2RevoluteJoint*>(j)->SetMotorSpeed(speed);
      break;
    case e_prismaticJoint:
      static_cast<b2PrismaticJoint*>(j)->SetMotorSpeed(speed /
                                                       pixels_per_meter_);
      break;
    case e_wheelJoint:
      static_cast<b2WheelJoint*>(j)->SetMotorSpeed(speed);
      break;
    default:
      CHECK(false, "SetMotorSpeed: unsupported joint type");
  }
}

void Physics::EnableMotor(JointHandle handle, bool flag) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j);
  switch (j->GetType()) {
    case e_revoluteJoint:
      static_cast<b2RevoluteJoint*>(j)->EnableMotor(flag);
      break;
    case e_prismaticJoint:
      static_cast<b2PrismaticJoint*>(j)->EnableMotor(flag);
      break;
    case e_wheelJoint:
      static_cast<b2WheelJoint*>(j)->EnableMotor(flag);
      break;
    default:
      CHECK(false, "EnableMotor: unsupported joint type");
  }
}

void Physics::EnableLimit(JointHandle handle, bool flag) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j);
  switch (j->GetType()) {
    case e_revoluteJoint:
      static_cast<b2RevoluteJoint*>(j)->EnableLimit(flag);
      break;
    case e_prismaticJoint:
      static_cast<b2PrismaticJoint*>(j)->EnableLimit(flag);
      break;
    default:
      CHECK(false, "EnableLimit: unsupported joint type");
  }
}

void Physics::SetJointLimits(JointHandle handle, float lower, float upper) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j);
  switch (j->GetType()) {
    case e_revoluteJoint:
      static_cast<b2RevoluteJoint*>(j)->SetLimits(lower, upper);
      break;
    case e_prismaticJoint:
      static_cast<b2PrismaticJoint*>(j)->SetLimits(lower / pixels_per_meter_,
                                                   upper / pixels_per_meter_);
      break;
    default:
      CHECK(false, "SetJointLimits: unsupported joint type");
  }
}

void Physics::SetMaxMotorTorque(JointHandle handle, float torque) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j);
  switch (j->GetType()) {
    case e_revoluteJoint:
      static_cast<b2RevoluteJoint*>(j)->SetMaxMotorTorque(torque);
      break;
    case e_wheelJoint:
      static_cast<b2WheelJoint*>(j)->SetMaxMotorTorque(torque);
      break;
    default:
      CHECK(false, "SetMaxMotorTorque: unsupported joint type");
  }
}

void Physics::SetMaxMotorForce(JointHandle handle, float force) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j && j->GetType() == e_prismaticJoint);
  static_cast<b2PrismaticJoint*>(j)->SetMaxMotorForce(force);
}

void Physics::SetLength(JointHandle handle, float length) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j && j->GetType() == e_distanceJoint);
  static_cast<b2DistanceJoint*>(j)->SetLength(length / pixels_per_meter_);
}

void Physics::SetTarget(JointHandle handle, FVec2 target) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j && j->GetType() == e_mouseJoint);
  static_cast<b2MouseJoint*>(j)->SetTarget(To(target));
}

void Physics::SetMaxForce(JointHandle handle, float force) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j && j->GetType() == e_mouseJoint);
  static_cast<b2MouseJoint*>(j)->SetMaxForce(force);
}

void Physics::SetJointFrequency(JointHandle handle, float hz) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j);
  float stiffness = 0;
  float damping = 0;
  // Retrieve current damping ratio by computing from current values, then
  // recompute with new frequency. Simpler: just use the helper with ratio=0.7
  // as default. But better: set stiffness directly from frequency and mass.
  b2Body* ba = j->GetBodyA();
  b2Body* bb = j->GetBodyB();
  switch (j->GetType()) {
    case e_distanceJoint: {
      auto* dj = static_cast<b2DistanceJoint*>(j);
      b2LinearStiffness(stiffness, damping, hz, /*damping_ratio=*/0.5f, ba, bb);
      dj->SetStiffness(stiffness);
      break;
    }
    case e_weldJoint: {
      auto* wj = static_cast<b2WeldJoint*>(j);
      b2AngularStiffness(stiffness, damping, hz, /*damping_ratio=*/0.5f, ba,
                         bb);
      wj->SetStiffness(stiffness);
      break;
    }
    case e_mouseJoint: {
      auto* mj = static_cast<b2MouseJoint*>(j);
      b2LinearStiffness(stiffness, damping, hz, /*damping_ratio=*/0.7f, ba, bb);
      mj->SetStiffness(stiffness);
      break;
    }
    case e_wheelJoint: {
      auto* wj = static_cast<b2WheelJoint*>(j);
      b2LinearStiffness(stiffness, damping, hz, /*damping_ratio=*/0.7f, ba, bb);
      wj->SetStiffness(stiffness);
      break;
    }
    default:
      CHECK(false, "SetJointFrequency: unsupported joint type");
  }
}

void Physics::SetJointDampingRatio(JointHandle handle, float ratio) {
  b2Joint* j = ResolveJoint(handle);
  CHECK(j);
  float stiffness = 0;
  float damping = 0;
  b2Body* ba = j->GetBodyA();
  b2Body* bb = j->GetBodyB();
  switch (j->GetType()) {
    case e_distanceJoint: {
      auto* dj = static_cast<b2DistanceJoint*>(j);
      b2LinearStiffness(stiffness, damping, /*hz=*/4.0f, ratio, ba, bb);
      dj->SetDamping(damping);
      break;
    }
    case e_weldJoint: {
      auto* wj = static_cast<b2WeldJoint*>(j);
      b2AngularStiffness(stiffness, damping, /*hz=*/4.0f, ratio, ba, bb);
      wj->SetDamping(damping);
      break;
    }
    case e_mouseJoint: {
      auto* mj = static_cast<b2MouseJoint*>(j);
      b2LinearStiffness(stiffness, damping, /*hz=*/5.0f, ratio, ba, bb);
      mj->SetDamping(damping);
      break;
    }
    case e_wheelJoint: {
      auto* wj = static_cast<b2WheelJoint*>(j);
      b2LinearStiffness(stiffness, damping, /*hz=*/4.0f, ratio, ba, bb);
      wj->SetDamping(damping);
      break;
    }
    default:
      CHECK(false, "SetJointDampingRatio: unsupported joint type");
  }
}

}  // namespace G