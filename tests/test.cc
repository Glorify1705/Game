#include "allocators.h"
#include "array.h"
#include "bits.h"
#include "circular_buffer.h"
#include "collision.h"
#include "collision_world.h"
#include "defer.h"
#include "dictionary.h"
#include "easing.h"
#include "error.h"
#include "executor.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest-matchers.h"
#include "inlined_array.h"
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
}

TEST(Tests, FixedStringBufferTruncation) {
  FixedStringBuffer<16> buffer;
  buffer.AllowTruncation();
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

TEST(Tests, FixedStringBufferGrowable) {
  Allocator* alloc = SystemAllocator::Instance();
  FixedStringBuffer<8> buffer(alloc);
  buffer.Append("hello");
  EXPECT_STREQ(buffer.str(), "hello");
  EXPECT_EQ(buffer.size(), 5);
  // This would truncate without an allocator, but grows instead.
  buffer.Append(" world, this is a longer string");
  EXPECT_STREQ(buffer.str(), "hello world, this is a longer string");
  EXPECT_EQ(buffer.size(), 36);
}

TEST(Tests, FixedStringBufferGrowableAppendF) {
  Allocator* alloc = SystemAllocator::Instance();
  FixedStringBuffer<8> buffer(alloc);
  buffer.AppendF("number=%d", 42);
  EXPECT_STREQ(buffer.str(), "number=42");
  EXPECT_EQ(buffer.size(), 9);
}

TEST(Tests, StrAlias) {
  Str buffer("hello ", "world");
  EXPECT_STREQ(buffer.str(), "hello world");
}

TEST(Tests, SmallBufferAlias) {
  SmallBuffer buffer;
  buffer.Append("test");
  EXPECT_STREQ(buffer.str(), "test");
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
  StaticAllocator<1024> arena;
  FreeList<uint32_t> freelist(&arena);
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

// ---------------------------------------------------------------------------
// InlinedArray
// ---------------------------------------------------------------------------

TEST(InlinedArray, EmptyOnConstruction) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  EXPECT_TRUE(arr.empty());
  EXPECT_EQ(arr.size(), 0u);
  EXPECT_EQ(arr.capacity(), 4u);
}

TEST(InlinedArray, PushWithinInline) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  arr.Push(10);
  arr.Push(20);
  arr.Push(30);
  EXPECT_EQ(arr.size(), 3u);
  EXPECT_EQ(arr[0], 10);
  EXPECT_EQ(arr[1], 20);
  EXPECT_EQ(arr[2], 30);
  EXPECT_EQ(arr.capacity(), 4u);
}

TEST(InlinedArray, FillInline) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  arr.Push(1);
  arr.Push(2);
  arr.Push(3);
  arr.Push(4);
  EXPECT_EQ(arr.size(), 4u);
  EXPECT_EQ(arr.capacity(), 4u);
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(arr[i], i + 1);
  }
}

TEST(InlinedArray, SpillToHeap) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  for (int i = 0; i < 5; ++i) arr.Push(i);
  EXPECT_EQ(arr.size(), 5u);
  EXPECT_GT(arr.capacity(), 4u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(arr[i], i);
  }
}

TEST(InlinedArray, GrowOnHeap) {
  InlinedArray<int, 2> arr(SystemAllocator::Instance());
  for (int i = 0; i < 100; ++i) arr.Push(i);
  EXPECT_EQ(arr.size(), 100u);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(arr[i], i);
  }
}

TEST(InlinedArray, Pop) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  arr.Push(1);
  arr.Push(2);
  arr.Push(3);
  EXPECT_EQ(arr.back(), 3);
  arr.Pop();
  EXPECT_EQ(arr.size(), 2u);
  EXPECT_EQ(arr.back(), 2);
}

TEST(InlinedArray, Clear) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  arr.Push(1);
  arr.Push(2);
  arr.Clear();
  EXPECT_TRUE(arr.empty());
  arr.Push(42);
  EXPECT_EQ(arr[0], 42);
}

TEST(InlinedArray, Emplace) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  arr.Emplace(42);
  EXPECT_EQ(arr[0], 42);
}

