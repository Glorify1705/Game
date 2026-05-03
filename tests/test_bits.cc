#include "bits.h"
#include "gtest/gtest.h"

namespace G {

TEST(BitsTest, NextPow2PowersOfTwo) {
  EXPECT_EQ(NextPow2(1), 1);
  EXPECT_EQ(NextPow2(2), 2);
  EXPECT_EQ(NextPow2(4), 4);
  EXPECT_EQ(NextPow2(64), 64);
  EXPECT_EQ(NextPow2(1024), 1024);
}

TEST(BitsTest, NextPow2NonPowers) {
  EXPECT_EQ(NextPow2(3), 4);
  EXPECT_EQ(NextPow2(5), 8);
  EXPECT_EQ(NextPow2(13), 16);
  EXPECT_EQ(NextPow2(100), 128);
  EXPECT_EQ(NextPow2(1000), 1024);
  EXPECT_EQ(NextPow2(1025), 2048);
}

TEST(BitsTest, NextPow2EdgeCases) {
  EXPECT_EQ(NextPow2(0), 1);
  EXPECT_EQ(NextPow2(1), 1);
}

TEST(BitsTest, Log2PowersOfTwo) {
  EXPECT_EQ(Log2(1), 1);
  EXPECT_EQ(Log2(2), 2);
  EXPECT_EQ(Log2(4), 3);
  EXPECT_EQ(Log2(8), 4);
  EXPECT_EQ(Log2(16), 5);
  EXPECT_EQ(Log2(1024), 11);
}

TEST(BitsTest, Log2NonPowers) {
  EXPECT_EQ(Log2(3), 2);
  EXPECT_EQ(Log2(5), 3);
  EXPECT_EQ(Log2(7), 3);
  EXPECT_EQ(Log2(9), 4);
  EXPECT_EQ(Log2(255), 8);
  EXPECT_EQ(Log2(256), 9);
}

TEST(BitsTest, AlignBasic) {
  EXPECT_EQ(Align(0, 4), 0);
  EXPECT_EQ(Align(1, 4), 4);
  EXPECT_EQ(Align(3, 4), 4);
  EXPECT_EQ(Align(4, 4), 4);
  EXPECT_EQ(Align(5, 4), 8);
}

TEST(BitsTest, AlignVariousAlignments) {
  EXPECT_EQ(Align(1, 8), 8);
  EXPECT_EQ(Align(8, 8), 8);
  EXPECT_EQ(Align(9, 8), 16);
  EXPECT_EQ(Align(1, 16), 16);
  EXPECT_EQ(Align(15, 16), 16);
  EXPECT_EQ(Align(16, 16), 16);
  EXPECT_EQ(Align(17, 16), 32);
}

TEST(BitsTest, AlignAlreadyAligned) {
  EXPECT_EQ(Align(0, 1), 0);
  EXPECT_EQ(Align(7, 1), 7);
  EXPECT_EQ(Align(256, 256), 256);
}

}  // namespace G
