#include "allocators.h"
#include "array.h"
#include "bits.h"
#include "defer.h"
#include "dictionary.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest-matchers.h"
#include "stringlib.h"
#include "vec.h"

using ::testing::Pointee;

namespace G {

TEST(Tests, FixedArray) {
  FixedArray<int> array(3, SystemAllocator::Instance());
  EXPECT_EQ(array.size(), 0);
  array.Push(1);
  EXPECT_EQ(array.size(), 1);
  array.Push(2);
  EXPECT_EQ(array.size(), 2);
  EXPECT_EQ(array[0], 1);
  EXPECT_EQ(array[1], 2);
  EXPECT_EQ(array.bytes(), 2 * sizeof(int));
  array.Push(3);
  EXPECT_EQ(array[0], 1);
  EXPECT_EQ(array[1], 2);
  EXPECT_EQ(array[2], 3);
}

TEST(Tests, FixedArrayWithAllocator) {
  FixedArray<int> array(3, SystemAllocator::Instance());
  EXPECT_EQ(array.size(), 0);
  array.Push(1);
  EXPECT_EQ(array.size(), 1);
  array.Push(2);
  EXPECT_EQ(array.size(), 2);
  EXPECT_EQ(array[0], 1);
  EXPECT_EQ(array[1], 2);
  EXPECT_EQ(array.bytes(), 2 * sizeof(int));
  array.Push(3);
  EXPECT_EQ(array[0], 1);
  EXPECT_EQ(array[1], 2);
  EXPECT_EQ(array[2], 3);
}

TEST(Tests, DynArray) {
  DynArray<int> array(SystemAllocator::Instance());
  EXPECT_EQ(array.size(), 0);
  array.Push(0);
  EXPECT_EQ(array.size(), 1);
  EXPECT_EQ(array.back(), 0);
  array.Push(1);
  EXPECT_EQ(array.size(), 2);
  EXPECT_EQ(array[0], 0);
  EXPECT_EQ(array.back(), 1);
  for (int i = 2; i < 100; ++i) {
    array.Push(i);
    EXPECT_EQ(array.back(), i);
  }
  EXPECT_EQ(array.size(), 100);
  for (std::size_t i = 0; i < array.size(); ++i) {
    EXPECT_EQ(array[i], i);
  }
  int p = 0;
  for (const int v : array) {
    EXPECT_EQ(v, p);
    p++;
  }
}

TEST(Tests, DynArrayMove) {
  DynArray<int> array(SystemAllocator::Instance());
  for (int i = 0; i < 100; ++i) {
    array.Push(i);
  }
  StaticAllocator<1024> allocator2;
  DynArray<int> array2(&allocator2);
  array2 = std::move(array);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(array2[i], i);
  }
  EXPECT_EQ(array2.size(), 100);
  EXPECT_EQ(array.size(), 0);
}

// ---------------------------------------------------------------------------
// FVec2
// ---------------------------------------------------------------------------

TEST(FVec2, Zero) {
  FVec2 v = FVec2::Zero();
  EXPECT_FLOAT_EQ(v.x, 0);
  EXPECT_FLOAT_EQ(v.y, 0);
}

TEST(FVec2, ExplicitConstructor) {
  FVec2 v(3.0f, 4.0f);
  EXPECT_FLOAT_EQ(v.x, 3.0f);
  EXPECT_FLOAT_EQ(v.y, 4.0f);
}

TEST(FVec2, ArrayAccess) {
  FVec2 v(1.0f, 2.0f);
  EXPECT_FLOAT_EQ(v.v[0], 1.0f);
  EXPECT_FLOAT_EQ(v.v[1], 2.0f);
}

TEST(FVec2, Addition) {
  FVec2 a(1.0f, 2.0f);
  FVec2 b(3.0f, 4.0f);
  FVec2 c = a + b;
  EXPECT_EQ(c, FVec2(4.0f, 6.0f));
}

TEST(FVec2, AddAssign) {
  FVec2 a(1.0f, 2.0f);
  a += FVec2(3.0f, 4.0f);
  EXPECT_EQ(a, FVec2(4.0f, 6.0f));
}

TEST(FVec2, Subtraction) {
  FVec2 a(5.0f, 7.0f);
  FVec2 b(2.0f, 3.0f);
  EXPECT_EQ(a - b, FVec2(3.0f, 4.0f));
}

TEST(FVec2, SubAssign) {
  FVec2 a(5.0f, 7.0f);
  a -= FVec2(2.0f, 3.0f);
  EXPECT_EQ(a, FVec2(3.0f, 4.0f));
}

TEST(FVec2, UnaryNegate) {
  FVec2 v(3.0f, -4.0f);
  FVec2 neg = -v;
  EXPECT_EQ(neg, FVec2(-3.0f, 4.0f));
}

TEST(FVec2, ScalarMultiply) {
  FVec2 v(2.0f, 3.0f);
  EXPECT_EQ(v * 2.0f, FVec2(4.0f, 6.0f));
}

TEST(FVec2, ScalarDivide) {
  FVec2 v(6.0f, 4.0f);
  EXPECT_EQ(v / 2.0f, FVec2(3.0f, 2.0f));
}

TEST(FVec2, MulAssign) {
  FVec2 v(2.0f, 3.0f);
  v *= 3.0f;
  EXPECT_EQ(v, FVec2(6.0f, 9.0f));
}

TEST(FVec2, Dot) {
  FVec2 a(1.0f, 2.0f);
  FVec2 b(3.0f, 4.0f);
  EXPECT_FLOAT_EQ(a.Dot(b), 11.0f);
}