TEST(InlinedArray, Iterator) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  for (int i = 0; i < 3; ++i) arr.Push(i * 10);
  int expected = 0;
  for (int v : arr) {
    EXPECT_EQ(v, expected);
    expected += 10;
  }
}

TEST(InlinedArray, IteratorAfterSpill) {
  InlinedArray<int, 2> arr(SystemAllocator::Instance());
  for (int i = 0; i < 10; ++i) arr.Push(i);
  int expected = 0;
  for (int v : arr) {
    EXPECT_EQ(v, expected);
    expected++;
  }
  EXPECT_EQ(expected, 10);
}

TEST(InlinedArray, ConstAccess) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  arr.Push(10);
  arr.Push(20);
  const auto& carr = arr;
  EXPECT_EQ(carr[0], 10);
  EXPECT_EQ(carr[1], 20);
  EXPECT_EQ(carr.back(), 20);
  EXPECT_EQ(carr.size(), 2u);
}

TEST(InlinedArray, SingleElementInline) {
  InlinedArray<int, 1> arr(SystemAllocator::Instance());
  arr.Push(42);
  EXPECT_EQ(arr[0], 42);
  EXPECT_EQ(arr.capacity(), 1u);
  arr.Push(99);
  EXPECT_EQ(arr.size(), 2u);
  EXPECT_GT(arr.capacity(), 1u);
  EXPECT_EQ(arr[0], 42);
  EXPECT_EQ(arr[1], 99);
}

TEST(InlinedArray, LargeInline) {
  InlinedArray<int, 64> arr(SystemAllocator::Instance());
  for (int i = 0; i < 64; ++i) arr.Push(i);
  EXPECT_EQ(arr.capacity(), 64u);
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(arr[i], i);
  }
  // One more triggers spill.
  arr.Push(64);
  EXPECT_EQ(arr.size(), 65u);
  EXPECT_GT(arr.capacity(), 64u);
}

TEST(InlinedArray, WithArenaAllocator) {
  StaticAllocator<4096> arena;
  InlinedArray<int, 8> arr(&arena);
  for (int i = 0; i < 50; ++i) arr.Push(i);
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(arr[i], i);
  }
}

TEST(InlinedArray, DataPointer) {
  InlinedArray<int, 4> arr(SystemAllocator::Instance());
  arr.Push(1);
  arr.Push(2);
  int* p = arr.data();
  EXPECT_EQ(p[0], 1);
  EXPECT_EQ(p[1], 2);
}

// ---------------------------------------------------------------------------
// Error / ErrorOr / TRY / MUST
// ---------------------------------------------------------------------------

TEST(Error, ErrnoConstruction) {
  auto e = Error::Errno(ENOMEM);
  EXPECT_TRUE(e.is_errno());
  EXPECT_EQ(e.code(), ENOMEM);
  EXPECT_TRUE(e.message().empty());
}

TEST(Error, MessageConstruction) {
  auto e = Error::Message("bad format");
  EXPECT_FALSE(e.is_errno());
  EXPECT_EQ(e.code(), 0);
  EXPECT_EQ(e.message(), "bad format");
}

TEST(Error, CapturesSourceLocation) {
  auto e = Error::Message("oops");
  // __builtin_LINE() captures the call site, so line should be nonzero and
  // file should end with "test.cc".
  EXPECT_GT(e.line(), 0u);
  std::string_view file(e.file());
  EXPECT_NE(file.find("test.cc"), std::string_view::npos);
}

TEST(ErrorOr, ValueConstruction) {
  ErrorOr<int> result(42);
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(result.value(), 42);
  EXPECT_EQ(result.release_value(), 42);
}

TEST(ErrorOr, ErrorConstruction) {
  ErrorOr<int> result(Error::Message("fail"));
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error().message(), "fail");
}

TEST(ErrorOr, ImplicitFromValue) {
  auto fn = []() -> ErrorOr<int> { return 7; };
  auto result = fn();
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(result.value(), 7);
}

TEST(ErrorOr, ImplicitFromError) {
  auto fn = []() -> ErrorOr<int> { return Error::Message("nope"); };
  auto result = fn();
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error().message(), "nope");
}

