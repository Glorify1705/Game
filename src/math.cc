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
  const float a0x = a.top_left.x, a0y = a.top_left.y;
  const float a1x = a.bottom_right.x, a1y = a.top_left.y;
  const float a2x = a.bottom_right.x, a2y = a.bottom_right.y;
  const float a3x = a.top_left.x, a3y = a.bottom_right.y;

  const float b0x = b.top_left.x, b0y = b.top_left.y;
  const float b1x = b.bottom_right.x, b1y = b.top_left.y;
  const float b2x = b.bottom_right.x, b2y = b.bottom_right.y;
  const float b3x = b.top_left.x, b3y = b.bottom_right.y;

  const FVec2 acenter = (a.top_left + a.bottom_right) / 2;

  const FMat4x4 kRotationA = TranslationXY(acenter.x, acenter.y) *
                             RotationZ(a.angle) *
                             TranslationXY(-acenter.x, -acenter.y);

  std::array<FVec2, 4> kPointsA = {FVec(a0x, a0y), FVec(a1x, a1y),
                                   FVec(a2x, a2y), FVec(a3x, a3y)};

  const FVec2 bcenter = (b.top_left + b.bottom_right) / 2;

  const FMat4x4 kRotationB = TranslationXY(bcenter.x, bcenter.y) *
                             RotationZ(b.angle) *
                             TranslationXY(-bcenter.x, -bcenter.y);

  std::array<FVec2, 4> kPointsB = {FVec(b0x, b0y), FVec(b1x, b1y),
                                   FVec(b2x, b2y), FVec(b3x, b3y)};

  for (auto& p : kPointsA) {
    const FVec4 rot = kRotationA * FVec(p.x, p.y, 0, 1);
    p.x = rot.x;
    p.y = rot.y;
  }

  for (auto& p : kPointsB) {
    const FVec4 rot = kRotationB * FVec(p.x, p.y, 0, 1);
    p.x = rot.x;
    p.y = rot.y;
  }

  FixedArray<FVec2, 8> edges;
  for (size_t i = 0; i < kPointsA.size(); ++i) {
    edges.Push(kPointsA[(i + 1) % kPointsA.size()] - kPointsA[i]);
  }
  for (size_t i = 0; i < kPointsB.size(); ++i) {
    edges.Push(kPointsB[(i + 1) % kPointsB.size()] - kPointsB[i]);
  }

  for (const auto& edge : edges) {
    const auto normal = Normal(edge);
    const auto [a_min, a_max] = Project(kPointsA, normal);
    const auto [b_min, b_max] = Project(kPointsB, normal);
    if (a_max < b_min || b_max < a_min) return true;
  }

  return false;
}
