#include "color.h"
#include "gtest/gtest.h"

namespace G {

TEST(ColorTest, BasicColors) {
  auto red = ColorFromTable("red");
  ASSERT_FALSE(red.is_error());
  EXPECT_EQ(red.value().r, 255);
  EXPECT_EQ(red.value().g, 0);
  EXPECT_EQ(red.value().b, 0);
  EXPECT_EQ(red.value().a, 255);

  auto green = ColorFromTable("green");
  ASSERT_FALSE(green.is_error());
  EXPECT_EQ(green.value().r, 0);
  EXPECT_EQ(green.value().g, 255);
  EXPECT_EQ(green.value().b, 0);

  auto blue = ColorFromTable("blue");
  ASSERT_FALSE(blue.is_error());
  EXPECT_EQ(blue.value().r, 0);
  EXPECT_EQ(blue.value().g, 0);
  EXPECT_EQ(blue.value().b, 255);
}

TEST(ColorTest, BlackAndWhite) {
  auto white = ColorFromTable("white");
  ASSERT_FALSE(white.is_error());
  EXPECT_EQ(white.value().r, 255);
  EXPECT_EQ(white.value().g, 255);
  EXPECT_EQ(white.value().b, 255);
  EXPECT_EQ(white.value().a, 255);

  auto black = ColorFromTable("black");
  ASSERT_FALSE(black.is_error());
  EXPECT_EQ(black.value().r, 0);
  EXPECT_EQ(black.value().g, 0);
  EXPECT_EQ(black.value().b, 0);
  EXPECT_EQ(black.value().a, 255);
}

TEST(ColorTest, UnknownColorReturnsError) {
  auto result = ColorFromTable("notacolor");
  EXPECT_TRUE(result.is_error());
}

TEST(ColorTest, EmptyStringReturnsError) {
  auto result = ColorFromTable("");
  EXPECT_TRUE(result.is_error());
}

TEST(ColorTest, NamedColors) {
  auto cyan = ColorFromTable("cyan");
  ASSERT_FALSE(cyan.is_error());
  EXPECT_EQ(cyan.value().r, 0);
  EXPECT_EQ(cyan.value().g, 255);
  EXPECT_EQ(cyan.value().b, 255);

  auto yellow = ColorFromTable("yellow");
  ASSERT_FALSE(yellow.is_error());
  EXPECT_EQ(yellow.value().r, 255);
  EXPECT_EQ(yellow.value().g, 255);
  EXPECT_EQ(yellow.value().b, 20);
}

TEST(ColorTest, AlphaIsAlways255) {
  // All table colors should have full alpha.
  for (const char* name : {"red", "green", "blue", "cyan", "yellow", "white",
                            "black", "orange", "purple", "pink"}) {
    auto result = ColorFromTable(name);
    if (!result.is_error()) {
      EXPECT_EQ(result.value().a, 255) << "Color: " << name;
    }
  }
}

TEST(ColorTest, ToFloat) {
  Color c{128, 64, 255, 200};
  FVec4 f = c.ToFloat();
  EXPECT_NEAR(f.x, 128.0 / 255.0, 1e-3);
  EXPECT_NEAR(f.y, 64.0 / 255.0, 1e-3);
  EXPECT_NEAR(f.z, 1.0, 1e-3);
  EXPECT_NEAR(f.w, 200.0 / 255.0, 1e-3);
}

TEST(ColorTest, StaticConstructors) {
  Color w = Color::White();
  EXPECT_EQ(w.r, 255);
  EXPECT_EQ(w.g, 255);
  EXPECT_EQ(w.b, 255);
  EXPECT_EQ(w.a, 255);

  Color b = Color::Black();
  EXPECT_EQ(b.r, 0);
  EXPECT_EQ(b.g, 0);
  EXPECT_EQ(b.b, 0);
  EXPECT_EQ(b.a, 255);

  Color z = Color::Zero();
  EXPECT_EQ(z.r, 0);
  EXPECT_EQ(z.g, 0);
  EXPECT_EQ(z.b, 0);
  EXPECT_EQ(z.a, 0);
}

}  // namespace G