TEST(ErrorOr, PointerValue) {
  int x = 99;
  ErrorOr<int*> result(&x);
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(*result.value(), 99);
}

TEST(ErrorOr, MoveConstruction) {
  ErrorOr<int> a(10);
  ErrorOr<int> b(std::move(a));
  EXPECT_FALSE(b.is_error());
  EXPECT_EQ(b.value(), 10);
}

TEST(ErrorOr, MoveConstructionError) {
  ErrorOr<int> a(Error::Errno(ENOMEM));
  ErrorOr<int> b(std::move(a));
  EXPECT_TRUE(b.is_error());
  EXPECT_EQ(b.error().code(), ENOMEM);
}

TEST(ErrorOr, VoidSuccess) {
  auto fn = []() -> ErrorOr<void> { return {}; };
  auto result = fn();
  EXPECT_FALSE(result.is_error());
}

TEST(ErrorOr, VoidError) {
  auto fn = []() -> ErrorOr<void> { return Error::Message("void fail"); };
  auto result = fn();
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error().message(), "void fail");
}

TEST(ErrorOr, VoidMoveConstruction) {
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

TEST(TRY, PropagatesValue) {
  auto result = TrySuccessChain();
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(result.value(), 30);
}

TEST(TRY, PropagatesError) {
  auto result = TryFailureChain();
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error().message(), "failed");
}

TEST(TRY, VoidSuccess) {
  auto result = TryVoidSuccess();
  EXPECT_FALSE(result.is_error());
}

TEST(TRY, VoidFailure) {
  auto result = TryVoidFailure();
  EXPECT_TRUE(result.is_error());
}

TEST(MUST, UnwrapsValue) {
  int v = MUST(Succeed(42));
  EXPECT_EQ(v, 42);
}

TEST(MUST, VoidSuccess) {
  auto fn = []() -> ErrorOr<void> { return {}; };
  MUST(fn());  // should not crash
}

// --- Collision Detection Tests ---

TEST(Collision, CircleCircleOverlap) {
  auto r = TestCircleCircle(FVec(0, 0), 10, FVec(15, 0), 10);
  EXPECT_TRUE(r.hit);
  // Separation normal pushes A away from B (leftward).
  EXPECT_NEAR(r.normal.x, -1.0f, 1e-5f);
  EXPECT_NEAR(r.normal.y, 0.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 5.0f, 1e-5f);
}

TEST(Collision, CircleCircleNoOverlap) {
  auto r = TestCircleCircle(FVec(0, 0), 10, FVec(25, 0), 10);
  EXPECT_FALSE(r.hit);
}

TEST(Collision, CircleCircleTangent) {
  auto r = TestCircleCircle(FVec(0, 0), 10, FVec(20, 0), 10);
  // Tangent: sum of radii == distance, so overlap = 0. Our test uses >,
  // so tangent is not a collision.
  EXPECT_FALSE(r.hit);
}

TEST(Collision, CircleCircleContained) {
  auto r = TestCircleCircle(FVec(0, 0), 20, FVec(5, 0), 5);
  EXPECT_TRUE(r.hit);
  EXPECT_NEAR(r.normal.x, -1.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 20.0f, 1e-5f);
}

TEST(Collision, CircleCircleCoincident) {
  auto r = TestCircleCircle(FVec(5, 5), 10, FVec(5, 5), 10);
  EXPECT_TRUE(r.hit);
  EXPECT_NEAR(r.depth, 20.0f, 1e-5f);
}

TEST(Collision, AABBAABBOverlap) {
  auto r = TestAABBAABB(FVec(0, 0), 10, 10, FVec(15, 0), 10, 10);
  EXPECT_TRUE(r.hit);
  // Separation normal pushes A away from B (leftward).
  EXPECT_NEAR(r.normal.x, -1.0f, 1e-5f);
  EXPECT_NEAR(r.normal.y, 0.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 5.0f, 1e-5f);
}

TEST(Collision, AABBAABBNoOverlap) {
  auto r = TestAABBAABB(FVec(0, 0), 10, 10, FVec(25, 0), 10, 10);
  EXPECT_FALSE(r.hit);
}

