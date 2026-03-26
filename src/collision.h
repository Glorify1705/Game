#pragma once
#ifndef _GAME_COLLISION_H
#define _GAME_COLLISION_H

#include <cstdint>

#include "vec.h"

namespace G {

// Shape types supported by the collision system.
// Extensible: add kCapsule, kPolygon here when needed.
enum class CollisionShapeType : uint8_t {
  kCircle,
  kAABB,
};

struct CircleShape {
  float radius;
};

struct AABBShape {
  float half_w;
  float half_h;
};

struct CollisionShape {
  CollisionShapeType type;
  union {
    CircleShape circle;
    AABBShape aabb;
  };
};

inline CollisionShape MakeCircle(float radius) {
  CollisionShape s;
  s.type = CollisionShapeType::kCircle;
  s.circle.radius = radius;
  return s;
}

inline CollisionShape MakeAABB(float width, float height) {
  CollisionShape s;
  s.type = CollisionShapeType::kAABB;
  s.aabb.half_w = width * 0.5f;
  s.aabb.half_h = height * 0.5f;
  return s;
}

struct CollisionAABB {
  FVec2 min;
  FVec2 max;
};

// Compute the world-space AABB for a shape at a given position.
inline CollisionAABB ComputeAABB(const CollisionShape& shape, FVec2 position) {
  CollisionAABB result;
  switch (shape.type) {
    case CollisionShapeType::kCircle:
      result.min = FVec(position.x - shape.circle.radius,
                        position.y - shape.circle.radius);
      result.max = FVec(position.x + shape.circle.radius,
                        position.y + shape.circle.radius);
      break;
    case CollisionShapeType::kAABB:
      result.min =
          FVec(position.x - shape.aabb.half_w, position.y - shape.aabb.half_h);
      result.max =
          FVec(position.x + shape.aabb.half_w, position.y + shape.aabb.half_h);
      break;
  }
  return result;
}

struct CollisionResult {
  bool hit = false;
  FVec2 normal;   // Separation normal — push A by normal * depth to resolve
  float depth;    // Penetration depth
  FVec2 contact;  // Contact point in world space
};

struct CollisionFilter {
  uint16_t category = 0x0001;  // What I am
  uint16_t mask = 0xFFFF;      // What I detect
};

inline bool ShouldCollide(CollisionFilter a, CollisionFilter b) {
  return (a.category & b.mask) != 0 && (b.category & a.mask) != 0;
}

// Narrow-phase pair tests. All take world-space positions.
// Normal is the separation direction: move A by normal * depth to resolve.
CollisionResult TestCircleCircle(FVec2 pos_a, float radius_a, FVec2 pos_b,
                                 float radius_b);

CollisionResult TestAABBAABB(FVec2 pos_a, float hw_a, float hh_a, FVec2 pos_b,
                             float hw_b, float hh_b);

CollisionResult TestCircleAABB(FVec2 circle_pos, float radius, FVec2 aabb_pos,
                               float hw, float hh);

// General dispatcher: tests any supported shape pair.
CollisionResult TestShapes(const CollisionShape& a, FVec2 pos_a,
                           const CollisionShape& b, FVec2 pos_b);

struct RaycastResult {
  bool hit = false;
  float t;       // Parametric distance along ray
  FVec2 normal;  // Surface normal at hit point
};

// Ray intersection tests.
RaycastResult RaycastCircle(FVec2 origin, FVec2 direction, float max_dist,
                            FVec2 circle_pos, float radius);

RaycastResult RaycastAABB(FVec2 origin, FVec2 direction, float max_dist,
                          FVec2 aabb_pos, float hw, float hh);

RaycastResult RaycastShape(FVec2 origin, FVec2 direction, float max_dist,
                           const CollisionShape& shape, FVec2 shape_pos);

// Point-in-shape test.
bool PointInShape(FVec2 point, const CollisionShape& shape, FVec2 shape_pos);

}  // namespace G

#endif  // _GAME_COLLISION_H
