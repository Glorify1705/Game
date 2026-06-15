#pragma once
#ifndef _GAME_SEGMENTED_LIST_H
#define _GAME_SEGMENTED_LIST_H

#include <cstring>
#include <utility>

#include "allocators.h"
#include "bits.h"
#include "logging.h"

namespace G {

// A growable list that never moves elements. Instead of reallocating
// and copying like DynArray, it allocates new segments of increasing
// size. Elements get stable pointers for their lifetime.
//
// Segments follow a power-of-two doubling scheme:
//   Shelf 0: P elements
//   Shelf 1: P elements
//   Shelf 2: 2P elements
//   Shelf 3: 4P elements
//   Shelf n (n >= 1): P * 2^(n-1) elements
//
// Indexing is O(1) using a single clz instruction to map a flat
// index to a (shelf, offset) pair.
//
// P must be a power of 2.
template <typename T, size_t P = 8>
class SegmentedList {
  static_assert(P > 0 && (P & (P - 1)) == 0, "P must be a power of 2");

 public:
  explicit SegmentedList(Allocator* allocator) : allocator_(allocator) {}

  ~SegmentedList() {
    for (size_t i = 0; i < shelves_allocated_; i++) {
      allocator_->DeallocArray(segments_[i], ShelfSize(i));
    }
  }

  T& operator[](size_t index) {
    CHECK(index < size_, index, " vs ", size_);
    auto [shelf, offset] = Locate(index);
    return segments_[shelf][offset];
  }

  const T& operator[](size_t index) const {
    CHECK(index < size_, index, " vs ", size_);
    auto [shelf, offset] = Locate(index);
    return segments_[shelf][offset];
  }

  T* Push(const T& value) {
    EnsureCapacity();
    auto [shelf, offset] = Locate(size_);
    T* ptr = &segments_[shelf][offset];
    ::new (ptr) T(value);
    size_++;
    return ptr;
  }

  T* Push(T&& value) {
    EnsureCapacity();
    auto [shelf, offset] = Locate(size_);
    T* ptr = &segments_[shelf][offset];
    ::new (ptr) T(std::move(value));
    size_++;
    return ptr;
  }

  template <typename... Args>
  T* Emplace(Args&&... args) {
    EnsureCapacity();
    auto [shelf, offset] = Locate(size_);
    T* ptr = &segments_[shelf][offset];
    ::new (ptr) T(std::forward<Args>(args)...);
    size_++;
    return ptr;
  }

  void Pop() {
    CHECK(size_ > 0);
    size_--;
  }

  void Clear() { size_ = 0; }

  T* PtrAt(size_t index) {
    CHECK(index < size_, index, " vs ", size_);
    auto [shelf, offset] = Locate(index);
    return &segments_[shelf][offset];
  }

  const T* PtrAt(size_t index) const {
    CHECK(index < size_, index, " vs ", size_);
    auto [shelf, offset] = Locate(index);
    return &segments_[shelf][offset];
  }

  T& back() {
    CHECK(size_ > 0);
    return (*this)[size_ - 1];
  }

  const T& back() const {
    CHECK(size_ > 0);
    return (*this)[size_ - 1];
  }

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  bool empty() const { return size_ == 0; }

  struct Iterator {
    SegmentedList* list;
    size_t index;

    T& operator*() { return (*list)[index]; }
    Iterator& operator++() {
      ++index;
      return *this;
    }
    bool operator!=(const Iterator& other) const {
      return index != other.index;
    }
  };

  struct ConstIterator {
    const SegmentedList* list;
    size_t index;

    const T& operator*() const { return (*list)[index]; }
    ConstIterator& operator++() {
      ++index;
      return *this;
    }
    bool operator!=(const ConstIterator& other) const {
      return index != other.index;
    }
  };

  Iterator begin() { return {this, 0}; }
  Iterator end() { return {this, size_}; }
  ConstIterator begin() const { return {this, 0}; }
  ConstIterator end() const { return {this, size_}; }

 private:
  // log2(P). Used to divide by P via right shift.
  static constexpr size_t kShift = Log2(P) - 1;
  static constexpr size_t kMaxShelves = 32;

  static constexpr size_t ShelfSize(size_t shelf) {
    return (shelf == 0) ? P : P << (shelf - 1);
  }

  // Map a flat index to (shelf_index, offset_within_shelf).
  // Uses a single clz instruction for the shelf lookup.
  std::pair<size_t, size_t> Locate(size_t index) const {
    if (index < P) return {0, index};
    size_t shelf = Log2(index >> kShift);
    size_t start = static_cast<size_t>(P) << (shelf - 1);
    return {shelf, index - start};
  }

  void EnsureCapacity() {
    if (size_ < capacity_) return;
    CHECK(shelves_allocated_ < kMaxShelves, "too many shelves");
    size_t shelf_size = ShelfSize(shelves_allocated_);
    segments_[shelves_allocated_] = allocator_->NewArray<T>(shelf_size);
    capacity_ += shelf_size;
    shelves_allocated_++;
  }

  Allocator* allocator_;
  T* segments_[kMaxShelves] = {};
  size_t size_ = 0;
  size_t capacity_ = 0;
  size_t shelves_allocated_ = 0;
};

}  // namespace G

#endif  // _GAME_SEGMENTED_LIST_H