TEST(FVec2, DotZero) {
  FVec2 z = FVec2::Zero();
  FVec2 v(1.0f, 2.0f);
  EXPECT_FLOAT_EQ(z.Dot(v), 0.0f);
}

TEST(FVec2, DotPerpendicular) {
  FVec2 a(1.0f, 0.0f);
  FVec2 b(0.0f, 1.0f);
  EXPECT_FLOAT_EQ(a.Dot(b), 0.0f);
}

TEST(FVec2, Equality) {
  FVec2 a(1.0f, 2.0f);
  FVec2 b(1.0f, 2.0f);
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
}

TEST(FVec2, Inequality) {
  FVec2 a(1.0f, 2.0f);
  FVec2 b(1.0f, 3.0f);
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a != b);
}

TEST(FVec2, Length2) {
  FVec2 v(3.0f, 4.0f);
  EXPECT_FLOAT_EQ(v.Length2(), 25.0f);
}

TEST(FVec2, Length) {
  FVec2 v(3.0f, 4.0f);
  EXPECT_FLOAT_EQ(v.Length(), 5.0f);
}

TEST(FVec2, Normalized) {
  FVec2 v(3.0f, 4.0f);
  FVec2 n = v.Normalized();
  EXPECT_NEAR(n.Length(), 1.0f, 1e-6f);
  EXPECT_NEAR(n.x, 0.6f, 1e-6f);
  EXPECT_NEAR(n.y, 0.8f, 1e-6f);
}

TEST(FVec2, FreeFunction) {
  FVec2 v = FVec(1.0f, 2.0f);
  EXPECT_FLOAT_EQ(v.x, 1.0f);
  EXPECT_FLOAT_EQ(v.y, 2.0f);
}

TEST(FVec2, Cardinality) { EXPECT_EQ(FVec2::kCardinality, 2u); }

// ---------------------------------------------------------------------------
// FVec3
// ---------------------------------------------------------------------------

TEST(FVec3, Zero) {
  FVec3 v = FVec3::Zero();
  EXPECT_FLOAT_EQ(v.x, 0);
  EXPECT_FLOAT_EQ(v.y, 0);
  EXPECT_FLOAT_EQ(v.z, 0);
}