TEST(Collision, AABBAABBVerticalOverlap) {
  auto r = TestAABBAABB(FVec(0, 0), 10, 10, FVec(0, 12), 10, 10);
  EXPECT_TRUE(r.hit);
  // Separation normal pushes A away from B (upward).
  EXPECT_NEAR(r.normal.x, 0.0f, 1e-5f);
  EXPECT_NEAR(r.normal.y, -1.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 8.0f, 1e-5f);
}

TEST(Collision, AABBAABBContained) {
  auto r = TestAABBAABB(FVec(0, 0), 20, 20, FVec(5, 5), 5, 5);
  EXPECT_TRUE(r.hit);
}

TEST(Collision, CircleAABBOverlap) {
  auto r = TestCircleAABB(FVec(15, 0), 10, FVec(0, 0), 10, 10);
  EXPECT_TRUE(r.hit);
  EXPECT_NEAR(r.normal.x, 1.0f, 1e-5f);
  EXPECT_NEAR(r.normal.y, 0.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 5.0f, 1e-5f);
}

TEST(Collision, CircleAABBNoOverlap) {
  auto r = TestCircleAABB(FVec(25, 0), 5, FVec(0, 0), 10, 10);
  EXPECT_FALSE(r.hit);
}

TEST(Collision, CircleAABBCenterInside) {
  auto r = TestCircleAABB(FVec(5, 0), 10, FVec(0, 0), 20, 20);
  EXPECT_TRUE(r.hit);
  // Center is inside AABB, should push toward nearest edge.
  EXPECT_GT(r.depth, 0);
}

TEST(Collision, CircleAABBCorner) {
  // Circle overlapping the corner of the AABB.
  auto r = TestCircleAABB(FVec(13, 13), 5, FVec(0, 0), 10, 10);
  EXPECT_TRUE(r.hit);
  // Normal should push circle away from corner, roughly toward (1, 1).
  float len = std::sqrt(r.normal.x * r.normal.x + r.normal.y * r.normal.y);
  EXPECT_NEAR(len, 1.0f, 1e-4f);
  EXPECT_GT(r.normal.x, 0);
  EXPECT_GT(r.normal.y, 0);
}

TEST(Collision, TestShapesDispatch) {
  CollisionShape circle = MakeCircle(10);
  CollisionShape aabb = MakeAABB(20, 20);

  // Circle vs Circle
  auto r1 = TestShapes(circle, FVec(0, 0), circle, FVec(15, 0));
  EXPECT_TRUE(r1.hit);

  // AABB vs AABB
  auto r2 = TestShapes(aabb, FVec(0, 0), aabb, FVec(15, 0));
  EXPECT_TRUE(r2.hit);

  // Circle vs AABB
  auto r3 = TestShapes(circle, FVec(15, 0), aabb, FVec(0, 0));
  EXPECT_TRUE(r3.hit);

  // AABB vs Circle (reversed)
  auto r4 = TestShapes(aabb, FVec(0, 0), circle, FVec(15, 0));
  EXPECT_TRUE(r4.hit);
  // Separation normal pushes A (AABB) away from B (circle), i.e. leftward.
  EXPECT_LT(r4.normal.x, 0);
}

TEST(Collision, RaycastCircleHit) {
  auto result = RaycastCircle(FVec(0, 0), FVec(1, 0), 100.0f, FVec(50, 0), 10);
  EXPECT_TRUE(result.hit);
  EXPECT_NEAR(result.t, 40.0f, 1e-3f);
  EXPECT_NEAR(result.normal.x, -1.0f, 1e-3f);
}

TEST(Collision, RaycastCircleMiss) {
  auto result = RaycastCircle(FVec(0, 0), FVec(1, 0), 100.0f, FVec(0, 50), 10);
  EXPECT_FALSE(result.hit);
}

TEST(Collision, RaycastAABBHit) {
  auto result =
      RaycastAABB(FVec(0, 0), FVec(1, 0), 100.0f, FVec(50, 0), 10, 10);
  EXPECT_TRUE(result.hit);
  EXPECT_NEAR(result.t, 40.0f, 1e-3f);
  EXPECT_NEAR(result.normal.x, -1.0f, 1e-3f);
}

