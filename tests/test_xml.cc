#include "allocators.h"
#include "gtest/gtest.h"
#include "xml.h"

namespace G {

// Each test uses a StaticAllocator (arena) so all tree nodes are freed when
// the arena goes out of scope. The XML parser doesn't individually deallocate.

TEST(XmlTest, MinimalSelfClosing) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<foo/>", &arena);
  ASSERT_FALSE(result.is_error());
  auto* root = result.value();
  EXPECT_EQ(root->tag, "foo");
  EXPECT_TRUE(root->text.empty());
  EXPECT_EQ(root->first_child, nullptr);
  EXPECT_EQ(root->first_attribute, nullptr);
}

TEST(XmlTest, EmptyElement) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<foo></foo>", &arena);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value()->tag, "foo");
  EXPECT_TRUE(result.value()->text.empty());
}

TEST(XmlTest, TextContent) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<msg>hello world</msg>", &arena);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value()->text, "hello world");
}

TEST(XmlTest, SingleAttribute) {
  StaticAllocator<4096> arena;
  auto result = ParseXml(R"(<a x="1"/>)", &arena);
  ASSERT_FALSE(result.is_error());
  auto* root = result.value();
  EXPECT_EQ(root->Attr("x"), "1");
  EXPECT_EQ(root->Attr("y"), "");
}

TEST(XmlTest, MultipleAttributes) {
  StaticAllocator<4096> arena;
  auto result = ParseXml(R"(<a x="1" y="2" z="3"/>)", &arena);
  ASSERT_FALSE(result.is_error());
  auto* root = result.value();
  EXPECT_EQ(root->Attr("x"), "1");
  EXPECT_EQ(root->Attr("y"), "2");
  EXPECT_EQ(root->Attr("z"), "3");
}

TEST(XmlTest, NestedChildren) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<root><a/><b/></root>", &arena);
  ASSERT_FALSE(result.is_error());
  auto* root = result.value();
  ASSERT_NE(root->first_child, nullptr);
  EXPECT_EQ(root->first_child->tag, "a");
  ASSERT_NE(root->first_child->next_sibling, nullptr);
  EXPECT_EQ(root->first_child->next_sibling->tag, "b");
  EXPECT_EQ(root->first_child->next_sibling->next_sibling, nullptr);
}

TEST(XmlTest, DeepNesting) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<a><b><c/></b></a>", &arena);
  ASSERT_FALSE(result.is_error());
  auto* a = result.value();
  EXPECT_EQ(a->tag, "a");
  ASSERT_NE(a->first_child, nullptr);
  auto* b = a->first_child;
  EXPECT_EQ(b->tag, "b");
  ASSERT_NE(b->first_child, nullptr);
  EXPECT_EQ(b->first_child->tag, "c");
}

TEST(XmlTest, XmlDeclarationSkipped) {
  StaticAllocator<4096> arena;
  auto result = ParseXml(R"(<?xml version="1.0"?><root/>)", &arena);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value()->tag, "root");
}

TEST(XmlTest, CommentSkipped) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<!-- comment --><root/>", &arena);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value()->tag, "root");
}

TEST(XmlTest, MultipleCommentsSkipped) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<!-- a --><!-- b --><root/>", &arena);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value()->tag, "root");
}

TEST(XmlTest, EmptyAttributeValue) {
  StaticAllocator<4096> arena;
  auto result = ParseXml(R"(<a x=""/>)", &arena);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value()->Attr("x"), "");
}

TEST(XmlTest, AttributeWithSpaces) {
  StaticAllocator<4096> arena;
  auto result = ParseXml(R"(<a msg="hello world"/>)", &arena);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value()->Attr("msg"), "hello world");
}

TEST(XmlTest, ForEachChild) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<root><item/><item/><other/></root>", &arena);
  ASSERT_FALSE(result.is_error());
  int count = 0;
  result.value()->ForEachChild("item", [&](const XmlElement&) { ++count; });
  EXPECT_EQ(count, 2);
}

