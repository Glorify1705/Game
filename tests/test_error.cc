#include <cerrno>
#include <string_view>

#include "error.h"
#include "gtest/gtest.h"

namespace G {

TEST(ErrorTest, ErrnoConstruction) {
  auto e = Error::Errno(ENOMEM);
  EXPECT_TRUE(e.is_errno());
  EXPECT_EQ(e.code(), ENOMEM);
  EXPECT_TRUE(e.message().empty());
}

TEST(ErrorTest, MessageConstruction) {
  auto e = Error::Message("bad format");
  EXPECT_FALSE(e.is_errno());
  EXPECT_EQ(e.code(), 0);
  EXPECT_EQ(e.message(), "bad format");
}

TEST(ErrorTest, CapturesSourceLocation) {
  auto e = Error::Message("oops");
  // __builtin_LINE() captures the call site, so line should be nonzero and
  // file should end with "test_error.cc".
  EXPECT_GT(e.line(), 0u);
  std::string_view file(e.file());
  EXPECT_NE(file.find("test_error.cc"), std::string_view::npos);
}

TEST(ErrorOrTest, ValueConstruction) {
  ErrorOr<int> result(42);
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(result.value(), 42);
  EXPECT_EQ(result.release_value(), 42);
}

TEST(ErrorOrTest, ErrorConstruction) {
  ErrorOr<int> result(Error::Message("fail"));
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error().message(), "fail");
}

TEST(ErrorOrTest, ImplicitFromValue) {
  auto fn = []() -> ErrorOr<int> { return 7; };
  auto result = fn();
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(result.value(), 7);
}

TEST(ErrorOrTest, ImplicitFromError) {
  auto fn = []() -> ErrorOr<int> { return Error::Message("nope"); };
  auto result = fn();
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error().message(), "nope");
}

TEST(ErrorOrTest, PointerValue) {
  int x = 99;
  ErrorOr<int*> result(&x);
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(*result.value(), 99);
}

TEST(ErrorOrTest, MoveConstruction) {
  ErrorOr<int> a(10);
  ErrorOr<int> b(std::move(a));
  EXPECT_FALSE(b.is_error());
  EXPECT_EQ(b.value(), 10);
}

TEST(ErrorOrTest, MoveConstructionError) {
  ErrorOr<int> a(Error::Errno(ENOMEM));
  ErrorOr<int> b(std::move(a));
  EXPECT_TRUE(b.is_error());
  EXPECT_EQ(b.error().code(), ENOMEM);
}

TEST(ErrorOrTest, VoidSuccess) {
  auto fn = []() -> ErrorOr<void> { return {}; };
  auto result = fn();
  EXPECT_FALSE(result.is_error());
}

TEST(ErrorOrTest, VoidError) {
  auto fn = []() -> ErrorOr<void> { return Error::Message("void fail"); };
  auto result = fn();
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error().message(), "void fail");
}

TEST(ErrorOrTest, VoidMoveConstruction) {
  ErrorOr<void> a(Error::Message("moved"));
  ErrorOr<void> b(std::move(a));
  EXPECT_TRUE(b.is_error());
  EXPECT_EQ(b.error().message(), "moved");
}

// Helper functions for TRY tests.
static ErrorOr<int> Succeed(int v) { return v; }
static ErrorOr<int> Fail() { return Error::Message("failed"); }

static ErrorOr<int> TrySuccessChain() {
  int a = TRY(Succeed(10));
  int b = TRY(Succeed(20));
  return a + b;
}

static ErrorOr<int> TryFailureChain() {
  int a = TRY(Succeed(10));
  int b = TRY(Fail());
  return a + b;  // never reached
}

static ErrorOr<void> TryVoidSuccess() {
  TRY(Succeed(1));
  return {};
}

static ErrorOr<void> TryVoidFailure() {
  TRY(Fail());
  return {};
}

TEST(TryTest, PropagatesValue) {
  auto result = TrySuccessChain();
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(result.value(), 30);
}

TEST(TryTest, PropagatesError) {
  auto result = TryFailureChain();
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error().message(), "failed");
}

TEST(TryTest, VoidSuccess) {
  auto result = TryVoidSuccess();
  EXPECT_FALSE(result.is_error());
}

TEST(TryTest, VoidFailure) {
  auto result = TryVoidFailure();
  EXPECT_TRUE(result.is_error());
}

TEST(MustTest, UnwrapsValue) {
  int v = MUST(Succeed(42));
  EXPECT_EQ(v, 42);
}

TEST(MustTest, VoidSuccess) {
  auto fn = []() -> ErrorOr<void> { return {}; };
  MUST(fn());  // should not crash
}

}  // namespace G
