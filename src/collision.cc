#include "collision.h"

#include <algorithm>
#include <cmath>

namespace G {
namespace {

float Clamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

}  // namespace

CollisionResult TestCircleCircle(FVec2 pos_a, float radius_a, FVec2 pos_b,
                                 float radius_b) {
  CollisionResult result = {};
  FVec2 delta = pos_b - pos_a;
  float dist2 = delta.Length2();
  float sum_r = radius_a + radius_b;

  if (dist2 >= sum_r * sum_r) return result;

  float dist = std::sqrt(dist2);
  result.hit = true;

  if (dist < 1e-8f) {
    // Circles are coincident — pick an arbitrary separation direction.
    result.normal = FVec(1.0f, 0.0f);
    result.depth = sum_r;
    result.contact = pos_a;
  } else {
    FVec2 dir = delta * (1.0f / dist);  // A toward B
    result.normal = -dir;               // separation: push A away from B
    result.depth = sum_r - dist;
    result.contact = pos_a + dir * radius_a;  // on A's surface toward B
  }
  return result;
}

CollisionResult TestAABBAABB(FVec2 pos_a, float hw_a, float hh_a, FVec2 pos_b,
                             float hw_b, float hh_b) {
  CollisionResult result = {};

  float dx = pos_b.x - pos_a.x;
  float dy = pos_b.y - pos_a.y;
  float overlap_x = (hw_a + hw_b) - std::abs(dx);
  float overlap_y = (hh_a + hh_b) - std::abs(dy);

  if (overlap_x <= 0 || overlap_y <= 0) return result;

  result.hit = true;

  // MTV is the axis with minimum overlap. Normal is the separation direction
  // for A (push A by normal * depth to resolve).
  if (overlap_x < overlap_y) {
    float sign = dx > 0 ? 1.0f : -1.0f;  // A toward B direction
    result.normal = FVec(-sign, 0.0f);   // separation: push A away from B
    result.depth = overlap_x;
    result.contact =
        FVec(pos_a.x + hw_a * sign, std::max(pos_a.y - hh_a, pos_b.y - hh_b) +
                                        std::min(hh_a * 2, hh_b * 2) * 0.5f);
  } else {
    float sign = dy > 0 ? 1.0f : -1.0f;  // A toward B direction
    result.normal = FVec(0.0f, -sign);   // separation: push A away from B
    result.depth = overlap_y;
    result.contact = FVec(std::max(pos_a.x - hw_a, pos_b.x - hw_b) +
                              std::min(hw_a * 2, hw_b * 2) * 0.5f,
                          pos_a.y + hh_a * sign);
  }
  return result;
}

CollisionResult TestCircleAABB(FVec2 circle_pos, float radius, FVec2 aabb_pos,
                               float hw, float hh) {
  CollisionResult result = {};

  float left = aabb_pos.x - hw;
  float right = aabb_pos.x + hw;
  float top = aabb_pos.y - hh;
  float bottom = aabb_pos.y + hh;

  // Closest point on AABB to circle center.
  float cx = Clamp(circle_pos.x, left, right);
  float cy = Clamp(circle_pos.y, top, bottom);

  // Check if circle center is inside the AABB.
  if (cx == circle_pos.x && cy == circle_pos.y) {
    // Center is inside — find nearest edge to push toward.
    float d_left = circle_pos.x - left;
    float d_right = right - circle_pos.x;
    float d_top = circle_pos.y - top;
    float d_bottom = bottom - circle_pos.y;

    float min_d = d_left;
    result.normal = FVec(-1.0f, 0.0f);

    if (d_right < min_d) {
      min_d = d_right;
      result.normal = FVec(1.0f, 0.0f);
    }
    if (d_top < min_d) {
      min_d = d_top;
      result.normal = FVec(0.0f, -1.0f);
    }
    if (d_bottom < min_d) {
      min_d = d_bottom;
      result.normal = FVec(0.0f, 1.0f);
    }

    result.hit = true;
    result.depth = radius + min_d;
    result.contact = circle_pos - result.normal * min_d;
    return result;
  }

  FVec2 closest = FVec(cx, cy);
  FVec2 delta = circle_pos - closest;
  float dist2 = delta.Length2();

  if (dist2 > radius * radius) return result;

  float dist = std::sqrt(dist2);
  result.hit = true;
  result.normal = delta * (1.0f / dist);
  result.depth = radius - dist;
  result.contact = closest;
  return result;
}

CollisionResult TestShapes(const CollisionShape& a, FVec2 pos_a,
                           const CollisionShape& b, FVec2 pos_b) {
  if (a.type == CollisionShapeType::kCircle &&
      b.type == CollisionShapeType::kCircle) {
    return TestCircleCircle(pos_a, a.circle.radius, pos_b, b.circle.radius);
  }
  if (a.type == CollisionShapeType::kAABB &&
      b.type == CollisionShapeType::kAABB) {
    return TestAABBAABB(pos_a, a.aabb.half_w, a.aabb.half_h, pos_b,
                        b.aabb.half_w, b.aabb.half_h);
  }
  if (a.type == CollisionShapeType::kCircle &&
      b.type == CollisionShapeType::kAABB) {
    return TestCircleAABB(pos_a, a.circle.radius, pos_b, b.aabb.half_w,
                          b.aabb.half_h);
  }
  if (a.type == CollisionShapeType::kAABB &&
      b.type == CollisionShapeType::kCircle) {
    // Flip: test as circle-AABB then negate normal.
    CollisionResult r = TestCircleAABB(pos_b, b.circle.radius, pos_a,
                                       a.aabb.half_w, a.aabb.half_h);
    if (r.hit) {
      r.normal = -r.normal;
    }
    return r;
  }
  return {};
}

RaycastResult RaycastCircle(FVec2 origin, FVec2 direction, float max_dist,
                            FVec2 circle_pos, float radius) {
  // Solve |origin + t*direction - center|^2 = radius^2
  FVec2 oc = origin - circle_pos;
  float a = direction.Dot(direction);
  float b = 2.0f * oc.Dot(direction);
  float c = oc.Dot(oc) - radius * radius;

  float disc = b * b - 4.0f * a * c;
  if (disc < 0) return {};

  float sqrt_disc = std::sqrt(disc);
  float inv_2a = 1.0f / (2.0f * a);
  float t0 = (-b - sqrt_disc) * inv_2a;
  float t1 = (-b + sqrt_disc) * inv_2a;

  float t = t0;
  if (t < 0) t = t1;
  if (t < 0 || t > max_dist) return {};

  FVec2 hit_point = origin + direction * t;
  return {true, t, (hit_point - circle_pos).Normalized()};
}

RaycastResult RaycastAABB(FVec2 origin, FVec2 direction, float max_dist,
                          FVec2 aabb_pos, float hw, float hh) {
  // Slab method.
  float min_x = aabb_pos.x - hw;
  float max_x = aabb_pos.x + hw;
  float min_y = aabb_pos.y - hh;
  float max_y = aabb_pos.y + hh;

  float tmin = 0.0f;
  float tmax = max_dist;
  FVec2 normal = FVec2::Zero();

  // X axis slab.
  if (std::abs(direction.x) < 1e-8f) {
    if (origin.x < min_x || origin.x > max_x) return {};
  } else {
    float inv_d = 1.0f / direction.x;
    float t1 = (min_x - origin.x) * inv_d;
    float t2 = (max_x - origin.x) * inv_d;
    FVec2 n1 = FVec(-1.0f, 0.0f);
    FVec2 n2 = FVec(1.0f, 0.0f);
    if (t1 > t2) {
      std::swap(t1, t2);
      std::swap(n1, n2);
    }
    if (t1 > tmin) {
      tmin = t1;
      normal = n1;
    }
    if (t2 < tmax) tmax = t2;
    if (tmin > tmax) return {};
  }

  // Y axis slab.
  if (std::abs(direction.y) < 1e-8f) {
    if (origin.y < min_y || origin.y > max_y) return {};
  } else {
    float inv_d = 1.0f / direction.y;
    float t1 = (min_y - origin.y) * inv_d;
    float t2 = (max_y - origin.y) * inv_d;
    FVec2 n1 = FVec(0.0f, -1.0f);
    FVec2 n2 = FVec(0.0f, 1.0f);
    if (t1 > t2) {
      std::swap(t1, t2);
      std::swap(n1, n2);
    }
    if (t1 > tmin) {
      tmin = t1;
      normal = n1;
    }
    if (t2 < tmax) tmax = t2;
    if (tmin > tmax) return {};
  }

  if (tmin < 0) return {};

  return {true, tmin, normal};
}

RaycastResult RaycastShape(FVec2 origin, FVec2 direction, float max_dist,
                           const CollisionShape& shape, FVec2 shape_pos) {
  switch (shape.type) {
    case CollisionShapeType::kCircle:
      return RaycastCircle(origin, direction, max_dist, shape_pos,
                           shape.circle.radius);
    case CollisionShapeType::kAABB:
      return RaycastAABB(origin, direction, max_dist, shape_pos,
                         shape.aabb.half_w, shape.aabb.half_h);
  }
  return {};
}

bool PointInShape(FVec2 point, const CollisionShape& shape, FVec2 shape_pos) {
  switch (shape.type) {
    case CollisionShapeType::kCircle: {
      FVec2 d = point - shape_pos;
      return d.Length2() <= shape.circle.radius * shape.circle.radius;
    }
    case CollisionShapeType::kAABB: {
      return point.x >= shape_pos.x - shape.aabb.half_w &&
             point.x <= shape_pos.x + shape.aabb.half_w &&
             point.y >= shape_pos.y - shape.aabb.half_h &&
             point.y <= shape_pos.y + shape.aabb.half_h;
    }
  }
  return false;
}

}  // namespace G
