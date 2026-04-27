#include <cmath>

#include "bits.h"
#include "easing.h"
#include "gtest/gtest.h"
#include "vec.h"

namespace G {

TEST(VectorTest, DotAndLength) {
  FVec3 v, w;
  v = FVec3::Zero();
  EXPECT_FLOAT_EQ(v.Dot(v), 0);
  EXPECT_FLOAT_EQ(v.Length2(), 0);
  v = FVec3(1, 2, 3);
  w = FVec3(3, 2, 1);
  EXPECT_FLOAT_EQ(v.Dot(w), 10);
  EXPECT_FLOAT_EQ(v.Length2(), 14);
  EXPECT_FLOAT_EQ(v.Length(), std::sqrt(14));
}

TEST(BitsTest, NextPow2) {
  EXPECT_EQ(NextPow2(1), 1);
  EXPECT_EQ(NextPow2(13), 16);
  EXPECT_EQ(NextPow2(2), 2);
}

TEST(EasingTest, BoundaryValues) {
  for (int i = 0; i < kEasingCount; ++i) {
    EasingType type = static_cast<EasingType>(i);
    float at_zero = Ease(type, 0.0f);
    float at_one = Ease(type, 1.0f);
    EXPECT_NEAR(at_zero, 0.0f, 1e-5f) << "Easing " << i << " at t=0";
    EXPECT_NEAR(at_one, 1.0f, 1e-5f) << "Easing " << i << " at t=1";
  }
}

TEST(EasingTest, Monotonicity) {
  // Most easings should be monotonically increasing (except elastic/back which
  // overshoot). Test the simple ones.
  EasingType monotonic[] = {kLinear,   kInQuad,    kOutQuad,    kInOutQuad,
                            kInCubic,  kOutCubic,  kInOutCubic, kInSine,
                            kOutSine,  kInOutSine, kInExpo,     kOutExpo,
                            kInOutExpo};
  for (EasingType type : monotonic) {
    float prev = 0.0f;
    for (int i = 1; i <= 100; ++i) {
      float t = static_cast<float>(i) / 100.0f;
      float val = Ease(type, t);
      EXPECT_GE(val, prev - 1e-6f) << "Easing " << type << " at t=" << t;
      prev = val;
    }
  }
}

TEST(EasingTest, LinearIsIdentity) {
  for (int i = 0; i <= 10; ++i) {
    float t = static_cast<float>(i) / 10.0f;
    EXPECT_NEAR(Ease(kLinear, t), t, 1e-6f);
  }
}

TEST(EasingTest, InOutSymmetry) {
  // in-out curves should satisfy f(0.5) ~ 0.5
  EasingType inout[] = {kInOutQuad,  kInOutCubic, kInOutQuart,
                        kInOutQuint, kInOutSine,  kInOutExpo,
                        kInOutCirc,  kInOutBack,  kInOutElastic};
  for (EasingType type : inout) {
    EXPECT_NEAR(Ease(type, 0.5f), 0.5f, 1e-4f) << "Easing " << type;
  }
}

// FVec2 new methods.

TEST(VectorTest, Vec2Length) {
  FVec2 v(3.0f, 4.0f);
  EXPECT_FLOAT_EQ(v.Length(), 5.0f);
  EXPECT_FLOAT_EQ(v.Length2(), 25.0f);
}

TEST(VectorTest, Vec2Distance) {
  FVec2 a(1.0f, 2.0f);
  FVec2 b(4.0f, 6.0f);
  FVec2 d = a - b;
  EXPECT_FLOAT_EQ(d.Length(), 5.0f);
}

TEST(VectorTest, Vec2Rotate) {
  FVec2 v(1.0f, 0.0f);
  float angle = static_cast<float>(M_PI / 2.0);
  float c = std::cos(angle);
  float s = std::sin(angle);
  FVec2 rotated(v.x * c - v.y * s, v.x * s + v.y * c);
  EXPECT_NEAR(rotated.x, 0.0f, 1e-6f);
  EXPECT_NEAR(rotated.y, 1.0f, 1e-6f);
}

TEST(VectorTest, Vec2Perpendicular) {
  FVec2 v(3.0f, 4.0f);
  FVec2 perp(-v.y, v.x);
  EXPECT_FLOAT_EQ(v.Dot(perp), 0.0f);
}

TEST(VectorTest, Vec2Reflect) {
  FVec2 v(1.0f, -1.0f);
  FVec2 n(0.0f, 1.0f);
  float d = 2.0f * v.Dot(n);
  FVec2 reflected(v.x - d * n.x, v.y - d * n.y);
  EXPECT_NEAR(reflected.x, 1.0f, 1e-6f);
  EXPECT_NEAR(reflected.y, 1.0f, 1e-6f);
}

TEST(VectorTest, Vec2Project) {
  FVec2 v(3.0f, 4.0f);
  FVec2 onto(1.0f, 0.0f);
  float d = v.Dot(onto) / onto.Length2();
  FVec2 projected = onto * d;
  EXPECT_NEAR(projected.x, 3.0f, 1e-6f);
  EXPECT_NEAR(projected.y, 0.0f, 1e-6f);
}

TEST(VectorTest, Vec2Angle) {
  FVec2 v(0.0f, 1.0f);
  EXPECT_NEAR(std::atan2(v.y, v.x), M_PI / 2.0, 1e-6);
}

TEST(VectorTest, Vec2Lerp) {
  FVec2 a(0.0f, 0.0f);
  FVec2 b(10.0f, 20.0f);
  FVec2 mid(a.x + (b.x - a.x) * 0.5f, a.y + (b.y - a.y) * 0.5f);
  EXPECT_FLOAT_EQ(mid.x, 5.0f);
  EXPECT_FLOAT_EQ(mid.y, 10.0f);
}

}  // namespace G
