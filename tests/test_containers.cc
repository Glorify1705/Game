#include "array.h"
#include "circular_buffer.h"
#include "dictionary.h"
#include "gmock/gmock-matchers.h"
#include "inlined_array.h"
#include "segmented_list.h"
#include "test_fixture.h"

namespace G {

// FixedArray

class FixedArrayTest : public AllocTest {};

TEST_F(FixedArrayTest, PushAndAccess) {
  FixedArray<int> array(3, alloc);
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

TEST_F(FixedArrayTest, WithAllocator) {
  FixedArray<int> array(3, alloc);
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

// DynArray

class DynArrayTest : public AllocTest {};

TEST_F(DynArrayTest, PushAndIterate) {
  DynArray<int> array(alloc);
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

TEST_F(DynArrayTest, Move) {
  DynArray<int> array(alloc);
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

// Dictionary

class DictionaryTest : public AllocTest {};

TEST_F(DictionaryTest, InsertAndLookup) {
  Dictionary<int> dictionary(alloc);
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

// BlockAllocator

class BlockAllocatorTest : public AllocTest {};

TEST_F(BlockAllocatorTest, AllocAndDealloc) {
  BlockAllocator<uint32_t> pool(alloc, 2);
  uint32_t* p = pool.AllocBlock();
  uint32_t* q = pool.AllocBlock();
  EXPECT_NE(p, q);
  pool.DeallocBlock(q);
  uint32_t* r = pool.AllocBlock();
  EXPECT_EQ(q, r);
}

// FreeList

TEST(FreeListTest, AllocAndDealloc) {
  StaticAllocator<1024> arena;
  FreeList<uint32_t> freelist(&arena);
  uint32_t* p = freelist.Alloc();
  uint32_t* q = freelist.Alloc();
  EXPECT_NE(p, q);
  freelist.Dealloc(q);
  uint32_t* r = freelist.Alloc();
  EXPECT_EQ(q, r);
}

// CircularBuffer

class CircularBufferTest : public AllocTest {};

TEST_F(CircularBufferTest, EmptyOnConstruction) {
  CircularBuffer<int> buf(4, alloc);
  EXPECT_TRUE(buf.empty());
  EXPECT_FALSE(buf.full());
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.capacity(), 4u);
}

TEST_F(CircularBufferTest, PushAndSize) {
  CircularBuffer<int> buf(4, alloc);
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

TEST_F(CircularBufferTest, FrontAndBack) {
  CircularBuffer<int> buf(4, alloc);
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

TEST_F(CircularBufferTest, PopReturnsOldest) {
  CircularBuffer<int> buf(4, alloc);
  buf.Push(10);
  buf.Push(20);
  buf.Push(30);
  EXPECT_EQ(buf.Pop(), 10);
  EXPECT_EQ(buf.Pop(), 20);
  EXPECT_EQ(buf.Pop(), 30);
  EXPECT_TRUE(buf.empty());
}

TEST_F(CircularBufferTest, PopUpdatesState) {
  CircularBuffer<int> buf(4, alloc);
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

TEST_F(CircularBufferTest, IndexedAccess) {
  CircularBuffer<int> buf(4, alloc);
  buf.Push(10);
  buf.Push(20);
  buf.Push(30);
  EXPECT_EQ(buf[0], 10);
  EXPECT_EQ(buf[1], 20);
  EXPECT_EQ(buf[2], 30);
}

TEST_F(CircularBufferTest, IndexedAccessAfterPop) {
  CircularBuffer<int> buf(4, alloc);
  buf.Push(10);
  buf.Push(20);
  buf.Push(30);
  buf.Pop();
  EXPECT_EQ(buf[0], 20);
  EXPECT_EQ(buf[1], 30);
}

TEST_F(CircularBufferTest, Wraparound) {
  CircularBuffer<int> buf(3, alloc);
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

TEST_F(CircularBufferTest, OverwriteWhenFull) {
  CircularBuffer<int> buf(3, alloc);
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

TEST_F(CircularBufferTest, OverwriteMultiple) {
  CircularBuffer<int> buf(3, alloc);
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

TEST_F(CircularBufferTest, PushPopInterleaved) {
  CircularBuffer<int> buf(2, alloc);
  for (int i = 0; i < 100; ++i) {
    buf.Push(i);
    EXPECT_EQ(buf.Pop(), i);
    EXPECT_TRUE(buf.empty());
  }
}

TEST_F(CircularBufferTest, FillEmptyRepeatedly) {
  CircularBuffer<int> buf(3, alloc);
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

TEST_F(CircularBufferTest, SizeOneBuffer) {
  CircularBuffer<int> buf(1, alloc);
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

TEST_F(CircularBufferTest, SizeOneOverwrite) {
  CircularBuffer<int> buf(1, alloc);
  buf.Push(1);
  buf.Push(2);
  EXPECT_TRUE(buf.full());
  EXPECT_EQ(buf.size(), 1u);
  EXPECT_EQ(buf.front(), 2);
  EXPECT_EQ(buf.back(), 2);
}

TEST_F(CircularBufferTest, ConstAccess) {
  CircularBuffer<int> buf(4, alloc);
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

// SegmentedList

class SegmentedListTest : public AllocTest {};

TEST_F(SegmentedListTest, EmptyOnConstruction) {
  SegmentedList<int> list(alloc);
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.size(), 0u);
  EXPECT_EQ(list.capacity(), 0u);
}

TEST_F(SegmentedListTest, PushAndAccess) {
  SegmentedList<int> list(alloc);
  for (int i = 0; i < 100; ++i) {
    list.Push(i);
  }
  EXPECT_EQ(list.size(), 100u);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(list[i], i);
  }
}

TEST_F(SegmentedListTest, StablePointers) {
  SegmentedList<int> list(alloc);
  int* ptrs[64];
  for (int i = 0; i < 64; ++i) {
    ptrs[i] = list.Push(i);
  }
  // All pointers remain valid after growth.
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(*ptrs[i], i);
  }
}

TEST_F(SegmentedListTest, PtrAt) {
  SegmentedList<int> list(alloc);
  for (int i = 0; i < 32; ++i) {
    list.Push(i * 10);
  }
  for (int i = 0; i < 32; ++i) {
    EXPECT_EQ(*list.PtrAt(i), i * 10);
  }
}

TEST_F(SegmentedListTest, Pop) {
  SegmentedList<int> list(alloc);
  list.Push(1);
  list.Push(2);
  list.Push(3);
  EXPECT_EQ(list.size(), 3u);
  EXPECT_EQ(list.back(), 3);
  list.Pop();
  EXPECT_EQ(list.size(), 2u);
  EXPECT_EQ(list.back(), 2);
}

TEST_F(SegmentedListTest, Clear) {
  SegmentedList<int> list(alloc);
  for (int i = 0; i < 50; ++i) list.Push(i);
  list.Clear();
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.size(), 0u);
  // Can push again after clear.
  list.Push(42);
  EXPECT_EQ(list[0], 42);
}

TEST_F(SegmentedListTest, Emplace) {
  SegmentedList<int> list(alloc);
  int* p = list.Emplace(42);
  EXPECT_EQ(*p, 42);
  EXPECT_EQ(list[0], 42);
}

TEST_F(SegmentedListTest, Iterator) {
  SegmentedList<int> list(alloc);
  for (int i = 0; i < 20; ++i) list.Push(i);
  int expected = 0;
  for (int v : list) {
    EXPECT_EQ(v, expected);
    expected++;
  }
  EXPECT_EQ(expected, 20);
}

TEST_F(SegmentedListTest, ConstAccess) {
  SegmentedList<int> list(alloc);
  for (int i = 0; i < 10; ++i) list.Push(i);
  const auto& clist = list;
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(clist[i], i);
  }
  EXPECT_EQ(clist.back(), 9);
  EXPECT_EQ(*clist.PtrAt(5), 5);
}

TEST_F(SegmentedListTest, CustomPrealloc) {
  SegmentedList<int, 4> list(alloc);
  for (int i = 0; i < 100; ++i) {
    list.Push(i);
  }
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(list[i], i);
  }
}

TEST_F(SegmentedListTest, LargePrealloc) {
  SegmentedList<int, 64> list(alloc);
  for (int i = 0; i < 1000; ++i) {
    list.Push(i);
  }
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(list[i], i);
  }
}

TEST_F(SegmentedListTest, BoundaryIndices) {
  // Test indices right at shelf boundaries with P=4.
  SegmentedList<int, 4> list(alloc);
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

TEST_F(SegmentedListTest, WithArenaAllocator) {
  StaticAllocator<4096> arena;
  SegmentedList<int> list(&arena);
  for (int i = 0; i < 50; ++i) {
    list.Push(i);
  }
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(list[i], i);
  }
}

// InlinedArray

class InlinedArrayTest : public AllocTest {};

TEST_F(InlinedArrayTest, EmptyOnConstruction) {
  InlinedArray<int, 4> arr(alloc);
  EXPECT_TRUE(arr.empty());
  EXPECT_EQ(arr.size(), 0u);
  EXPECT_EQ(arr.capacity(), 4u);
}

TEST_F(InlinedArrayTest, PushWithinInline) {
  InlinedArray<int, 4> arr(alloc);
  arr.Push(10);
  arr.Push(20);
  arr.Push(30);
  EXPECT_EQ(arr.size(), 3u);
  EXPECT_EQ(arr[0], 10);
  EXPECT_EQ(arr[1], 20);
  EXPECT_EQ(arr[2], 30);
  EXPECT_EQ(arr.capacity(), 4u);
}

TEST_F(InlinedArrayTest, FillInline) {
  InlinedArray<int, 4> arr(alloc);
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

TEST_F(InlinedArrayTest, SpillToHeap) {
  InlinedArray<int, 4> arr(alloc);
  for (int i = 0; i < 5; ++i) arr.Push(i);
  EXPECT_EQ(arr.size(), 5u);
  EXPECT_GT(arr.capacity(), 4u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(arr[i], i);
  }
}

TEST_F(InlinedArrayTest, GrowOnHeap) {
  InlinedArray<int, 2> arr(alloc);
  for (int i = 0; i < 100; ++i) arr.Push(i);
  EXPECT_EQ(arr.size(), 100u);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(arr[i], i);
  }
}

TEST_F(InlinedArrayTest, Pop) {
  InlinedArray<int, 4> arr(alloc);
  arr.Push(1);
  arr.Push(2);
  arr.Push(3);
  EXPECT_EQ(arr.back(), 3);
  arr.Pop();
  EXPECT_EQ(arr.size(), 2u);
  EXPECT_EQ(arr.back(), 2);
}

TEST_F(InlinedArrayTest, Clear) {
  InlinedArray<int, 4> arr(alloc);
  arr.Push(1);
  arr.Push(2);
  arr.Clear();
  EXPECT_TRUE(arr.empty());
  arr.Push(42);
  EXPECT_EQ(arr[0], 42);
}

TEST_F(InlinedArrayTest, Emplace) {
  InlinedArray<int, 4> arr(alloc);
  arr.Emplace(42);
  EXPECT_EQ(arr[0], 42);
}

TEST_F(InlinedArrayTest, Iterator) {
  InlinedArray<int, 4> arr(alloc);
  for (int i = 0; i < 3; ++i) arr.Push(i * 10);
  int expected = 0;
  for (int v : arr) {
    EXPECT_EQ(v, expected);
    expected += 10;
  }
}

TEST_F(InlinedArrayTest, IteratorAfterSpill) {
  InlinedArray<int, 2> arr(alloc);
  for (int i = 0; i < 10; ++i) arr.Push(i);
  int expected = 0;
  for (int v : arr) {
    EXPECT_EQ(v, expected);
    expected++;
  }
  EXPECT_EQ(expected, 10);
}

TEST_F(InlinedArrayTest, ConstAccess) {
  InlinedArray<int, 4> arr(alloc);
  arr.Push(10);
  arr.Push(20);
  const auto& carr = arr;
  EXPECT_EQ(carr[0], 10);
  EXPECT_EQ(carr[1], 20);
  EXPECT_EQ(carr.back(), 20);
  EXPECT_EQ(carr.size(), 2u);
}

TEST_F(InlinedArrayTest, SingleElementInline) {
  InlinedArray<int, 1> arr(alloc);
  arr.Push(42);
  EXPECT_EQ(arr[0], 42);
  EXPECT_EQ(arr.capacity(), 1u);
  arr.Push(99);
  EXPECT_EQ(arr.size(), 2u);
  EXPECT_GT(arr.capacity(), 1u);
  EXPECT_EQ(arr[0], 42);
  EXPECT_EQ(arr[1], 99);
}

TEST_F(InlinedArrayTest, LargeInline) {
  InlinedArray<int, 64> arr(alloc);
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

TEST_F(InlinedArrayTest, WithArenaAllocator) {
  StaticAllocator<4096> arena;
  InlinedArray<int, 8> arr(&arena);
  for (int i = 0; i < 50; ++i) arr.Push(i);
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(arr[i], i);
  }
}

TEST_F(InlinedArrayTest, DataPointer) {
  InlinedArray<int, 4> arr(alloc);
  arr.Push(1);
  arr.Push(2);
  int* p = arr.data();
  EXPECT_EQ(p[0], 1);
  EXPECT_EQ(p[1], 2);
}

}  // namespace G
