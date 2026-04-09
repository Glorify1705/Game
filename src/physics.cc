#include "physics.h"

namespace G {
namespace {

void* Box2dAlloc(void* ctx, int32_t size, int32_t align) {
  return static_cast<Allocator*>(ctx)->Alloc(size, align);
}

void Box2dFree(void* ctx, void* ptr, int32_t size) {
  static_cast<Allocator*>(ctx)->Dealloc(ptr, size);
}

}  // namespace

Physics::Physics(FVec2 pixel_dimensions, float pixels_per_meter,
                 Allocator* allocator)
    : pixels_per_meter_(pixels_per_meter),
      world_dimensions_(pixel_dimensions / pixels_per_meter),
      world_(b2Vec2(0, 0)) {
  box2d_allocator_.Alloc = Box2dAlloc;
  box2d_allocator_.Free = Box2dFree;
  box2d_allocator_.ctx = allocator;
  b2SetAllocator(&box2d_allocator_);
  world_.SetContactListener(this);
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
  def.type = b2_dynamicBody;
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
  if (options.sensor) return Handle{body, userdata};
  // Friction joint anchors the body to the ground to simulate top-down drag.
  // Without it, bodies slide forever since world gravity is zero.
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
                                   uintptr_t userdata,
                                   PhysicsShapeOptions options) {
  CHECK(ground_, "create_ground() must be called before add_circle()");
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
  fixture.density = options.density;
  fixture.friction = options.friction;
  fixture.restitution = options.restitution;
  fixture.isSensor = options.sensor;
  fixture.filter.categoryBits = options.category;
  fixture.filter.maskBits = options.mask;
  body->CreateFixture(&fixture);
  if (options.sensor) return Handle{body, userdata};
  // Friction joint anchors the body to the ground to simulate top-down drag.
  // Without it, bodies slide forever since world gravity is zero.
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

void Physics::Update(float dt) {
  constexpr int32_t kVelocityIterations = 6;
  constexpr int32_t kPositionIterations = 2;
  world_.Step(dt, kVelocityIterations, kPositionIterations);
}

}  // namespace G