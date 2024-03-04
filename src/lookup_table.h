#pragma once
#ifndef _GAME_LOOKUP_TABLE_H
#define _GAME_LOOKUP_TABLE_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "allocators.h"
#include "array.h"
#include "xxhash.h"

namespace G {
namespace internal_table {

inline uint64_t Hash(std::string_view s) {
  return XXH64(s.data(), s.size(), 0xC0DE15D474);
}

inline int32_t MSIProbe(uint64_t hash, int exp, int32_t idx) {
  uint32_t mask = (static_cast<uint32_t>(1) << exp) - 1;
  uint32_t step = (hash >> (64 - exp)) | 1;
  return (idx + step) & mask;
}

}  // namespace internal_table

// Intended for static tables with keys that exist for the whole lifetime of the
// binary.
template <typename T, size_t kLogTableSize = 10>
class LookupTable {
 public:
  LookupTable(Allocator* allocator)
      : allocator_(allocator),
        keys_(1 << kLogTableSize, allocator),
        key_strs_(kKeysSize, allocator),
        key_lengths_(1 << kLogTableSize, allocator),
        values_(1 << kLogTableSize, allocator) {
    key_lengths_.Resize(key_lengths_.capacity());
    keys_.Resize(keys_.capacity());
    values_.Resize(values_.capacity());
    std::fill(key_lengths_.begin(), key_lengths_.end(), 0);
  }

  bool Lookup(std::string_view key, T* value = nullptr) const {
    const uint64_t h = internal_table::Hash(key);
    for (int32_t i = h;;) {
      i = internal_table::MSIProbe(h, kLogTableSize, i);
      if (key_lengths_[i] == 0) break;
      if (key_lengths_[i] == key.size() &&
          !std::memcmp(keys_[i], key.data(), key.size())) {
        if (value) *value = values_[i];
        return true;
      }
    }
    return false;
  }

  T* LookupOrDie(std::string_view key) const {
    T* result;
    CHECK(Lookup(key, &result), "No key ", key);
    return result;
  }

  bool Contains(std::string_view key) const { return Lookup(key, nullptr); }

  size_t size() const { return elements_; }

  void Insert(std::string_view key, T value) {
    const uint64_t h = internal_table::Hash(key);
    for (int32_t i = h;;) {
      i = internal_table::MSIProbe(h, kLogTableSize, i);
      if (key_lengths_[i] != 0) {
        if (key_lengths_[i] == key.size() &&
            !std::memcmp(keys_[i], key.data(), key.size())) {
          values_[i] = std::move(value);
          return;
        }
      } else {
        DCHECK(elements_ < keys_.size());
        auto* str = key_strs_.Insert(key.data(), key.size());
        keys_[i] = str;
        values_[i] = std::move(value);
        key_lengths_[i] = key.size();
        elements_++;
        return;
      }
    }
    DIE("OOM");
  }

 private:
  inline static constexpr size_t kKeysSize = 1 << 15;

  Allocator* allocator_;
  FixedArray<char*> keys_;
  FixedArray<char> key_strs_;
  FixedArray<size_t> key_lengths_;
  FixedArray<T> values_;
  size_t elements_ = 0;
};

}  // namespace G

#endif  // _GAME_LOOKUP_TABLE_H