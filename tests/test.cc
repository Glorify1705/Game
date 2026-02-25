#include "allocators.h"
#include "array.h"
#include "bits.h"
#include "circular_buffer.h"
#include "defer.h"
#include "dictionary.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest-matchers.h"
#include "stringlib.h"
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
  for (std::size_t i = 0; i < array.size(); ++i) {
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

TEST(Tests, BlockAllocator) {
  BlockAllocator<uint32_t> pool(SystemAllocator::Instance(), 2);
  uint32_t* p = pool.AllocBlock();
  uint32_t* q = pool.AllocBlock();
  EXPECT_NE(p, q);
  pool.DeallocBlock(q);
  uint32_t* r = pool.AllocBlock();
  EXPECT_EQ(q, r);
}

TEST(Tests, FreeListAllocator) {
  FreeList<uint32_t> freelist(SystemAllocator::Instance());
  uint32_t* p = freelist.Alloc();
  uint32_t* q = freelist.Alloc();
  EXPECT_NE(p, q);
  freelist.Dealloc(q);
  uint32_t* r = freelist.Alloc();
  EXPECT_EQ(q, r);
}

TEST(Tests, ShardedFreeListAllocator) {
  void* buffer;
  ASSERT_TRUE(posix_memalign(&buffer, kMaxAlign, Megabytes(128)) == 0);
  DEFER([&] { std::free(buffer); });
  auto* a =
      new ShardedFreeListAllocator(SystemAllocator::Instance(), Megabytes(128));
  DEFER([&] { delete a; });
  auto* p = a->Alloc(1, 16);
  ASSERT_NE(p, nullptr);
  a->Dealloc(p, 1);
  std::array<void*, 1000> ptrs;
  for (size_t i = 0; i < 1000; ++i) {
    ptrs[i] = a->Alloc(1, 16);
  }
  for (size_t i = 0; i < 1000; ++i) {
    a->Dealloc(ptrs[i], 1);
  }
  for (size_t i = 0; i < 1000; ++i) {
    ptrs[i] = a->Alloc(513, 16);
  }
  for (size_t i = 0; i < 1000; ++i) {
    a->Dealloc(ptrs[i], 513);
  }
}

// ---------------------------------------------------------------------------
// CircularBuffer
// ---------------------------------------------------------------------------

TEST(CircularBuffer, EmptyOnConstruction) {
  CircularBuffer<int> buf(4, SystemAllocator::Instance());
  EXPECT_TRUE(buf.empty());
  EXPECT_FALSE(buf.full());
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.capacity(), 4u);
}

TEST(CircularBuffer, PushAndSize) {
  CircularBuffer<int> buf(4, SystemAllocator::Instance());
  buf.Push(10);
  EXPECT_EQ(buf.size(), 1u);
  EXPECT_FALSE(buf.empty());
  EXPECT_FALSE(buf.full());
  buf.Push(20);
  EXPECT_EQ(buf.size(), 2u);
  buf.Push(30);
  buf.Push(40);
  EXPECT_EQ(buf.size(), 4u);
  EXPECT_TRUE(buf.full());
}

TEST(CircularBuffer, FrontAndBack) {
  CircularBuffer<int> buf(4, SystemAllocator::Instance());
  buf.Push(10);
  EXPECT_EQ(buf.front(), 10);
  EXPECT_EQ(buf.back(), 10);
  buf.Push(20);
  EXPECT_EQ(buf.front(), 10);
  EXPECT_EQ(buf.back(), 20);
  buf.Push(30);
  EXPECT_EQ(buf.front(), 10);
  EXPECT_EQ(buf.back(), 30);
}

TEST(CircularBuffer, PopReturnsOldest) {
  CircularBuffer<int> buf(4, SystemAllocator::Instance());
  buf.Push(10);
  buf.Push(20);
  buf.Push(30);
  EXPECT_EQ(buf.Pop(), 10);
  EXPECT_EQ(buf.Pop(), 20);
  EXPECT_EQ(buf.Pop(), 30);
  EXPECT_TRUE(buf.empty());
}

TEST(CircularBuffer, PopUpdatesState) {
  CircularBuffer<int> buf(4, SystemAllocator::Instance());
  buf.Push(1);
  buf.Push(2);
  buf.Push(3);
  buf.Push(4);
  EXPECT_TRUE(buf.full());
  buf.Pop();
  EXPECT_FALSE(buf.full());
  EXPECT_EQ(buf.size(), 3u);
  EXPECT_EQ(buf.front(), 2);
}

TEST(CircularBuffer, IndexedAccess) {
  CircularBuffer<int> buf(4, SystemAllocator::Instance());
  buf.Push(10);
  buf.Push(20);
  buf.Push(30);
  EXPECT_EQ(buf[0], 10);
  EXPECT_EQ(buf[1], 20);
  EXPECT_EQ(buf[2], 30);
}