TEST(Collision, RaycastAABBMiss) {
  auto result =
      RaycastAABB(FVec(0, 0), FVec(1, 0), 100.0f, FVec(0, 50), 10, 10);
  EXPECT_FALSE(result.hit);
}

TEST(Collision, PointInCircle) {
  CollisionShape c = MakeCircle(10);
  EXPECT_TRUE(PointInShape(FVec(5, 5), c, FVec(0, 0)));
  EXPECT_FALSE(PointInShape(FVec(15, 0), c, FVec(0, 0)));
}

TEST(Collision, PointInAABB) {
  CollisionShape a = MakeAABB(20, 20);
  EXPECT_TRUE(PointInShape(FVec(5, 5), a, FVec(0, 0)));
  EXPECT_FALSE(PointInShape(FVec(15, 0), a, FVec(0, 0)));
}

// --- Collision World Tests ---

TEST(CollisionWorld, AddRemove) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);
  world.Update();

  CollisionShape circle = MakeCircle(10);
  auto h = world.Add(circle, FVec(100, 100), {}, false, 0);
  EXPECT_TRUE(world.IsValid(h));
  EXPECT_EQ(world.active_count(), 1u);

  world.Remove(h);
  EXPECT_FALSE(world.IsValid(h));
  EXPECT_EQ(world.active_count(), 0u);
}

TEST(CollisionWorld, HandleGenerations) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h1 = world.Add(circle, FVec(0, 0), {}, false, 0);
  world.Remove(h1);

  // Re-adding should reuse the same slot but with a new generation.
  auto h2 = world.Add(circle, FVec(0, 0), {}, false, 0);
  EXPECT_EQ(h1.index, h2.index);
  EXPECT_NE(h1.generation, h2.generation);

  // Old handle is invalid.
  EXPECT_FALSE(world.IsValid(h1));
  EXPECT_TRUE(world.IsValid(h2));
}

TEST(CollisionWorld, GetSetPosition) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape aabb = MakeAABB(20, 20);
  auto h = world.Add(aabb, FVec(10, 20), {}, false, 0);
  FVec2 pos = world.GetPosition(h);
  EXPECT_NEAR(pos.x, 10.0f, 1e-5f);
  EXPECT_NEAR(pos.y, 20.0f, 1e-5f);

  world.SetPosition(h, FVec(50, 60));
  pos = world.GetPosition(h);
  EXPECT_NEAR(pos.x, 50.0f, 1e-5f);
  EXPECT_NEAR(pos.y, 60.0f, 1e-5f);
}

TEST(CollisionWorld, OverlapQuery) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h1 = world.Add(circle, FVec(0, 0), {}, false, 0);
  auto h2 = world.Add(circle, FVec(15, 0), {}, false, 0);
  auto h3 = world.Add(circle, FVec(100, 100), {}, false, 0);
  world.Update();

  CollisionWorld::OverlapResult results[64];
  uint32_t count = world.GetOverlaps(h1, results, 64);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(results[0].handle, h2);

  // h3 is too far away.
  count = world.GetOverlaps(h3, results, 64);
  EXPECT_EQ(count, 0u);

  (void)h3;  // Suppress unused warning.
}

TEST(CollisionWorld, CollisionFiltering) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);

  // h1 is category 0x0001, detects 0x0002
  auto h1 = world.Add(circle, FVec(0, 0), {0x0001, 0x0002}, false, 0);
  // h2 is category 0x0002, detects 0x0001
  auto h2 = world.Add(circle, FVec(15, 0), {0x0002, 0x0001}, false, 0);
  // h3 is category 0x0004, detects 0x0004 (doesn't match h1)
  auto h3 = world.Add(circle, FVec(5, 0), {0x0004, 0x0004}, false, 0);
  world.Update();

  CollisionWorld::OverlapResult results[64];

  // h1 should see h2 (categories match masks).
  uint32_t count = world.GetOverlaps(h1, results, 64);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(results[0].handle, h2);

  (void)h3;
}

