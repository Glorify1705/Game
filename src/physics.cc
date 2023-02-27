#include "physics.h"

#include "mat.h"
#include "transformations.h"

Physics::Handle Physics::AddBox(FVec2 top_left, FVec2 bottom_right,
                                FVec2 initial_position) {
  uint32_t index = boxes_.size();
  Rectangle rect = {.v = {
                        top_left,
                        FVec(bottom_right.x, top_left.y),
                        bottom_right,
                        FVec(top_left.x, bottom_right.y),
                    }};
  boxes_.Push(Box{rect, Body{.position = initial_position}});
  return Handle{index};
}
void Physics::ApplyForce(Handle handle, FVec2 force) {
  auto& box = boxes_[handle.id];
  box.acceleration += force / box.mass;
}

FVec2 Physics::GetPosition(Handle handle) const {
  const auto& box = boxes_[handle.id];
  return box.position;
}

float Physics::GetAngle(Handle handle) const {
  const auto& box = boxes_[handle.id];
  return box.angle;
}

void Physics::Turn(Handle handle, float angle) {
  auto& box = boxes_[handle.id];
  const FMat4x4 transform =
      RotateZOnPoint(box.position.x, box.position.y, angle);
  box.angle += angle;
  for (auto& p : box.v) {
    FVec4 r = transform * FVec(p.x, p.y, 0, 1);
    p = FVec(r.x, r.y);
  }
}

void Physics::Update(float dt) {
  for (auto& box : boxes_) {
    box.velocity += box.acceleration * dt;
    box.position += box.velocity * dt;
  }
}