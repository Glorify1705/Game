#include "console.h"
#include "lua.h"
#include "mat.h"
#include "physics.h"
#include "transformations.h"

namespace G {
namespace {

void* BoxAlloc(void* context, int32_t mem) {
  return reinterpret_cast<Allocator*>(context)->Alloc(mem, /*align=*/16);
}

void BoxDealloc(void* context, void* ptr) {
  return reinterpret_cast<Allocator*>(context)->Dealloc(ptr, /*size=*/1);
}

}  // namespace

Physics::Physics(FVec2 pixel_dimensions, float pixels_per_meter,
                 Allocator* allocator)
    : pixels_per_meter_(pixels_per_meter),
      world_dimensions_(pixel_dimensions / pixels_per_meter),
      world_(b2Vec2(0, 0)) {
  b2SetAllocFunction(BoxAlloc, allocator);
  b2SetFreeFunction(BoxDealloc, allocator);
  world_.SetContactListener(this);
}

void Physics::CreateGround() {
  if (ground_ == nullptr) {
    b2BodyDef bd;
    bd.type = b2_staticBody;
    bd.position.Set(0.0f, 0.0f);
    bd.userData.pointer = 0;
    ground_ = world_.CreateBody(&bd);
  } else {
    auto* fixture = ground_->GetFixtureList();
    while (fixture) {
      auto* ptr = fixture;
      fixture = fixture->GetNext();
      ground_->DestroyFixture(ptr);
    }
  }
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
  CreateGround();
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
                                uintptr_t userdata) {
  const b2Vec2 tl = To(top_left);
  const b2Vec2 br = To(bottom_right);
  b2BodyDef def;
  def.type = b2_dynamicBody;
  def.position.Set((tl.x + br.x) / 2, (tl.y + br.y) / 2);
  def.userData.pointer = userdata;
  b2PolygonShape box;
  box.SetAsBox((br.x - tl.x) / 2, (br.y - tl.y) / 2, b2Vec2(0, 0), angle);
  b2Body* body = world_.CreateBody(&def);
  b2FixtureDef fixture;
  fixture.shape = &box;
  fixture.density = 2.0f;
  fixture.friction = 0.3f;
  body->CreateFixture(&fixture);
  b2FrictionJointDef jd;
  float I = body->GetInertia();
  float mass = body->GetMass();
  jd.bodyA = ground_;
  jd.bodyB = body;
  jd.localAnchorA.SetZero();
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
                                   uintptr_t userdata) {
  const b2Vec2 p = To(position);
  b2BodyDef def;
  def.type = b2_dynamicBody;
  def.position.Set(p.x, p.y);
  def.userData.pointer = userdata;
  b2CircleShape circle;
  circle.m_p = def.position;
  circle.m_radius = radius;
  b2Body* body = world_.CreateBody(&def);
  b2FixtureDef fixture;
  fixture.shape = &circle;
  fixture.density = 2.0f;
  fixture.friction = 0.3f;
  body->CreateFixture(&fixture);
  b2FrictionJointDef jd;
  float I = body->GetInertia();
  float mass = body->GetMass();
  jd.bodyA = ground_;
  jd.bodyB = body;
  jd.localAnchorA.SetZero();
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

void Physics::Rotate(Handle handle, float angle) {
  auto* body = handle.handle;
  body->SetTransform(body->GetPosition(), body->GetAngle() + angle);
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

FVec2 Physics::From(b2Vec2 v) const {
  return FVec(v.x, v.y) * pixels_per_meter_;
}
b2Vec2 Physics::To(FVec2 v) const {
  return b2Vec2(v.x / pixels_per_meter_, v.y / pixels_per_meter_);
}

void Physics::Update(float dt) {
  constexpr int32_t kVelocityIterations = 6;
  constexpr int32_t kPositionIterations = 2;
  world_.Step(dt, kVelocityIterations, kPositionIterations);
}

}  // namespace G