#include "transformations.h"

#include <cmath>

#include "gtest/gtest.h"

namespace G {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEps = 1e-5f;

// Helper: check that a 4x4 matrix is the identity.
void ExpectIdentity(const FMat4x4& m) {
  for (size_t i = 0; i < 4; ++i) {
    for (size_t j = 0; j < 4; ++j) {
      float expected = (i == j) ? 1.0f : 0.0f;
      EXPECT_NEAR(m.val(i, j), expected, kEps)
          << "at (" << i << ", " << j << ")";
    }
  }
}

}  // namespace

// TranslationXY

TEST(TransformTest, TranslationIdentityAtZero) {
  FMat4x4 t = TranslationXY(0, 0);
  ExpectIdentity(t);
}

TEST(TransformTest, TranslationSetsLastColumn) {
  FMat4x4 t = TranslationXY(10, -20);
  EXPECT_FLOAT_EQ(t.val(0, 3), 10.0f);
  EXPECT_FLOAT_EQ(t.val(1, 3), -20.0f);
  // Diagonal is still 1.
  EXPECT_FLOAT_EQ(t.val(0, 0), 1.0f);
  EXPECT_FLOAT_EQ(t.val(1, 1), 1.0f);
  EXPECT_FLOAT_EQ(t.val(2, 2), 1.0f);
  EXPECT_FLOAT_EQ(t.val(3, 3), 1.0f);
}

TEST(TransformTest, TranslationTransformsPoint) {
  FMat4x4 t = TranslationXY(5, 3);
  FVec4 p(1, 2, 0, 1);  // Homogeneous point.
  FVec4 result = t * p;
  EXPECT_NEAR(result.x, 6.0f, kEps);
  EXPECT_NEAR(result.y, 5.0f, kEps);
}

// ScaleXY

TEST(TransformTest, ScaleIdentityAtOne) {
  FMat4x4 s = ScaleXY(1, 1);
  ExpectIdentity(s);
}

TEST(TransformTest, ScaleSetsDiagonal) {
  FMat4x4 s = ScaleXY(2, 3);
  EXPECT_FLOAT_EQ(s.val(0, 0), 2.0f);
  EXPECT_FLOAT_EQ(s.val(1, 1), 3.0f);
  EXPECT_FLOAT_EQ(s.val(2, 2), 1.0f);
  EXPECT_FLOAT_EQ(s.val(3, 3), 1.0f);
}

TEST(TransformTest, ScaleTransformsPoint) {
  FMat4x4 s = ScaleXY(2, 0.5f);
  FVec4 p(3, 4, 0, 1);
  FVec4 result = s * p;
  EXPECT_NEAR(result.x, 6.0f, kEps);
  EXPECT_NEAR(result.y, 2.0f, kEps);
}

// RotationZ

TEST(TransformTest, RotationZeroIsIdentity) {
  FMat4x4 r = RotationZ(0);
  ExpectIdentity(r);
}

TEST(TransformTest, Rotation90DegreesSwapsAxes) {
  FMat4x4 r = RotationZ(kPi / 2);
  FVec4 p(1, 0, 0, 1);
  FVec4 result = r * p;
  EXPECT_NEAR(result.x, 0.0f, kEps);
  EXPECT_NEAR(result.y, 1.0f, kEps);
}

TEST(TransformTest, Rotation180DegreesNegates) {
  FMat4x4 r = RotationZ(kPi);
  FVec4 p(1, 0, 0, 1);
  FVec4 result = r * p;
  EXPECT_NEAR(result.x, -1.0f, kEps);
  EXPECT_NEAR(result.y, 0.0f, kEps);
}

TEST(TransformTest, RotationPreservesLength) {
  FMat4x4 r = RotationZ(0.7f);
  FVec4 p(3, 4, 0, 0);  // Direction vector (w=0).
  FVec4 result = r * p;
  float original_len = std::sqrt(3 * 3 + 4 * 4);
  float rotated_len = std::sqrt(result.x * result.x + result.y * result.y);
  EXPECT_NEAR(rotated_len, original_len, kEps);
}

