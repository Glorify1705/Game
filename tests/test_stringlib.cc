#include "stringlib.h"
#include "gtest/gtest.h"

namespace G {

TEST(StringLibTest, HasPrefixTrue) {
  EXPECT_TRUE(HasPrefix("hello world", "hello"));
  EXPECT_TRUE(HasPrefix("hello", "hello"));
  EXPECT_TRUE(HasPrefix("hello", ""));
  EXPECT_TRUE(HasPrefix("a", "a"));
}

TEST(StringLibTest, HasPrefixFalse) {
  EXPECT_FALSE(HasPrefix("hello", "world"));
  EXPECT_FALSE(HasPrefix("hello", "hello world"));
  EXPECT_FALSE(HasPrefix("", "a"));
  EXPECT_FALSE(HasPrefix("hel", "hello"));
}

TEST(StringLibTest, ConsumePrefixMatch) {
  std::string_view s = "hello world";
  EXPECT_TRUE(ConsumePrefix(&s, "hello "));
  EXPECT_EQ(s, "world");
}

TEST(StringLibTest, ConsumePrefixNoMatch) {
  std::string_view s = "hello world";
  EXPECT_FALSE(ConsumePrefix(&s, "goodbye"));
  EXPECT_EQ(s, "hello world");
}

TEST(StringLibTest, ConsumePrefixEmpty) {
  std::string_view s = "hello";
  EXPECT_TRUE(ConsumePrefix(&s, ""));
  EXPECT_EQ(s, "hello");
}

TEST(StringLibTest, HasSuffixTrue) {
  EXPECT_TRUE(HasSuffix("hello world", "world"));
  EXPECT_TRUE(HasSuffix("hello", "hello"));
  EXPECT_TRUE(HasSuffix("hello", ""));
  EXPECT_TRUE(HasSuffix("test.lua", ".lua"));
}

TEST(StringLibTest, HasSuffixFalse) {
  EXPECT_FALSE(HasSuffix("hello", "world"));
  EXPECT_FALSE(HasSuffix("hello", "hello world"));
  EXPECT_FALSE(HasSuffix("", "a"));
  EXPECT_FALSE(HasSuffix("test.lua", ".cc"));
}

TEST(StringLibTest, ConsumeSuffixMatch) {
  std::string_view s = "test.lua";
  EXPECT_TRUE(ConsumeSuffix(&s, ".lua"));
  EXPECT_EQ(s, "test");
}

TEST(StringLibTest, ConsumeSuffixNoMatch) {
  std::string_view s = "test.lua";
  EXPECT_FALSE(ConsumeSuffix(&s, ".cc"));
  EXPECT_EQ(s, "test.lua");
}

TEST(StringLibTest, PrintDoubleBasic) {
  char buf[64];
  PrintDouble(3.14, buf, sizeof(buf));
  EXPECT_STREQ(buf, "3.14");
}

TEST(StringLibTest, PrintDoubleWholeNumber) {
  char buf[64];
  PrintDouble(42.0, buf, sizeof(buf));
  EXPECT_STREQ(buf, "42.00");
}

TEST(StringLibTest, PrintDoubleNegative) {
  char buf[64];
  PrintDouble(-1.5, buf, sizeof(buf));
  EXPECT_STREQ(buf, "-1.50");
}

TEST(StringLibTest, PrintDoubleZero) {
  char buf[64];
  PrintDouble(0.0, buf, sizeof(buf));
  EXPECT_STREQ(buf, "0.00");
}

TEST(StringLibTest, PrintDoubleTruncation) {
  char buf[64];
  // PrintDouble uses 2 decimal places.
  PrintDouble(1.999, buf, sizeof(buf));
  EXPECT_STREQ(buf, "2.00");
}

}  // namespace G
