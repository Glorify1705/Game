#include "math.h"

#include <array>
#include <utility>

#include "array.h"
#include "transformations.h"

namespace {

FVec2 Normal(FVec2 v) { return FVec(-v.y, v.x).Normalized(); }

template <typename T>
std::pair<float, float> Project(const T& edges, FVec2 normal) {
  float m = std::numeric_limits<float>::max(),
        M = std::numeric_limits<float>::min();
  for (const auto& p : edges) {
    m = std::min(m, p.Dot(normal));
    M = std::min(M, p.Dot(normal));
  }
  return std::make_pair(m, M);
}

struct Edge {
  FVec2 p0;
  FVec2 p1;
};

}  // namespace

// Based on Separating Axis Theorem
// (https://en.wikipedia.org/wiki/Hyperplane_separation_theorem).
bool CheckOverlap(const Rectangle& a, const Rectangle& b) {
  FixedArray<FVec2, 8> edges;
  for (size_t i = 0; i < a.v.size(); ++i) {
    edges.Push(a.v[(i + 1) % a.v.size()] - a.v[i]);
  }
  for (size_t i = 0; i < b.v.size(); ++i) {
    edges.Push(b.v[(i + 1) % b.v.size()] - b.v[i]);
  }

  for (const auto& edge : edges) {
    const auto normal = Normal(edge);
    const auto [a_min, a_max] = Project(a.v, normal);
    const auto [b_min, b_max] = Project(a.v, normal);
    if (a_max < b_min || b_max < a_min) return true;
  }

  return false;
}
