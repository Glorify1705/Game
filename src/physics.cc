#include "physics.h"

#include "mat.h"
#include "transformations.h"

namespace {

FVec2 From(b2Vec2 v) { return FVec(v.x, v.y); }
b2Vec2 To(FVec2 v) { return b2Vec2(v.x, v.y); }

}  // namespace

Physics::Physics() : world_(b2Vec2(0, 0)) { world_.SetContactListener(this); }

Physics::Handle Physics::AddBox(FVec2 top_left, FVec2 bottom_right,
                                float angle) {
  const FVec2 center = (top_left + bottom_right) / 2.0;
  b2BodyDef def;
  def.type = b2_dynamicBody;
  def.position.Set(center.x, center.y);
  b2PolygonShape box;
  const FVec2 hws = (bottom_right - top_left) / 2;
  box.SetAsBox(hws.x, hws.y, To(center), angle);
  b2Body* body = world_.CreateBody(&def);
  b2FixtureDef fixture;
  fixture.shape = &box;
  fixture.density = 1.0f;
  fixture.friction = 0.3f;
  body->CreateFixture(&fixture);
  return Handle{body};
}

void Physics::SetOrigin(FVec2 origin) {
  world_.ShiftOrigin(b2Vec2(origin.x, origin.y));
}

void Physics::Rotate(Handle handle, float angle) {
  auto* body = handle.handle;
  body->SetTransform(body->GetPosition(), body->GetAngle() + angle);
}

void Physics::ApplyLinearVelocity(Handle handle, FVec2 v) {
  auto* body = handle.handle;
  const auto before = body->GetPosition();
  body->ApplyLinearImpulse(To(v), body->GetWorldCenter(), /*wake=*/true);
  const auto after = body->GetPosition();
  LOG("v = ", v, " ", From(before), " ", From(after));
}

FVec2 Physics::GetPosition(Handle handle) const {
  return From(handle.handle->GetPosition());
}

float Physics::GetAngle(Handle handle) const {
  return handle.handle->GetAngle();
}

void Physics::Update(float dt) {
  constexpr int32_t kVelocityIterations = 6;
  constexpr int32_t kPositionIterations = 2;
  world_.Step(dt, kVelocityIterations, kPositionIterations);
}