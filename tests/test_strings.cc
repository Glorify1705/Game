#include "gmock/gmock-matchers.h"
#include "string_table.h"
#include "stringlib.h"
#include "test_fixture.h"

namespace G {

class StringBufferTest : public AllocTest {};

TEST_F(StringBufferTest, Basic) {
  FixedStringBuffer<16> buffer;
  EXPECT_STREQ(buffer.str(), "");
  EXPECT_TRUE(buffer.empty());
  buffer.Append("foo ");
  buffer.Append("bar");
  EXPECT_STREQ(buffer.str(), "foo bar");
  EXPECT_EQ(buffer.size(), 7);
  EXPECT_FALSE(buffer.empty());
}

TEST_F(StringBufferTest, Truncation) {
  FixedStringBuffer<16> buffer(kTruncating);
  buffer.Append("foo ");
  buffer.Append("bar");
  buffer.Append(" bar ");
  buffer.Append("bar ");
  buffer.Append("bar ");
  buffer.Append("bar ");
  buffer.Append("bar ");
  EXPECT_STREQ(buffer.str(), "foo bar bar bar ");
  EXPECT_EQ(buffer.size(), 16);
  EXPECT_FALSE(buffer.empty());
}

TEST_F(StringBufferTest, Growable) {
  FixedStringBuffer<8> buffer(alloc);
  buffer.Append("hello");
  EXPECT_STREQ(buffer.str(), "hello");
  EXPECT_EQ(buffer.size(), 5);
  // This would truncate without an allocator, but grows instead.
  buffer.Append(" world, this is a longer string");
  EXPECT_STREQ(buffer.str(), "hello world, this is a longer string");
  EXPECT_EQ(buffer.size(), 36);
}

TEST_F(StringBufferTest, GrowableAppendF) {
  FixedStringBuffer<8> buffer(alloc);
  buffer.AppendF("number=%d", 42);
  EXPECT_STREQ(buffer.str(), "number=42");
  EXPECT_EQ(buffer.size(), 9);
}

TEST_F(StringBufferTest, StrAlias) {
  Str buffer("hello ", "world");
  EXPECT_STREQ(buffer.str(), "hello world");
}

TEST_F(StringBufferTest, SmallBufferAlias) {
  SmallBuffer buffer;
  buffer.Append("test");
  EXPECT_STREQ(buffer.str(), "test");
}

TEST_F(StringBufferTest, OperatorPlusEquals) {
  FixedStringBuffer<64> buf;
  buf += "hello";
  buf += ' ';
  buf += "world";
  EXPECT_STREQ(buf.str(), "hello world");
}

TEST_F(StringBufferTest, OperatorStream) {
  FixedStringBuffer<64> buf;
  buf << "x=" << 42 << " y=" << 3.14;
  EXPECT_STREQ(buf.str(), "x=42 y=3.14");
}

TEST_F(StringBufferTest, View) {
  FixedStringBuffer<32> buf("hello");
  std::string_view sv = buf.view();
  EXPECT_EQ(sv, "hello");
  EXPECT_EQ(sv.size(), 5);
  buf.Append(" world");
  sv = buf.view();
  EXPECT_EQ(sv, "hello world");
}

TEST_F(StringBufferTest, Clear) {
  FixedStringBuffer<32> buf("hello");
  EXPECT_FALSE(buf.empty());
  buf.Clear();
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0);
  EXPECT_STREQ(buf.str(), "");
}

TEST_F(StringBufferTest, Set) {
  FixedStringBuffer<32> buf("hello");
  buf.Set("goodbye");
  EXPECT_STREQ(buf.str(), "goodbye");
  buf.Set("a", "b", "c");
  EXPECT_STREQ(buf.str(), "abc");
}

TEST_F(StringBufferTest, SetF) {
  FixedStringBuffer<32> buf("hello");
  buf.SetF("n=%d", 99);
  EXPECT_STREQ(buf.str(), "n=99");
}

TEST_F(StringBufferTest, AppendF) {
  FixedStringBuffer<64> buf;
  buf.AppendF("x=%d", 10);
  buf.AppendF(" y=%d", 20);
  EXPECT_STREQ(buf.str(), "x=10 y=20");
}

TEST_F(StringBufferTest, RemainingAndCapacity) {
  FixedStringBuffer<32> buf;
  EXPECT_EQ(buf.capacity(), 32);
  EXPECT_EQ(buf.remaining(), 32);
  buf.Append("hello");
  EXPECT_EQ(buf.remaining(), 27);
  EXPECT_EQ(buf.capacity(), 32);
}

TEST_F(StringBufferTest, AppendBuffer) {
  FixedStringBuffer<32> a("hello");
  FixedStringBuffer<32> b(" world");
  a.AppendBuffer(b);
  EXPECT_STREQ(a.str(), "hello world");
}

TEST_F(StringBufferTest, AppendToStringProtocol) {
  // StringBuffer can be appended to another StringBuffer via Append().
  FixedStringBuffer<32> inner("inner");
  FixedStringBuffer<64> outer("outer=");
  outer.Append(inner);
  EXPECT_STREQ(outer.str(), "outer=inner");
}

TEST_F(StringBufferTest, TruncatingWithInitialContent) {
  FixedStringBuffer<8> buf(kTruncating, "hello world, this is long");
  EXPECT_EQ(buf.size(), 8);
  EXPECT_STREQ(buf.str(), "hello wo");
}

TEST_F(StringBufferTest, ExplicitConstCharStar) {
  FixedStringBuffer<32> buf("test");
  // Explicit conversion works.
  const char* p = static_cast<const char*>(buf);
  EXPECT_STREQ(p, "test");
  // .str() also works.
  EXPECT_STREQ(buf.str(), "test");
}

TEST_F(StringBufferTest, CmdBufferAlias) {
  CmdBuffer buf("/some/long/path/to/file");
  EXPECT_STREQ(buf.str(), "/some/long/path/to/file");
  EXPECT_EQ(buf.capacity(), 1024);
}

TEST_F(StringBufferTest, MultiTypeAppend) {
  FixedStringBuffer<64> buf;
  buf.Append("count=", 42, " ratio=", 3.14);
  std::string_view sv = buf.view();
  EXPECT_TRUE(sv.substr(0, 9) == "count=42 ");
}

TEST(NullTerminatedTest, ZeroCopy) {
  const char* literal = "hello";
  NullTerminated nt(std::string_view(literal, 5));
  EXPECT_STREQ(nt.c_str(), "hello");
  // Zero-copy: points at the original literal since it's already terminated.
  EXPECT_EQ(nt.c_str(), literal);
}

TEST(NullTerminatedTest, Copies) {
  const char data[] = "helloXworld";
  // View into the middle -- not null-terminated at the view boundary.
  std::string_view sv(data, 5);
  NullTerminated nt(sv);
  EXPECT_STREQ(nt.c_str(), "hello");
  // Must have copied because the byte after the view is 'X', not '\0'.
  EXPECT_NE(nt.c_str(), data);
}

// StringTable

TEST(StringTableTest, InternAndLookup) {
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
