#include "allocators.h"
#include "array.h"
#include "bits.h"
#include "circular_buffer.h"
#include "defer.h"
#include "dictionary.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest-matchers.h"
#include "segmented_list.h"
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

// ---------------------------------------------------------------------------
// SegmentedList
// ---------------------------------------------------------------------------

TEST(SegmentedList, EmptyOnConstruction) {
  SegmentedList<int> list(SystemAllocator::Instance());
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.size(), 0u);
  EXPECT_EQ(list.capacity(), 0u);
}

TEST(SegmentedList, PushAndAccess) {
  SegmentedList<int> list(SystemAllocator::Instance());
  for (int i = 0; i < 100; ++i) {
    list.Push(i);
  }
  EXPECT_EQ(list.size(), 100u);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(list[i], i);
  }
}

TEST(SegmentedList, StablePointers) {
  SegmentedList<int> list(SystemAllocator::Instance());
  int* ptrs[64];
  for (int i = 0; i < 64; ++i) {
    ptrs[i] = list.Push(i);
  }
  // All pointers remain valid after growth.
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(*ptrs[i], i);
  }
}

TEST(SegmentedList, PtrAt) {
  SegmentedList<int> list(SystemAllocator::Instance());
  for (int i = 0; i < 32; ++i) {
    list.Push(i * 10);
  }
  for (int i = 0; i < 32; ++i) {
    EXPECT_EQ(*list.PtrAt(i), i * 10);
  }
}

TEST(SegmentedList, Pop) {
  SegmentedList<int> list(SystemAllocator::Instance());
  list.Push(1);
  list.Push(2);
  list.Push(3);
  EXPECT_EQ(list.size(), 3u);
  EXPECT_EQ(list.back(), 3);
  list.Pop();
  EXPECT_EQ(list.size(), 2u);
  EXPECT_EQ(list.back(), 2);
}

TEST(SegmentedList, Clear) {
  SegmentedList<int> list(SystemAllocator::Instance());
  for (int i = 0; i < 50; ++i) list.Push(i);
  list.Clear();
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.size(), 0u);
  // Can push again after clear.
  list.Push(42);
  EXPECT_EQ(list[0], 42);
}

TEST(SegmentedList, Emplace) {
  SegmentedList<int> list(SystemAllocator::Instance());
  int* p = list.Emplace(42);
  EXPECT_EQ(*p, 42);
  EXPECT_EQ(list[0], 42);
}

TEST(SegmentedList, Iterator) {
  SegmentedList<int> list(SystemAllocator::Instance());
  for (int i = 0; i < 20; ++i) list.Push(i);
  int expected = 0;
  for (int v : list) {
    EXPECT_EQ(v, expected);
    expected++;
  }
  EXPECT_EQ(expected, 20);
}

TEST(SegmentedList, ConstAccess) {
  SegmentedList<int> list(SystemAllocator::Instance());
  for (int i = 0; i < 10; ++i) list.Push(i);
  const auto& clist = list;
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(clist[i], i);
  }
  EXPECT_EQ(clist.back(), 9);
  EXPECT_EQ(*clist.PtrAt(5), 5);
}

TEST(SegmentedList, CustomPrealloc) {
  SegmentedList<int, 4> list(SystemAllocator::Instance());
  for (int i = 0; i < 100; ++i) {
    list.Push(i);
  }
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(list[i], i);
  }
}

TEST(SegmentedList, LargePrealloc) {
  SegmentedList<int, 64> list(SystemAllocator::Instance());
  for (int i = 0; i < 1000; ++i) {
    list.Push(i);
  }
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(list[i], i);
  }
}

TEST(SegmentedList, BoundaryIndices) {
  // Test indices right at shelf boundaries with P=4.
  SegmentedList<int, 4> list(SystemAllocator::Instance());
  // Shelf 0: indices 0-3 (4 items)
  // Shelf 1: indices 4-7 (4 items)
  // Shelf 2: indices 8-15 (8 items)
  // Shelf 3: indices 16-31 (16 items)
  for (int i = 0; i < 32; ++i) {
    list.Push(i * 100);
  }
  // Check boundary values.
  EXPECT_EQ(list[0], 0);      // shelf 0 start
  EXPECT_EQ(list[3], 300);    // shelf 0 end
  EXPECT_EQ(list[4], 400);    // shelf 1 start
  EXPECT_EQ(list[7], 700);    // shelf 1 end
  EXPECT_EQ(list[8], 800);    // shelf 2 start
  EXPECT_EQ(list[15], 1500);  // shelf 2 end
  EXPECT_EQ(list[16], 1600);  // shelf 3 start
  EXPECT_EQ(list[31], 3100);  // shelf 3 end
}

TEST(SegmentedList, WithArenaAllocator) {
  StaticAllocator<4096> arena;
  SegmentedList<int> list(&arena);
  for (int i = 0; i < 50; ++i) {
    list.Push(i);
  }
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(list[i], i);
  }
}

}  // namespace G