TEST(CircularBuffer, IndexedAccessAfterPop) {
  CircularBuffer<int> buf(4, SystemAllocator::Instance());
  buf.Push(10);
  buf.Push(20);
  buf.Push(30);
  buf.Pop();
  EXPECT_EQ(buf[0], 20);
  EXPECT_EQ(buf[1], 30);
}

TEST(CircularBuffer, Wraparound) {
  CircularBuffer<int> buf(3, SystemAllocator::Instance());
  buf.Push(1);
  buf.Push(2);
  buf.Push(3);
  EXPECT_TRUE(buf.full());

  EXPECT_EQ(buf.Pop(), 1);
  EXPECT_EQ(buf.Pop(), 2);

  buf.Push(4);
  buf.Push(5);
  EXPECT_TRUE(buf.full());

  EXPECT_EQ(buf[0], 3);
  EXPECT_EQ(buf[1], 4);
  EXPECT_EQ(buf[2], 5);
  EXPECT_EQ(buf.front(), 3);
  EXPECT_EQ(buf.back(), 5);
}

TEST(CircularBuffer, OverwriteWhenFull) {
  CircularBuffer<int> buf(3, SystemAllocator::Instance());
  buf.Push(1);
  buf.Push(2);
  buf.Push(3);
  EXPECT_TRUE(buf.full());

  buf.Push(4);
  EXPECT_TRUE(buf.full());
  EXPECT_EQ(buf.size(), 3u);
  EXPECT_EQ(buf.front(), 2);
  EXPECT_EQ(buf.back(), 4);
  EXPECT_EQ(buf[0], 2);
  EXPECT_EQ(buf[1], 3);
  EXPECT_EQ(buf[2], 4);
}

TEST(CircularBuffer, OverwriteMultiple) {
  CircularBuffer<int> buf(3, SystemAllocator::Instance());
  buf.Push(1);
  buf.Push(2);
  buf.Push(3);

  buf.Push(4);
  buf.Push(5);
  buf.Push(6);
  EXPECT_TRUE(buf.full());
  EXPECT_EQ(buf.size(), 3u);
  EXPECT_EQ(buf.front(), 4);
  EXPECT_EQ(buf.back(), 6);
}

TEST(CircularBuffer, PushPopInterleaved) {
  CircularBuffer<int> buf(2, SystemAllocator::Instance());
  for (int i = 0; i < 100; ++i) {
    buf.Push(i);
    EXPECT_EQ(buf.Pop(), i);
    EXPECT_TRUE(buf.empty());
  }
}

TEST(CircularBuffer, FillEmptyRepeatedly) {
  CircularBuffer<int> buf(3, SystemAllocator::Instance());
  for (int round = 0; round < 10; ++round) {
    buf.Push(round * 3);
    buf.Push(round * 3 + 1);
    buf.Push(round * 3 + 2);
    EXPECT_TRUE(buf.full());
    EXPECT_EQ(buf.Pop(), round * 3);
    EXPECT_EQ(buf.Pop(), round * 3 + 1);
    EXPECT_EQ(buf.Pop(), round * 3 + 2);
    EXPECT_TRUE(buf.empty());
  }
}

TEST(CircularBuffer, SizeOneBuffer) {
  CircularBuffer<int> buf(1, SystemAllocator::Instance());
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.capacity(), 1u);

  buf.Push(42);
  EXPECT_TRUE(buf.full());
  EXPECT_EQ(buf.size(), 1u);
  EXPECT_EQ(buf.front(), 42);
  EXPECT_EQ(buf.back(), 42);

  EXPECT_EQ(buf.Pop(), 42);
  EXPECT_TRUE(buf.empty());
}

TEST(CircularBuffer, SizeOneOverwrite) {
  CircularBuffer<int> buf(1, SystemAllocator::Instance());
  buf.Push(1);
  buf.Push(2);
  EXPECT_TRUE(buf.full());
  EXPECT_EQ(buf.size(), 1u);
  EXPECT_EQ(buf.front(), 2);
  EXPECT_EQ(buf.back(), 2);
}

TEST(CircularBuffer, ConstAccess) {
  CircularBuffer<int> buf(4, SystemAllocator::Instance());
  buf.Push(10);
  buf.Push(20);
  const auto& cbuf = buf;
  EXPECT_EQ(cbuf[0], 10);
  EXPECT_EQ(cbuf[1], 20);
  EXPECT_EQ(cbuf.front(), 10);
  EXPECT_EQ(cbuf.back(), 20);
  EXPECT_EQ(cbuf.size(), 2u);
  EXPECT_FALSE(cbuf.empty());
  EXPECT_FALSE(cbuf.full());
  EXPECT_EQ(cbuf.capacity(), 4u);
}

}  // namespace G
