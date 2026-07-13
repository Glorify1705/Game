#include "allocators.h"
#include "gtest/gtest.h"
#include "test_fixture.h"

namespace G {

// SystemAllocator

TEST(SystemAllocatorTest, AllocAndDealloc) {
  auto* a = SystemAllocator::Instance();
  void* p = a->Alloc(128, 8);
  ASSERT_NE(p, nullptr);
  std::memset(p, 0xAB, 128);
  a->Dealloc(p, 128);
}

TEST(SystemAllocatorTest, DeallocNull) {
  auto* a = SystemAllocator::Instance();
  a->Dealloc(nullptr, 0);  // Must not crash.
}

TEST(SystemAllocatorTest, Realloc) {
  auto* a = SystemAllocator::Instance();
  void* p = a->Alloc(16, 8);
  ASSERT_NE(p, nullptr);
  std::memset(p, 0x42, 16);
  void* q = a->Realloc(p, 16, 256, 8);
  ASSERT_NE(q, nullptr);
  // First 16 bytes must be preserved.
  auto* bytes = static_cast<uint8_t*>(q);
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(bytes[i], 0x42);
  }
  a->Dealloc(q, 256);
}

TEST(SystemAllocatorTest, NewAndDestroy) {
  auto* a = SystemAllocator::Instance();
  int* p = a->New<int>(42);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(*p, 42);
  a->Destroy(p);
}

TEST(SystemAllocatorTest, NewArray) {
  auto* a = SystemAllocator::Instance();
  int* arr = a->NewArray<int>(8);
  ASSERT_NE(arr, nullptr);
  for (int i = 0; i < 8; ++i) arr[i] = i * 10;
  for (int i = 0; i < 8; ++i) EXPECT_EQ(arr[i], i * 10);
  a->DeallocArray(arr, 8);
}

TEST(SystemAllocatorTest, StrDup) {
  auto* a = SystemAllocator::Instance();
  std::string_view original = "hello world";
  std::string_view duped = a->StrDup(original);
  EXPECT_EQ(duped, "hello world");
  EXPECT_NE(duped.data(), original.data());
  a->Dealloc(const_cast<char*>(duped.data()), duped.size());
}

TEST(SystemAllocatorTest, Singleton) {
  EXPECT_EQ(SystemAllocator::Instance(), SystemAllocator::Instance());
}

// ArenaAllocator

class ArenaTest : public BaseTest {};

TEST_F(ArenaTest, BasicAlloc) {
  ArenaAllocator arena(alloc, 1024);
  void* p = arena.Alloc(64, 8);
  ASSERT_NE(p, nullptr);
  EXPECT_GT(arena.used_memory(), 0u);
  EXPECT_EQ(arena.total_memory(), 1024u);
}

TEST_F(ArenaTest, MultipleAllocs) {
  ArenaAllocator arena(alloc, 1024);
  void* a1 = arena.Alloc(100, 8);
  void* a2 = arena.Alloc(100, 8);
  void* a3 = arena.Alloc(100, 8);
  ASSERT_NE(a1, nullptr);
  ASSERT_NE(a2, nullptr);
  ASSERT_NE(a3, nullptr);
  // All pointers must be distinct.
  EXPECT_NE(a1, a2);
  EXPECT_NE(a2, a3);
}

TEST_F(ArenaTest, ExhaustReturnsNull) {
  ArenaAllocator arena(alloc, 128);
  void* p1 = arena.Alloc(64, 8);
  ASSERT_NE(p1, nullptr);
  void* p2 = arena.Alloc(64, 8);
  // Alignment overhead may consume extra bytes.
  if (p2 != nullptr) {
    // If second alloc succeeded, a third large one should fail.
    void* p3 = arena.Alloc(128, 8);
    EXPECT_EQ(p3, nullptr);
  }
}

TEST_F(ArenaTest, Reset) {
  ArenaAllocator arena(alloc, 1024);
  arena.Alloc(512, 8);
  EXPECT_GT(arena.used_memory(), 0u);
  arena.Reset();
  EXPECT_EQ(arena.used_memory(), 0u);
  // Can allocate again after reset.
  void* p = arena.Alloc(512, 8);
  EXPECT_NE(p, nullptr);
}

TEST_F(ArenaTest, DeallocLastFreesBump) {
  ArenaAllocator arena(alloc, 1024);
  arena.Alloc(64, 8);
  size_t before = arena.used_memory();
  void* p = arena.Alloc(128, 8);
  EXPECT_GT(arena.used_memory(), before);
  // Deallocating the last allocation reclaims space.
  arena.Dealloc(p, 128);
  EXPECT_EQ(arena.used_memory(), before);
}