TEST(FVec3, ExplicitConstructor) {
  FVec3 v(1.0f, 2.0f, 3.0f);
  EXPECT_FLOAT_EQ(v.x, 1.0f);
  EXPECT_FLOAT_EQ(v.y, 2.0f);
  EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(FVec3, BroadcastConstructor) {
  FVec3 v(5.0f);
  EXPECT_FLOAT_EQ(v.x, 5.0f);
  EXPECT_FLOAT_EQ(v.y, 5.0f);
  EXPECT_FLOAT_EQ(v.z, 5.0f);
}

TEST(FVec3, ArrayConstructor) {
  float arr[] = {1.0f, 2.0f, 3.0f};
  FVec3 v(arr);
  EXPECT_FLOAT_EQ(v.x, 1.0f);
  EXPECT_FLOAT_EQ(v.y, 2.0f);
  EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(FVec3, ArrayAccess) {
  FVec3 v(1.0f, 2.0f, 3.0f);
  EXPECT_FLOAT_EQ(v.v[0], 1.0f);
  EXPECT_FLOAT_EQ(v.v[1], 2.0f);
  EXPECT_FLOAT_EQ(v.v[2], 3.0f);
}

TEST(FVec3, Addition) {
  FVec3 a(1, 2, 3);
  FVec3 b(4, 5, 6);
  EXPECT_EQ(a + b, FVec3(5, 7, 9));
}

TEST(FVec3, AddAssign) {
  FVec3 a(1, 2, 3);
  a += FVec3(4, 5, 6);
  EXPECT_EQ(a, FVec3(5, 7, 9));
}

TEST(FVec3, Subtraction) {
  FVec3 a(5, 7, 9);
  FVec3 b(1, 2, 3);
  EXPECT_EQ(a - b, FVec3(4, 5, 6));
}

TEST(FVec3, SubAssign) {
  FVec3 a(5, 7, 9);
  a -= FVec3(1, 2, 3);
  EXPECT_EQ(a, FVec3(4, 5, 6));
}

TEST(FVec3, UnaryNegate) {
  FVec3 v(1, -2, 3);
  EXPECT_EQ(-v, FVec3(-1, 2, -3));
}

TEST(FVec3, ScalarMultiply) {
  FVec3 v(1, 2, 3);
  EXPECT_EQ(v * 2.0f, FVec3(2, 4, 6));
}

TEST(FVec3, ScalarDivide) {
  FVec3 v(2, 4, 6);
  EXPECT_EQ(v / 2.0f, FVec3(1, 2, 3));
}

TEST(FVec3, MulAssign) {
  FVec3 v(1, 2, 3);
  v *= 3.0f;
  EXPECT_EQ(v, FVec3(3, 6, 9));
}

TEST(FVec3, Dot) {
  FVec3 a(1, 2, 3);
  FVec3 b(3, 2, 1);
  EXPECT_FLOAT_EQ(a.Dot(b), 10.0f);
}

TEST(FVec3, DotZero) {
  FVec3 z = FVec3::Zero();
  EXPECT_FLOAT_EQ(z.Dot(z), 0.0f);
}

TEST(FVec3, Equality) {
  FVec3 a(1, 2, 3);
  FVec3 b(1, 2, 3);
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
}

TEST(FVec3, Inequality) {
  FVec3 a(1, 2, 3);
  FVec3 b(1, 2, 4);
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a != b);
}

TEST(FVec3, Length2) {
  FVec3 v(1, 2, 3);
  EXPECT_FLOAT_EQ(v.Length2(), 14.0f);
}

TEST(FVec3, Length) {
  FVec3 v(1, 2, 3);
  EXPECT_FLOAT_EQ(v.Length(), std::sqrt(14.0f));
}

TEST(FVec3, Normalized) {
  FVec3 v(0, 3, 4);
  FVec3 n = v.Normalized();
  EXPECT_NEAR(n.Length(), 1.0f, 1e-6f);
  EXPECT_NEAR(n.x, 0.0f, 1e-6f);
  EXPECT_NEAR(n.y, 0.6f, 1e-6f);
  EXPECT_NEAR(n.z, 0.8f, 1e-6f);
}

TEST(FVec3, CrossProduct) {
  FVec3 x(1, 0, 0);
  FVec3 y(0, 1, 0);
  FVec3 z = x.Cross(y);
  EXPECT_EQ(z, FVec3(0, 0, 1));
}

TEST(FVec3, CrossProductAnticommutative) {
  FVec3 a(1, 2, 3);
  FVec3 b(4, 5, 6);
  FVec3 ab = a.Cross(b);
  FVec3 ba = b.Cross(a);
  EXPECT_EQ(ab, -ba);
}

TEST(FVec3, CrossProductSelfIsZero) {
  FVec3 v(1, 2, 3);
  EXPECT_EQ(v.Cross(v), FVec3::Zero());
}

TEST(FVec3, FreeFunction) {
  FVec3 v = FVec(1.0f, 2.0f, 3.0f);
  EXPECT_FLOAT_EQ(v.x, 1.0f);
  EXPECT_FLOAT_EQ(v.y, 2.0f);
  EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(FVec3, Cardinality) { EXPECT_EQ(FVec3::kCardinality, 3u); }

// ---------------------------------------------------------------------------
// FVec4
// ---------------------------------------------------------------------------

TEST(FVec4, Zero) {
  FVec4 v = FVec4::Zero();
  EXPECT_FLOAT_EQ(v.x, 0);
  EXPECT_FLOAT_EQ(v.y, 0);
  EXPECT_FLOAT_EQ(v.z, 0);
  EXPECT_FLOAT_EQ(v.w, 0);
}

TEST(FVec4, ExplicitConstructor) {
  FVec4 v(1, 2, 3, 4);
  EXPECT_FLOAT_EQ(v.x, 1);
  EXPECT_FLOAT_EQ(v.y, 2);
  EXPECT_FLOAT_EQ(v.z, 3);
  EXPECT_FLOAT_EQ(v.w, 4);
}

TEST(FVec4, BroadcastConstructor) {
  FVec4 v(7.0f);
  EXPECT_FLOAT_EQ(v.x, 7);
  EXPECT_FLOAT_EQ(v.y, 7);
  EXPECT_FLOAT_EQ(v.z, 7);
  EXPECT_FLOAT_EQ(v.w, 7);
}

TEST(FVec4, ArrayConstructor) {
  float arr[] = {1, 2, 3, 4};
  FVec4 v(arr);
  EXPECT_FLOAT_EQ(v.x, 1);
  EXPECT_FLOAT_EQ(v.y, 2);
  EXPECT_FLOAT_EQ(v.z, 3);
  EXPECT_FLOAT_EQ(v.w, 4);
}

TEST(FVec4, Addition) {
  FVec4 a(1, 2, 3, 4);
  FVec4 b(5, 6, 7, 8);
  EXPECT_EQ(a + b, FVec4(6, 8, 10, 12));
}

TEST(FVec4, AddAssign) {
  FVec4 a(1, 2, 3, 4);
  a += FVec4(5, 6, 7, 8);
  EXPECT_EQ(a, FVec4(6, 8, 10, 12));
}

TEST(FVec4, Subtraction) {
  FVec4 a(5, 7, 9, 11);
  FVec4 b(1, 2, 3, 4);
  EXPECT_EQ(a - b, FVec4(4, 5, 6, 7));
}

TEST(FVec4, SubAssign) {
  FVec4 a(5, 7, 9, 11);
  a -= FVec4(1, 2, 3, 4);
  EXPECT_EQ(a, FVec4(4, 5, 6, 7));
}

TEST(FVec4, UnaryNegate) {
  FVec4 v(1, -2, 3, -4);
  EXPECT_EQ(-v, FVec4(-1, 2, -3, 4));
}

TEST(FVec4, ScalarMultiply) {
  FVec4 v(1, 2, 3, 4);
  EXPECT_EQ(v * 2.0f, FVec4(2, 4, 6, 8));
}

TEST(FVec4, ScalarDivide) {
  FVec4 v(2, 4, 6, 8);
  EXPECT_EQ(v / 2.0f, FVec4(1, 2, 3, 4));
}

TEST(FVec4, MulAssign) {
  FVec4 v(1, 2, 3, 4);
  v *= 3.0f;
  EXPECT_EQ(v, FVec4(3, 6, 9, 12));
}

TEST(FVec4, Dot) {
  FVec4 a(1, 2, 3, 4);
  FVec4 b(5, 6, 7, 8);
  EXPECT_FLOAT_EQ(a.Dot(b), 70.0f);
}

TEST(FVec4, Equality) {
  FVec4 a(1, 2, 3, 4);
  EXPECT_TRUE(a == FVec4(1, 2, 3, 4));
  EXPECT_FALSE(a != FVec4(1, 2, 3, 4));
}

TEST(FVec4, Inequality) {
  FVec4 a(1, 2, 3, 4);
  EXPECT_TRUE(a != FVec4(1, 2, 3, 5));
}

TEST(FVec4, Length2) {
  FVec4 v(1, 2, 3, 4);
  EXPECT_FLOAT_EQ(v.Length2(), 30.0f);
}

TEST(FVec4, Length) {
  FVec4 v(1, 2, 3, 4);
  EXPECT_FLOAT_EQ(v.Length(), std::sqrt(30.0f));
}

TEST(FVec4, Normalized) {
  FVec4 v(1, 0, 0, 0);
  FVec4 n = v.Normalized();
  EXPECT_NEAR(n.Length(), 1.0f, 1e-6f);
  EXPECT_NEAR(n.x, 1.0f, 1e-6f);
}

TEST(FVec4, FreeFunction) {
  FVec4 v = FVec(1.0f, 2.0f, 3.0f, 4.0f);
  EXPECT_FLOAT_EQ(v.x, 1.0f);
  EXPECT_FLOAT_EQ(v.w, 4.0f);
}

TEST(FVec4, Cardinality) { EXPECT_EQ(FVec4::kCardinality, 4u); }

// ---------------------------------------------------------------------------
// DVec2
// ---------------------------------------------------------------------------

TEST(DVec2, Zero) {
  DVec2 v = DVec2::Zero();
  EXPECT_DOUBLE_EQ(v.x, 0);
  EXPECT_DOUBLE_EQ(v.y, 0);
}

TEST(DVec2, ExplicitConstructor) {
  DVec2 v(3.0, 4.0);
  EXPECT_DOUBLE_EQ(v.x, 3.0);
  EXPECT_DOUBLE_EQ(v.y, 4.0);
}

TEST(DVec2, BroadcastConstructor) {
  DVec2 v(5.0);
  EXPECT_DOUBLE_EQ(v.x, 5.0);
  EXPECT_DOUBLE_EQ(v.y, 5.0);
}

TEST(DVec2, ArrayConstructor) {
  double arr[] = {1.0, 2.0};
  DVec2 v(arr);
  EXPECT_DOUBLE_EQ(v.x, 1.0);
  EXPECT_DOUBLE_EQ(v.y, 2.0);
}

TEST(DVec2, Addition) {
  DVec2 a(1, 2);
  DVec2 b(3, 4);
  EXPECT_EQ(a + b, DVec2(4, 6));
}

TEST(DVec2, AddAssign) {
  DVec2 a(1, 2);
  a += DVec2(3, 4);
  EXPECT_EQ(a, DVec2(4, 6));
}

TEST(DVec2, Subtraction) {
  DVec2 a(5, 7);
  EXPECT_EQ(a - DVec2(2, 3), DVec2(3, 4));
}

TEST(DVec2, SubAssign) {
  DVec2 a(5, 7);
  a -= DVec2(2, 3);
  EXPECT_EQ(a, DVec2(3, 4));
}

TEST(DVec2, UnaryNegate) {
  DVec2 v(3, -4);
  EXPECT_EQ(-v, DVec2(-3, 4));
}

TEST(DVec2, ScalarMultiply) {
  DVec2 v(2, 3);
  EXPECT_EQ(v * 2.0, DVec2(4, 6));
}

TEST(DVec2, ScalarDivide) {
  DVec2 v(6, 4);
  EXPECT_EQ(v / 2.0, DVec2(3, 2));
}

TEST(DVec2, MulAssign) {
  DVec2 v(2, 3);
  v *= 3.0;
  EXPECT_EQ(v, DVec2(6, 9));
}

TEST(DVec2, Dot) {
  DVec2 a(1, 2);
  DVec2 b(3, 4);
  EXPECT_DOUBLE_EQ(a.Dot(b), 11.0);
}

TEST(DVec2, Equality) {
  DVec2 a(1, 2);
  EXPECT_TRUE(a == DVec2(1, 2));
  EXPECT_FALSE(a != DVec2(1, 2));
}

TEST(DVec2, Inequality) {
  DVec2 a(1, 2);
  EXPECT_TRUE(a != DVec2(1, 3));
}

TEST(DVec2, Length) {
  DVec2 v(3, 4);
  EXPECT_DOUBLE_EQ(v.Length2(), 25.0);
  EXPECT_DOUBLE_EQ(v.Length(), 5.0);
}

TEST(DVec2, Normalized) {
  DVec2 v(3, 4);
  DVec2 n = v.Normalized();
  EXPECT_NEAR(n.Length(), 1.0, 1e-10);
  EXPECT_NEAR(n.x, 0.6, 1e-10);
  EXPECT_NEAR(n.y, 0.8, 1e-10);
}

TEST(DVec2, FreeFunction) {
  DVec2 v = DVec(1.0, 2.0);
  EXPECT_DOUBLE_EQ(v.x, 1.0);
  EXPECT_DOUBLE_EQ(v.y, 2.0);
}

TEST(DVec2, Cardinality) { EXPECT_EQ(DVec2::kCardinality, 2u); }

// ---------------------------------------------------------------------------
// DVec3
// ---------------------------------------------------------------------------

TEST(DVec3, Zero) {
  DVec3 v = DVec3::Zero();
  EXPECT_DOUBLE_EQ(v.x, 0);
  EXPECT_DOUBLE_EQ(v.y, 0);
  EXPECT_DOUBLE_EQ(v.z, 0);
}

TEST(DVec3, ExplicitConstructor) {
  DVec3 v(1, 2, 3);
  EXPECT_DOUBLE_EQ(v.x, 1);
  EXPECT_DOUBLE_EQ(v.y, 2);
  EXPECT_DOUBLE_EQ(v.z, 3);
}

TEST(DVec3, BroadcastConstructor) {
  DVec3 v(5.0);
  EXPECT_DOUBLE_EQ(v.x, 5);
  EXPECT_DOUBLE_EQ(v.y, 5);
  EXPECT_DOUBLE_EQ(v.z, 5);
}

TEST(DVec3, ArrayConstructor) {
  double arr[] = {1, 2, 3};
  DVec3 v(arr);
  EXPECT_DOUBLE_EQ(v.x, 1);
  EXPECT_DOUBLE_EQ(v.y, 2);
  EXPECT_DOUBLE_EQ(v.z, 3);
}

TEST(DVec3, Addition) {
  DVec3 a(1, 2, 3);
  DVec3 b(4, 5, 6);
  EXPECT_EQ(a + b, DVec3(5, 7, 9));
}

TEST(DVec3, AddAssign) {
  DVec3 a(1, 2, 3);
  a += DVec3(4, 5, 6);
  EXPECT_EQ(a, DVec3(5, 7, 9));
}

TEST(DVec3, Subtraction) {
  DVec3 a(5, 7, 9);
  EXPECT_EQ(a - DVec3(1, 2, 3), DVec3(4, 5, 6));
}

TEST(DVec3, SubAssign) {
  DVec3 a(5, 7, 9);
  a -= DVec3(1, 2, 3);
  EXPECT_EQ(a, DVec3(4, 5, 6));
}

TEST(DVec3, UnaryNegate) {
  DVec3 v(1, -2, 3);
  EXPECT_EQ(-v, DVec3(-1, 2, -3));
}

TEST(DVec3, ScalarMultiply) {
  DVec3 v(1, 2, 3);
  EXPECT_EQ(v * 2.0, DVec3(2, 4, 6));
}

TEST(DVec3, ScalarDivide) {
  DVec3 v(2, 4, 6);
  EXPECT_EQ(v / 2.0, DVec3(1, 2, 3));
}

TEST(DVec3, MulAssign) {
  DVec3 v(1, 2, 3);
  v *= 3.0;
  EXPECT_EQ(v, DVec3(3, 6, 9));
}

TEST(DVec3, Dot) {
  DVec3 a(1, 2, 3);
  DVec3 b(3, 2, 1);
  EXPECT_DOUBLE_EQ(a.Dot(b), 10.0);
}

TEST(DVec3, Equality) {
  DVec3 a(1, 2, 3);
  EXPECT_TRUE(a == DVec3(1, 2, 3));
  EXPECT_FALSE(a != DVec3(1, 2, 3));
}

TEST(DVec3, Inequality) {
  DVec3 a(1, 2, 3);
  EXPECT_TRUE(a != DVec3(1, 2, 4));
}

TEST(DVec3, Length) {
  DVec3 v(1, 2, 3);
  EXPECT_DOUBLE_EQ(v.Length2(), 14.0);
  EXPECT_DOUBLE_EQ(v.Length(), std::sqrt(14.0));
}

TEST(DVec3, Normalized) {
  DVec3 v(0, 3, 4);
  DVec3 n = v.Normalized();
  EXPECT_NEAR(n.Length(), 1.0, 1e-10);
}

TEST(DVec3, CrossProduct) {
  DVec3 x(1, 0, 0);
  DVec3 y(0, 1, 0);
  EXPECT_EQ(x.Cross(y), DVec3(0, 0, 1));
}

TEST(DVec3, CrossProductAnticommutative) {
  DVec3 a(1, 2, 3);
  DVec3 b(4, 5, 6);
  EXPECT_EQ(a.Cross(b), -b.Cross(a));
}

TEST(DVec3, CrossProductSelfIsZero) {
  DVec3 v(1, 2, 3);
  EXPECT_EQ(v.Cross(v), DVec3::Zero());
}

TEST(DVec3, FreeFunction) {
  DVec3 v = DVec(1.0, 2.0, 3.0);
  EXPECT_DOUBLE_EQ(v.z, 3.0);
}

TEST(DVec3, Cardinality) { EXPECT_EQ(DVec3::kCardinality, 3u); }

// ---------------------------------------------------------------------------
// DVec4
// ---------------------------------------------------------------------------

TEST(DVec4, Zero) {
  DVec4 v = DVec4::Zero();
  EXPECT_DOUBLE_EQ(v.x, 0);
  EXPECT_DOUBLE_EQ(v.y, 0);
  EXPECT_DOUBLE_EQ(v.z, 0);
  EXPECT_DOUBLE_EQ(v.w, 0);
}

TEST(DVec4, ExplicitConstructor) {
  DVec4 v(1, 2, 3, 4);
  EXPECT_DOUBLE_EQ(v.x, 1);
  EXPECT_DOUBLE_EQ(v.w, 4);
}

TEST(DVec4, BroadcastConstructor) {
  DVec4 v(7.0);
  EXPECT_DOUBLE_EQ(v.x, 7);
  EXPECT_DOUBLE_EQ(v.w, 7);
}

TEST(DVec4, ArrayConstructor) {
  double arr[] = {1, 2, 3, 4};
  DVec4 v(arr);
  EXPECT_DOUBLE_EQ(v.x, 1);
  EXPECT_DOUBLE_EQ(v.w, 4);
}

TEST(DVec4, Addition) {
  DVec4 a(1, 2, 3, 4);
  DVec4 b(5, 6, 7, 8);
  EXPECT_EQ(a + b, DVec4(6, 8, 10, 12));
}

TEST(DVec4, AddAssign) {
  DVec4 a(1, 2, 3, 4);
  a += DVec4(5, 6, 7, 8);
  EXPECT_EQ(a, DVec4(6, 8, 10, 12));
}

TEST(DVec4, Subtraction) {
  DVec4 a(5, 7, 9, 11);
  EXPECT_EQ(a - DVec4(1, 2, 3, 4), DVec4(4, 5, 6, 7));
}

TEST(DVec4, SubAssign) {
  DVec4 a(5, 7, 9, 11);
  a -= DVec4(1, 2, 3, 4);
  EXPECT_EQ(a, DVec4(4, 5, 6, 7));
}

TEST(DVec4, UnaryNegate) {
  DVec4 v(1, -2, 3, -4);
  EXPECT_EQ(-v, DVec4(-1, 2, -3, 4));
}

TEST(DVec4, ScalarMultiply) {
  DVec4 v(1, 2, 3, 4);
  EXPECT_EQ(v * 2.0, DVec4(2, 4, 6, 8));
}

TEST(DVec4, ScalarDivide) {
  DVec4 v(2, 4, 6, 8);
  EXPECT_EQ(v / 2.0, DVec4(1, 2, 3, 4));
}

TEST(DVec4, MulAssign) {
  DVec4 v(1, 2, 3, 4);
  v *= 3.0;
  EXPECT_EQ(v, DVec4(3, 6, 9, 12));
}

TEST(DVec4, Dot) {
  DVec4 a(1, 2, 3, 4);
  DVec4 b(5, 6, 7, 8);
  EXPECT_DOUBLE_EQ(a.Dot(b), 70.0);
}

TEST(DVec4, Equality) {
  DVec4 a(1, 2, 3, 4);
  EXPECT_TRUE(a == DVec4(1, 2, 3, 4));
}

TEST(DVec4, Inequality) {
  DVec4 a(1, 2, 3, 4);
  EXPECT_TRUE(a != DVec4(1, 2, 3, 5));
}

TEST(DVec4, Length) {
  DVec4 v(1, 2, 3, 4);
  EXPECT_DOUBLE_EQ(v.Length2(), 30.0);
  EXPECT_DOUBLE_EQ(v.Length(), std::sqrt(30.0));
}

TEST(DVec4, Normalized) {
  DVec4 v(1, 0, 0, 0);
  DVec4 n = v.Normalized();
  EXPECT_NEAR(n.Length(), 1.0, 1e-10);
}

TEST(DVec4, FreeFunction) {
  DVec4 v = DVec(1.0, 2.0, 3.0, 4.0);
  EXPECT_DOUBLE_EQ(v.w, 4.0);
}

TEST(DVec4, Cardinality) { EXPECT_EQ(DVec4::kCardinality, 4u); }

// ---------------------------------------------------------------------------
// IVec2
// ---------------------------------------------------------------------------

TEST(IVec2, Zero) {
  IVec2 v = IVec2::Zero();
  EXPECT_EQ(v.x, 0);
  EXPECT_EQ(v.y, 0);
}

TEST(IVec2, ExplicitConstructor) {
  IVec2 v(3, 4);
  EXPECT_EQ(v.x, 3);
  EXPECT_EQ(v.y, 4);
}

TEST(IVec2, BroadcastConstructor) {
  IVec2 v(5);
  EXPECT_EQ(v.x, 5);
  EXPECT_EQ(v.y, 5);
}

TEST(IVec2, ArrayConstructor) {
  int arr[] = {1, 2};
  IVec2 v(arr);
  EXPECT_EQ(v.x, 1);
  EXPECT_EQ(v.y, 2);
}

TEST(IVec2, Addition) {
  IVec2 a(1, 2);
  IVec2 b(3, 4);
  EXPECT_EQ(a + b, IVec2(4, 6));
}

TEST(IVec2, AddAssign) {
  IVec2 a(1, 2);
  a += IVec2(3, 4);
  EXPECT_EQ(a, IVec2(4, 6));
}

TEST(IVec2, Subtraction) {
  IVec2 a(5, 7);
  EXPECT_EQ(a - IVec2(2, 3), IVec2(3, 4));
}

TEST(IVec2, SubAssign) {
  IVec2 a(5, 7);
  a -= IVec2(2, 3);
  EXPECT_EQ(a, IVec2(3, 4));
}

TEST(IVec2, UnaryNegate) {
  IVec2 v(3, -4);
  EXPECT_EQ(-v, IVec2(-3, 4));
}

TEST(IVec2, ScalarMultiply) {
  IVec2 v(2, 3);
  EXPECT_EQ(v * 2, IVec2(4, 6));
}

TEST(IVec2, ScalarDivide) {
  IVec2 v(6, 4);
  EXPECT_EQ(v / 2, IVec2(3, 2));
}

TEST(IVec2, MulAssign) {
  IVec2 v(2, 3);
  v *= 3;
  EXPECT_EQ(v, IVec2(6, 9));
}

TEST(IVec2, Dot) {
  IVec2 a(1, 2);
  IVec2 b(3, 4);
  EXPECT_EQ(a.Dot(b), 11);
}

TEST(IVec2, Equality) {
  IVec2 a(1, 2);
  EXPECT_TRUE(a == IVec2(1, 2));
  EXPECT_FALSE(a != IVec2(1, 2));
}

TEST(IVec2, Inequality) {
  IVec2 a(1, 2);
  EXPECT_TRUE(a != IVec2(1, 3));
}

TEST(IVec2, Length2) {
  IVec2 v(3, 4);
  EXPECT_EQ(v.Length2(), 25);
}

TEST(IVec2, FreeFunction) {
  IVec2 v = IVec(1, 2);
  EXPECT_EQ(v.x, 1);
  EXPECT_EQ(v.y, 2);
}

TEST(IVec2, Cardinality) { EXPECT_EQ(IVec2::kCardinality, 2u); }

// ---------------------------------------------------------------------------
// IVec3
// ---------------------------------------------------------------------------

TEST(IVec3, Zero) {
  IVec3 v = IVec3::Zero();
  EXPECT_EQ(v.x, 0);
  EXPECT_EQ(v.y, 0);
  EXPECT_EQ(v.z, 0);
}

TEST(IVec3, ExplicitConstructor) {
  IVec3 v(1, 2, 3);
  EXPECT_EQ(v.x, 1);
  EXPECT_EQ(v.y, 2);
  EXPECT_EQ(v.z, 3);
}

TEST(IVec3, BroadcastConstructor) {
  IVec3 v(5);
  EXPECT_EQ(v.x, 5);
  EXPECT_EQ(v.y, 5);
  EXPECT_EQ(v.z, 5);
}

TEST(IVec3, ArrayConstructor) {
  int arr[] = {1, 2, 3};
  IVec3 v(arr);
  EXPECT_EQ(v.x, 1);
  EXPECT_EQ(v.y, 2);
  EXPECT_EQ(v.z, 3);
}

TEST(IVec3, Addition) {
  IVec3 a(1, 2, 3);
  IVec3 b(4, 5, 6);
  EXPECT_EQ(a + b, IVec3(5, 7, 9));
}

TEST(IVec3, AddAssign) {
  IVec3 a(1, 2, 3);
  a += IVec3(4, 5, 6);
  EXPECT_EQ(a, IVec3(5, 7, 9));
}

TEST(IVec3, Subtraction) {
  IVec3 a(5, 7, 9);
  EXPECT_EQ(a - IVec3(1, 2, 3), IVec3(4, 5, 6));
}

TEST(IVec3, SubAssign) {
  IVec3 a(5, 7, 9);
  a -= IVec3(1, 2, 3);
  EXPECT_EQ(a, IVec3(4, 5, 6));
}

TEST(IVec3, UnaryNegate) {
  IVec3 v(1, -2, 3);
  EXPECT_EQ(-v, IVec3(-1, 2, -3));
}

TEST(IVec3, ScalarMultiply) {
  IVec3 v(1, 2, 3);
  EXPECT_EQ(v * 2, IVec3(2, 4, 6));
}

TEST(IVec3, ScalarDivide) {
  IVec3 v(2, 4, 6);
  EXPECT_EQ(v / 2, IVec3(1, 2, 3));
}

TEST(IVec3, MulAssign) {
  IVec3 v(1, 2, 3);
  v *= 3;
  EXPECT_EQ(v, IVec3(3, 6, 9));
}

TEST(IVec3, Dot) {
  IVec3 a(1, 2, 3);
  IVec3 b(3, 2, 1);
  EXPECT_EQ(a.Dot(b), 10);
}

TEST(IVec3, Equality) {
  IVec3 a(1, 2, 3);
  EXPECT_TRUE(a == IVec3(1, 2, 3));
  EXPECT_FALSE(a != IVec3(1, 2, 3));
}

TEST(IVec3, Inequality) {
  IVec3 a(1, 2, 3);
  EXPECT_TRUE(a != IVec3(1, 2, 4));
}

TEST(IVec3, Length2) {
  IVec3 v(1, 2, 3);
  EXPECT_EQ(v.Length2(), 14);
}

TEST(IVec3, CrossProduct) {
  IVec3 x(1, 0, 0);
  IVec3 y(0, 1, 0);
  EXPECT_EQ(x.Cross(y), IVec3(0, 0, 1));
}

TEST(IVec3, CrossProductAnticommutative) {
  IVec3 a(1, 2, 3);
  IVec3 b(4, 5, 6);
  EXPECT_EQ(a.Cross(b), -b.Cross(a));
}

TEST(IVec3, CrossProductSelfIsZero) {
  IVec3 v(1, 2, 3);
  EXPECT_EQ(v.Cross(v), IVec3::Zero());
}

TEST(IVec3, FreeFunction) {
  IVec3 v = IVec(1, 2, 3);
  EXPECT_EQ(v.z, 3);
}

TEST(IVec3, Cardinality) { EXPECT_EQ(IVec3::kCardinality, 3u); }

// ---------------------------------------------------------------------------
// IVec4
// ---------------------------------------------------------------------------

TEST(IVec4, Zero) {
  IVec4 v = IVec4::Zero();
  EXPECT_EQ(v.x, 0);
  EXPECT_EQ(v.y, 0);
  EXPECT_EQ(v.z, 0);
  EXPECT_EQ(v.w, 0);
}

TEST(IVec4, ExplicitConstructor) {
  IVec4 v(1, 2, 3, 4);
  EXPECT_EQ(v.x, 1);
  EXPECT_EQ(v.y, 2);
  EXPECT_EQ(v.z, 3);
  EXPECT_EQ(v.w, 4);
}

TEST(IVec4, BroadcastConstructor) {
  IVec4 v(7);
  EXPECT_EQ(v.x, 7);
  EXPECT_EQ(v.w, 7);
}

TEST(IVec4, ArrayConstructor) {
  int arr[] = {1, 2, 3, 4};
  IVec4 v(arr);
  EXPECT_EQ(v.x, 1);
  EXPECT_EQ(v.w, 4);
}

TEST(IVec4, Addition) {
  IVec4 a(1, 2, 3, 4);
  IVec4 b(5, 6, 7, 8);
  EXPECT_EQ(a + b, IVec4(6, 8, 10, 12));
}

TEST(IVec4, AddAssign) {
  IVec4 a(1, 2, 3, 4);
  a += IVec4(5, 6, 7, 8);
  EXPECT_EQ(a, IVec4(6, 8, 10, 12));
}

TEST(IVec4, Subtraction) {
  IVec4 a(5, 7, 9, 11);
  EXPECT_EQ(a - IVec4(1, 2, 3, 4), IVec4(4, 5, 6, 7));
}

TEST(IVec4, SubAssign) {
  IVec4 a(5, 7, 9, 11);
  a -= IVec4(1, 2, 3, 4);
  EXPECT_EQ(a, IVec4(4, 5, 6, 7));
}

TEST(IVec4, UnaryNegate) {
  IVec4 v(1, -2, 3, -4);
  EXPECT_EQ(-v, IVec4(-1, 2, -3, 4));
}

TEST(IVec4, ScalarMultiply) {
  IVec4 v(1, 2, 3, 4);
  EXPECT_EQ(v * 2, IVec4(2, 4, 6, 8));
}

TEST(IVec4, ScalarDivide) {
  IVec4 v(2, 4, 6, 8);
  EXPECT_EQ(v / 2, IVec4(1, 2, 3, 4));
}

TEST(IVec4, MulAssign) {
  IVec4 v(1, 2, 3, 4);
  v *= 3;
  EXPECT_EQ(v, IVec4(3, 6, 9, 12));
}

TEST(IVec4, Dot) {
  IVec4 a(1, 2, 3, 4);
  IVec4 b(5, 6, 7, 8);
  EXPECT_EQ(a.Dot(b), 70);
}

TEST(IVec4, Equality) {
  IVec4 a(1, 2, 3, 4);
  EXPECT_TRUE(a == IVec4(1, 2, 3, 4));
  EXPECT_FALSE(a != IVec4(1, 2, 3, 4));
}

TEST(IVec4, Inequality) {
  IVec4 a(1, 2, 3, 4);
  EXPECT_TRUE(a != IVec4(1, 2, 3, 5));
}

TEST(IVec4, Length2) {
  IVec4 v(1, 2, 3, 4);
  EXPECT_EQ(v.Length2(), 30);
}

TEST(IVec4, FreeFunction) {
  IVec4 v = IVec(1, 2, 3, 4);
  EXPECT_EQ(v.w, 4);
}

TEST(IVec4, Cardinality) { EXPECT_EQ(IVec4::kCardinality, 4u); }

TEST(Tests, Bits) {
  EXPECT_EQ(NextPow2(1), 1);
  EXPECT_EQ(NextPow2(13), 16);
  EXPECT_EQ(NextPow2(2), 2);
}

TEST(Tests, FixedStringBufferTest) {
  FixedStringBuffer<16> buffer;
  EXPECT_STREQ(buffer.str(), "");
  EXPECT_TRUE(buffer.empty());
  buffer.Append("foo ");
  buffer.Append("bar");
  EXPECT_STREQ(buffer.str(), "foo bar");
  EXPECT_EQ(buffer.size(), 7);
  EXPECT_FALSE(buffer.empty());
  buffer.Append(" bar ");
  buffer.Append("bar ");
  buffer.Append("bar ");
  buffer.Append("bar ");
  buffer.Append("bar ");
  EXPECT_STREQ(buffer.str(), "foo bar bar bar ");
  EXPECT_EQ(buffer.size(), 16);
  EXPECT_FALSE(buffer.empty());
}

TEST(Tests, Dictionary) {
  Dictionary<int> dictionary(SystemAllocator::Instance());
  EXPECT_FALSE(dictionary.Contains("foo"));
  EXPECT_FALSE(dictionary.Contains("bar"));
  dictionary.Insert("foo", 1);
  int value;
  EXPECT_TRUE(dictionary.Lookup("foo", &value));
  EXPECT_EQ(value, 1);
  EXPECT_THAT(dictionary.LookupOrDie("foo"), 1);
  EXPECT_TRUE(dictionary.Contains("foo"));
  EXPECT_FALSE(dictionary.Contains("bar"));
  dictionary.Insert("foo", 2);
  EXPECT_TRUE(dictionary.Contains("foo"));
  EXPECT_FALSE(dictionary.Contains("bar"));
  EXPECT_THAT(dictionary.LookupOrDie("foo"), 2);
}

TEST(Tests, StringTable) {
  auto s = std::make_unique<StringTable>();
  uint32_t handle1 = s->Intern("foo");
  uint32_t handle2 = s->Intern("bar");
  EXPECT_NE(handle1, handle2);
  uint32_t handle3 = s->Intern("foo");
  EXPECT_EQ(handle1, s->Handle("foo"));
  EXPECT_NE(handle2, s->Handle("foo"));
  EXPECT_EQ(handle2, s->Handle("bar"));
  EXPECT_EQ(handle1, handle3);
}

TEST(Tests, BlockAllocator) {
  BlockAllocator<uint32_t> pool(SystemAllocator::Instance(), 2);
  uint32_t* p = pool.AllocBlock();
  uint32_t* q = pool.AllocBlock();
  EXPECT_NE(p, q);
  pool.DeallocBlock(q);
  uint32_t* r = pool.AllocBlock();
  EXPECT_EQ(q, r);
}

TEST(Tests, ShardedFreeListAllocator) {
  void* buffer;
  ASSERT_TRUE(posix_memalign(&buffer, kMaxAlign, Megabytes(128)) == 0);
  DEFER([&] { std::free(buffer); });
  auto* a =
      new ShardedFreeListAllocator(SystemAllocator::Instance(), Megabytes(128));
  DEFER([&] { delete a; });
  auto* p = a->Alloc(1, 16);
  ASSERT_NE(p, nullptr);
  a->Dealloc(p, 1);
  std::array<void*, 1000> ptrs;
  for (size_t i = 0; i < 1000; ++i) {
    ptrs[i] = a->Alloc(1, 16);
  }
  for (size_t i = 0; i < 1000; ++i) {
    a->Dealloc(ptrs[i], 1);
  }
  for (size_t i = 0; i < 1000; ++i) {
    ptrs[i] = a->Alloc(513, 16);
  }
  for (size_t i = 0; i < 1000; ++i) {
    a->Dealloc(ptrs[i], 513);
  }
}

}  // namespace G