TEST(XmlTest, AttrInt) {
  StaticAllocator<4096> arena;
  auto result = ParseXml(R"(<a x="42" neg="-7" zero="0"/>)", &arena);
  ASSERT_FALSE(result.is_error());
  auto* root = result.value();
  EXPECT_EQ(root->AttrInt("x"), 42);
  EXPECT_EQ(root->AttrInt("neg"), -7);
  EXPECT_EQ(root->AttrInt("zero"), 0);
  EXPECT_EQ(root->AttrInt("missing"), 0);
}

TEST(XmlTest, AttrIntNonNumeric) {
  StaticAllocator<4096> arena;
  auto result = ParseXml(R"(<a x="abc" partial="12abc"/>)", &arena);
  ASSERT_FALSE(result.is_error());
  auto* root = result.value();
  EXPECT_EQ(root->AttrInt("x"), 0);
  EXPECT_EQ(root->AttrInt("partial"), 12);
}

TEST(XmlTest, AttrFloat) {
  StaticAllocator<4096> arena;
  auto result = ParseXml(R"(<a x="3.14" neg="-1.5"/>)", &arena);
  ASSERT_FALSE(result.is_error());
  auto* root = result.value();
  EXPECT_NEAR(root->AttrFloat("x"), 3.14f, 0.01f);
  EXPECT_NEAR(root->AttrFloat("neg"), -1.5f, 0.01f);
  EXPECT_EQ(root->AttrFloat("missing"), 0.0f);
}

TEST(XmlTest, TagWithPunctuation) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<my-tag.ns:name/>", &arena);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value()->tag, "my-tag.ns:name");
}

TEST(XmlTest, WhitespaceOnlyTextIsEmpty) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<a>   </a>", &arena);
  ASSERT_FALSE(result.is_error());
  // Parser skips leading whitespace, so whitespace-only content is empty.
  EXPECT_TRUE(result.value()->text.empty());
}

TEST(XmlTest, TextWithLeadingWhitespace) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<a>  hello  </a>", &arena);
  ASSERT_FALSE(result.is_error());
  // Text starts after whitespace is skipped.
  EXPECT_FALSE(result.value()->text.empty());
}

TEST(XmlTest, SurroundingWhitespace) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("  \n<root/>  ", &arena);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value()->tag, "root");
}

TEST(XmlTest, ChildrenWithAttributes) {
  StaticAllocator<4096> arena;
  std::string input =
      R"(<root><img src="x.png" w="32" h="32"/><text>hi</text></root>)";
  auto result = ParseXml(input, &arena);
  ASSERT_FALSE(result.is_error());
  auto* img = result.value()->first_child;
  ASSERT_NE(img, nullptr);
  EXPECT_EQ(img->Attr("src"), "x.png");
  EXPECT_EQ(img->AttrInt("w"), 32);
  auto* text = img->next_sibling;
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->text, "hi");
}

// Error cases.

TEST(XmlTest, EmptyInput) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("", &arena);
  EXPECT_TRUE(result.is_error());
}

TEST(XmlTest, OnlyWhitespace) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("   ", &arena);
  EXPECT_TRUE(result.is_error());
}

TEST(XmlTest, UnclosedTag) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<foo>", &arena);
  EXPECT_TRUE(result.is_error());
}

TEST(XmlTest, MismatchedCloseTag) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<foo></bar>", &arena);
  EXPECT_TRUE(result.is_error());
}

TEST(XmlTest, MissingAttributeEquals) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<foo x/>", &arena);
  EXPECT_TRUE(result.is_error());
}

TEST(XmlTest, MissingAttributeQuote) {
  StaticAllocator<4096> arena;
  auto result = ParseXml("<foo x=val/>", &arena);
  EXPECT_TRUE(result.is_error());
}

TEST(XmlTest, UnterminatedAttributeValue) {
  StaticAllocator<4096> arena;
  auto result = ParseXml(R"(<foo x="unterminated)", &arena);
  EXPECT_TRUE(result.is_error());
}

TEST(XmlTest, ManySiblings) {
  StaticAllocator<8192> arena;
  std::string input = "<root>";
  for (int i = 0; i < 50; ++i) input += "<item/>";
  input += "</root>";
  auto result = ParseXml(input, &arena);
  ASSERT_FALSE(result.is_error());
  int count = 0;
  result.value()->ForEachChild("item", [&](const XmlElement&) { ++count; });
  EXPECT_EQ(count, 50);
}

}  // namespace G