TEST(CollisionWorld, Raycast) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h = world.Add(circle, FVec(50, 0), {}, false, 0);
  world.Update();

  CollisionWorld::RaycastHit hit;
  bool found = world.Raycast(FVec(0, 0), FVec(1, 0), 100, 0xFFFF, &hit);
  EXPECT_TRUE(found);
  EXPECT_EQ(hit.handle, h);
  EXPECT_NEAR(hit.t, 40.0f, 1e-2f);
}

TEST(CollisionWorld, RaycastMiss) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  world.Add(circle, FVec(0, 50), {}, false, 0);
  world.Update();

  CollisionWorld::RaycastHit hit;
  bool found = world.Raycast(FVec(0, 0), FVec(1, 0), 100, 0xFFFF, &hit);
  EXPECT_FALSE(found);
}

TEST(CollisionWorld, QueryPoint) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h = world.Add(circle, FVec(0, 0), {}, false, 0);
  world.Update();

  ColliderHandle results[64];
  uint32_t count = world.QueryPoint(FVec(5, 5), 0xFFFF, results, 64);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(results[0], h);

  count = world.QueryPoint(FVec(50, 50), 0xFFFF, results, 64);
  EXPECT_EQ(count, 0u);
}

TEST(CollisionWorld, MoveAndSlide) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  CollisionShape wall = MakeAABB(200, 20);

  auto player = world.Add(circle, FVec(100, 80), {}, false, 0);
  world.Add(wall, FVec(100, 100), {}, false, 0);  // Wall below player.
  world.Update();

  // Move player downward into wall. Keep velocity small enough that the
  // circle center doesn't pass the wall center (discrete resolution limit).
  auto result = world.MoveAndSlide(player, FVec(0, 15));

  // Player should be pushed out of the wall.
  EXPECT_LT(result.position.y, 100.0f);
  EXPECT_GE(result.contact_count, 1u);
}

TEST(CollisionWorld, MoveAndCollide) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  CollisionShape wall = MakeAABB(200, 20);

  auto player = world.Add(circle, FVec(100, 80), {}, false, 0);
  world.Add(wall, FVec(100, 100), {}, false, 0);
  world.Update();

  auto result = world.MoveAndCollide(player, FVec(0, 30));
  EXPECT_GE(result.contact_count, 1u);
}

TEST(CollisionWorld, TriggerDetection) {
  auto* alloc = SystemAllocator::Instance();
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h1 = world.Add(circle, FVec(0, 0), {}, true, 0);  // trigger
  auto h2 = world.Add(circle, FVec(100, 100), {}, false, 0);

  // First frame: no overlaps.
  world.Update();
  EXPECT_EQ(world.new_trigger_count(), 0u);

  // Move h2 into h1's range.
  world.SetPosition(h2, FVec(5, 0));
  world.Update();
  EXPECT_GE(world.new_trigger_count(), 1u);

  // Next frame: still overlapping, no new triggers.
  world.Update();
  EXPECT_EQ(world.new_trigger_count(), 0u);

  // Move h2 away.
  world.SetPosition(h2, FVec(100, 100));
  world.Update();
  EXPECT_GE(world.lost_trigger_count(), 1u);

  (void)h1;
}

TEST(Easing, BoundaryValues) {
  for (int i = 0; i < kEasingCount; ++i) {
    EasingType type = static_cast<EasingType>(i);
    float at_zero = Ease(type, 0.0f);
    float at_one = Ease(type, 1.0f);
    EXPECT_NEAR(at_zero, 0.0f, 1e-5f) << "Easing " << i << " at t=0";
    EXPECT_NEAR(at_one, 1.0f, 1e-5f) << "Easing " << i << " at t=1";
  }
}

TEST(Easing, Monotonicity) {
  // Most easings should be monotonically increasing (except elastic/back which
  // overshoot). Test the simple ones.
  EasingType monotonic[] = {kLinear,   kInQuad,    kOutQuad,    kInOutQuad,
                            kInCubic,  kOutCubic,  kInOutCubic, kInSine,
                            kOutSine,  kInOutSine, kInExpo,     kOutExpo,
                            kInOutExpo};
  for (EasingType type : monotonic) {
    float prev = 0.0f;
    for (int i = 1; i <= 100; ++i) {
      float t = static_cast<float>(i) / 100.0f;
      float val = Ease(type, t);
      EXPECT_GE(val, prev - 1e-6f) << "Easing " << type << " at t=" << t;
      prev = val;
    }
  }
}

