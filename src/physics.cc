#include "physics.h"

Physics::Handle Physics::AddBox(FVec2 top_left, FVec2 bottom_right,
                                FVec2 initial_position) {
  uint32_t index = boxes_.size();
  boxes_.Push(Box{top_left, bottom_right, /*angle=*/0,
                  Body{.position = initial_position}});
  return Handle{index};
}
void Physics::ApplyForce(Handle handle, FVec2 force) {
  auto& box = boxes_[handle.id];
  box.acceleration += force / box.mass;
}

FVec2 Physics::GetPosition(Handle handle) {
  const auto& box = boxes_[handle.id];
  return box.position;
}

void Physics::Turn(Handle handle, float angle) {
  auto& box = boxes_[handle.id];
  box.angle += angle;
}

void Physics::Update(float dt) {
  for (auto& box : boxes_) {
    box.velocity += box.acceleration * dt;
    box.position += box.velocity * dt;
  }
}