TEST_F(ArenaTest, DeallocNonLastIsNoop) {
  ArenaAllocator arena(alloc, 1024);
  void* first = arena.Alloc(64, 8);
  arena.Alloc(64, 8);
  size_t used = arena.used_memory();
  // Deallocating a non-last allocation doesn't reclaim space.
  arena.Dealloc(first, 64);
  EXPECT_EQ(arena.used_memory(), used);
}

TEST_F(ArenaTest, DeallocNull) {
  ArenaAllocator arena(alloc, 256);
  arena.Dealloc(nullptr, 0);  // Must not crash.
}

TEST_F(ArenaTest, ReallocCopies) {
  ArenaAllocator arena(alloc, 1024);
  void* p = arena.Alloc(16, 8);
  std::memset(p, 0xCD, 16);
  void* q = arena.Realloc(p, 16, 64, 8);
  ASSERT_NE(q, nullptr);
  auto* bytes = static_cast<uint8_t*>(q);
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(bytes[i], 0xCD);
  }
}

// StaticAllocator

TEST(StaticAllocatorTest, StackBacked) {
  StaticAllocator<512> arena;
  EXPECT_EQ(arena.total_memory(), 512u);
  void* p = arena.Alloc(256, 8);
  ASSERT_NE(p, nullptr);
  EXPECT_GT(arena.used_memory(), 0u);
}

TEST(StaticAllocatorTest, ExhaustReturnsNull) {
  StaticAllocator<64> arena;
  void* p1 = arena.Alloc(32, 8);
  ASSERT_NE(p1, nullptr);
  void* p2 = arena.Alloc(32, 8);
  // Might succeed or fail depending on alignment overhead.
  void* p3 = arena.Alloc(32, 8);
  // At least one of these must be null.
  EXPECT_TRUE(p2 == nullptr || p3 == nullptr);
}

// BlockAllocator

class BlockAllocatorAllocTest : public BaseTest {};

TEST_F(BlockAllocatorAllocTest, AllocAndDealloc) {
  BlockAllocator<int> blocks(alloc, 4);
  int* a = blocks.AllocBlock();
  int* b = blocks.AllocBlock();
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  *a = 10;
  *b = 20;
  EXPECT_EQ(*a, 10);
  EXPECT_EQ(*b, 20);
  blocks.DeallocBlock(a);
  blocks.DeallocBlock(b);
}

TEST_F(BlockAllocatorAllocTest, ExhaustReturnsNull) {
  BlockAllocator<int> blocks(alloc, 2);
  int* a = blocks.AllocBlock();
  int* b = blocks.AllocBlock();
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  int* c = blocks.AllocBlock();
  EXPECT_EQ(c, nullptr);
  blocks.DeallocBlock(a);
  blocks.DeallocBlock(b);
}

TEST_F(BlockAllocatorAllocTest, ReuseAfterDealloc) {
  BlockAllocator<int> blocks(alloc, 1);
  int* a = blocks.AllocBlock();
  ASSERT_NE(a, nullptr);
  blocks.DeallocBlock(a);
  int* b = blocks.AllocBlock();
  EXPECT_NE(b, nullptr);
  blocks.DeallocBlock(b);
}

// FreeList

class FreeListAllocTest : public BaseTest {};

TEST_F(FreeListAllocTest, AllocAndDealloc) {
  ArenaAllocator arena(alloc, 1024);
  FreeList<int> fl(&arena);
  int* a = fl.Alloc();
  ASSERT_NE(a, nullptr);
  *a = 42;
  EXPECT_EQ(*a, 42);
  fl.Dealloc(a);
}

TEST_F(FreeListAllocTest, NewConstructs) {
  struct Pair {
    int x, y;
    Pair(int a, int b) : x(a), y(b) {}
  };
  ArenaAllocator arena(alloc, 1024);
  FreeList<Pair> fl(&arena);
  Pair* p = fl.New(3, 7);
  EXPECT_EQ(p->x, 3);
  EXPECT_EQ(p->y, 7);
  fl.Dealloc(p);
}

TEST_F(FreeListAllocTest, ReusesMemory) {
  ArenaAllocator arena(alloc, 1024);
  FreeList<int> fl(&arena);
  int* a = fl.Alloc();
  fl.Dealloc(a);
  int* b = fl.Alloc();
  // After dealloc, the same block should be reused.
  EXPECT_EQ(a, b);
  fl.Dealloc(b);
}

}  // namespace G
