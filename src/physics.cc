#include "physics.h"

#include "mat.h"
#include "transformations.h"

Physics::Physics() : world_(b2Vec2(0, 0)) { world_.SetContactListener(this); }

Physics::Handle Physics::AddBox(FVec2 top_left, FVec2 bottom_right, FVec2 pos) {
  b2BodyDef def;
  def.type = b2_dynamicBody;
  def.position.Set(pos.x, pos.y);
  b2PolygonShape box;
  FVec2 center = (top_left + bottom_right) / 2.0;
  box.SetAsBox(center.x, center.y);
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

void Physics::Update(float dt) {
  constexpr int32_t kVelocityIterations = 6;
  constexpr int32_t kPositionIterations = 2;
  world_.Step(dt, kVelocityIterations, kPositionIterations);
}