TEST(Easing, LinearIsIdentity) {
  for (int i = 0; i <= 10; ++i) {
    float t = static_cast<float>(i) / 10.0f;
    EXPECT_NEAR(Ease(kLinear, t), t, 1e-6f);
  }
}

TEST(Easing, InOutSymmetry) {
  // in-out curves should satisfy f(0.5) ≈ 0.5
  EasingType inout[] = {kInOutQuad,  kInOutCubic, kInOutQuart,
                        kInOutQuint, kInOutSine,  kInOutExpo,
                        kInOutCirc,  kInOutBack,  kInOutElastic};
  for (EasingType type : inout) {
    EXPECT_NEAR(Ease(type, 0.5f), 0.5f, 1e-4f) << "Easing " << type;
  }
}

TEST(InlineExecutor, SubmitRunsSynchronously) {
  InlineExecutor exec;
  int result = 0;
  Task task;
  task.fn = [](void* ud) { *static_cast<int*>(ud) = 42; };
  task.userdata = &result;
  task.cleanup = nullptr;
  exec.Submit(&task);
  EXPECT_EQ(result, 42);
  EXPECT_TRUE(task.done.load());
}

TEST(InlineExecutor, CleanupIsCalled) {
  InlineExecutor exec;
  int cleanup_count = 0;
  Task task;
  task.fn = [](void*) {};
  task.userdata = &cleanup_count;
  task.cleanup = [](void* ud) { ++*static_cast<int*>(ud); };
  exec.Submit(&task);
  EXPECT_EQ(cleanup_count, 1);
}

TEST(InlineExecutor, ParallelForRunsSequentially) {
  InlineExecutor exec;
  int sum = 0;
  exec.ParallelFor(
      10, 1,
      [](int start, int end, void* ctx) {
        auto* s = static_cast<int*>(ctx);
        for (int i = start; i < end; ++i) *s += i;
      },
      &sum);
  EXPECT_EQ(sum, 45);
}

TEST(ThreadPoolExecutor, SubmitAndWait) {
  ThreadPoolExecutor pool(SystemAllocator::Instance(), 2);
  pool.Start();
  std::atomic<int> result{0};
  Task task;
  task.fn = [](void* ud) {
    static_cast<std::atomic<int>*>(ud)->store(42, std::memory_order_relaxed);
  };
  task.userdata = &result;
  task.cleanup = nullptr;
  pool.Submit(&task);
  pool.Wait(&task);
  EXPECT_EQ(result.load(), 42);
  pool.Shutdown();
}

TEST(ThreadPoolExecutor, ParallelForSum) {
  ThreadPoolExecutor pool(SystemAllocator::Instance(), 4);
  pool.Start();
  constexpr int kN = 10000;
  int data[kN];
  for (int i = 0; i < kN; ++i) data[i] = i;

  std::atomic<int> sum{0};
  struct Ctx {
    int* data;
    std::atomic<int>* sum;
  };
  Ctx ctx{data, &sum};

  pool.ParallelFor(
      kN, /*min_batch=*/100,
      [](int start, int end, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        int local = 0;
        for (int i = start; i < end; ++i) local += c->data[i];
        c->sum->fetch_add(local, std::memory_order_relaxed);
      },
      &ctx);
  EXPECT_EQ(sum.load(), kN * (kN - 1) / 2);
  pool.Shutdown();
}

TEST(ThreadPoolExecutor, ParallelForWithZeroCount) {
  ThreadPoolExecutor pool(SystemAllocator::Instance(), 2);
  pool.Start();
  bool called = false;
  pool.ParallelFor(
      0, 1, [](int, int, void* ud) { *static_cast<bool*>(ud) = true; },
      &called);
  EXPECT_FALSE(called);
  pool.Shutdown();
}

TEST(ThreadPoolExecutor, NumDefaultThreads) {
  size_t n = ThreadPoolExecutor::NumDefaultThreads();
  EXPECT_GE(n, 1u);
}

}  // namespace G