TEST(TransformTest, RotationFullCircle) {
  FMat4x4 r = RotationZ(2 * kPi);
  FVec4 p(5, 7, 0, 1);
  FVec4 result = r * p;
  EXPECT_NEAR(result.x, 5.0f, kEps);
  EXPECT_NEAR(result.y, 7.0f, kEps);
}

// Ortho

TEST(TransformTest, OrthoCenterMapsToOrigin) {
  FMat4x4 o = Ortho(/*l=*/0, /*r=*/800, /*t=*/600, /*b=*/0);
  // Center of the viewport: (400, 300).
  FVec4 center(400, 300, 0, 1);
  FVec4 result = o * center;
  EXPECT_NEAR(result.x, 0.0f, kEps);
  EXPECT_NEAR(result.y, 0.0f, kEps);
}

TEST(TransformTest, OrthoCornersMaps) {
  FMat4x4 o = Ortho(/*l=*/0, /*r=*/800, /*t=*/600, /*b=*/0);
  // Bottom-left (0,0) → (-1,-1).
  FVec4 bl(0, 0, 0, 1);
  FVec4 rbl = o * bl;
  EXPECT_NEAR(rbl.x, -1.0f, kEps);
  EXPECT_NEAR(rbl.y, -1.0f, kEps);
  // Top-right (800,600) → (1,1).
  FVec4 tr(800, 600, 0, 1);
  FVec4 rtr = o * tr;
  EXPECT_NEAR(rtr.x, 1.0f, kEps);
  EXPECT_NEAR(rtr.y, 1.0f, kEps);
}

// RotateZOnPoint

TEST(TransformTest, RotateZOnPointPivot) {
  // Rotating around a point should leave that point unchanged.
  FMat4x4 r = RotateZOnPoint(10, 20, kPi / 3);
  FVec4 pivot(10, 20, 0, 1);
  FVec4 result = r * pivot;
  EXPECT_NEAR(result.x, 10.0f, kEps);
  EXPECT_NEAR(result.y, 20.0f, kEps);
}

TEST(TransformTest, RotateZOnPointRotatesOthers) {
  // Rotate (11, 20) around (10, 20) by 90 degrees → (10, 21).
  FMat4x4 r = RotateZOnPoint(10, 20, kPi / 2);
  FVec4 p(11, 20, 0, 1);
  FVec4 result = r * p;
  EXPECT_NEAR(result.x, 10.0f, kEps);
  EXPECT_NEAR(result.y, 21.0f, kEps);
}

// Composition

TEST(TransformTest, ScaleThenTranslate) {
  FMat4x4 s = ScaleXY(2, 2);
  FMat4x4 t = TranslationXY(10, 5);
  FMat4x4 combined = t * s;
  FVec4 p(1, 1, 0, 1);
  FVec4 result = combined * p;
  // Scale first: (2,2), then translate: (12,7).
  EXPECT_NEAR(result.x, 12.0f, kEps);
  EXPECT_NEAR(result.y, 7.0f, kEps);
}

TEST(TransformTest, TranslateThenScale) {
  FMat4x4 t = TranslationXY(10, 5);
  FMat4x4 s = ScaleXY(2, 2);
  FMat4x4 combined = s * t;
  FVec4 p(1, 1, 0, 1);
  FVec4 result = combined * p;
  // Translate first: (11,6), then scale: (22,12).
  EXPECT_NEAR(result.x, 22.0f, kEps);
  EXPECT_NEAR(result.y, 12.0f, kEps);
}

TEST(TransformTest, InverseRotation) {
  float angle = 1.23f;
  FMat4x4 r = RotationZ(angle);
  FMat4x4 rinv = RotationZ(-angle);
  FMat4x4 product = r * rinv;
  ExpectIdentity(product);
}

TEST(TransformTest, InverseTranslation) {
  FMat4x4 t = TranslationXY(7, -3);
  FMat4x4 tinv = TranslationXY(-7, 3);
  FMat4x4 product = t * tinv;
  ExpectIdentity(product);
}

}  // namespace G
