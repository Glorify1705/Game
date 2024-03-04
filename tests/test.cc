#include "array.h"
#include "bits.h"
#include "dictionary.h"
#include "gmock/gmock-matchers.h"
#include "gport-shim.h"
#include "gtest/gtest-matchers.h"
#include "lookup_table.h"
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
  for (size_t i = 0; i < array.size(); ++i) {
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

TEST(Tests, Vectors) {
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

TEST(Tests, LookupTable) {
  LookupTable<int> table(SystemAllocator::Instance());
  table.Insert("foo", 1);
  table.Insert("bar", 2);
  EXPECT_EQ(table.size(), 2);
  int val;
  ASSERT_TRUE(table.Lookup("foo", &val));
  EXPECT_EQ(val, 1);
  ASSERT_FALSE(table.Lookup("baz", &val));
  table.Insert("foo", 3);
  ASSERT_TRUE(table.Lookup("foo", &val));
  EXPECT_EQ(val, 3);
}

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

}  // namespace G