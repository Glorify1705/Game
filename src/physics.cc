#include "console.h"
#include "mat.h"
#include "physics.h"
#include "transformations.h"

namespace G {
namespace {

void DefaultContactCallback(std::string_view lhs, std::string_view rhs) {
  LOG("Collision between ", lhs, " and ", rhs);
}

}  // namespace

Physics::Physics(FVec2 pixel_dimensions, float pixels_per_meter)
    : pixels_per_meter_(pixels_per_meter),
      world_dimensions_(pixel_dimensions.x / pixels_per_meter,
                        pixel_dimensions.y / pixels_per_meter),
      world_(b2Vec2(0, 0)) {
  WATCH_EXPR("World", world_dimensions_);
  SetContactCallback(DefaultContactCallback);

  world_.SetGravity(b2Vec2(0, 0));
  world_.SetContactListener(this);

  b2BodyDef bd;
  bd.position.Set(0.0f, 0.0f);
  bd.userData.pointer = AddId("walls");
  ground_ = world_.CreateBody(&bd);

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

void Physics::BeginContact(b2Contact* c) {
  const uintptr_t a = c->GetFixtureA()->GetBody()->GetUserData().pointer;
  const uintptr_t b = c->GetFixtureB()->GetBody()->GetUserData().pointer;
  contact_callback_(Convert(a), Convert(b));
}

void Physics::EndContact(b2Contact*) {}

void Physics::SetContactCallback(ContactCallback callback) {
  contact_callback_ = callback;
}

uintptr_t Physics::AddId(std::string_view id) {
  DCHECK(ids_pos_ + id.size() < kMaxIdsLength, "OOM ids_pos_ = ", ids_pos_);
  std::memcpy(&ids_[ids_pos_], id.data(), id.size());
  size_t pos = ids_pos_;
  ids_pos_ += id.size();
  return (id.size() << kMaxIdsLenLog) | pos;
}

std::string_view Physics::Convert(uintptr_t encoded) const {
  const size_t index = encoded & (kMaxIdsLength - 1);
  return std::string_view(&ids_[index], encoded >> kMaxIdsLenLog);
}

Physics::Handle Physics::AddBox(FVec2 top_left, FVec2 bottom_right, float angle,
                                std::string_view id) {
  const b2Vec2 tl = To(top_left);
  const b2Vec2 br = To(bottom_right);
  b2BodyDef def;
  def.type = b2_dynamicBody;
  def.position.Set((tl.x + br.x) / 2, (tl.y + br.y) / 2);
  def.userData.pointer = AddId(id);
  b2PolygonShape box;
  box.SetAsBox((br.x - tl.x) / 2, (br.y - tl.y) / 2, b2Vec2(0, 0), angle);
  b2Body* body = world_.CreateBody(&def);
  b2FixtureDef fixture;
  fixture.shape = &box;
  fixture.density = 2.0f;
  fixture.friction = 0.3f;
  if (id.empty()) {
    fixture.isSensor = true;
  }
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
  return Handle{body};
}

void Physics::SetOrigin(FVec2 origin) { world_.ShiftOrigin(To(origin)); }

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