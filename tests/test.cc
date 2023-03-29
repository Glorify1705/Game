#include "array.h"
#include "gtest/gtest.h"
#include "lookup_table.h"

namespace G {

TEST(Tests, FixedArray) {
  FixedArray<int, 3> array;
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
  FixedArray<int, 3, SystemAllocator> array;
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

TEST(Tests, LookupTable) {
  LookupTable<int> table;
  table.Insert("foo", 1);
  table.Insert("bar", 2);
  EXPECT_EQ(table.size(), 2);
  int val;
  ASSERT_TRUE(table.Lookup("foo", &val));
  EXPECT_EQ(val, 1);
}

}  // namespace G