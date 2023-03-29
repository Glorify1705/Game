#include "array.h"
#include "gtest/gtest.h"

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
}

}  // namespace G