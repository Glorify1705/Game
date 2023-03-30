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
#include "uninitialized.h"
#include "xxhash.h"

namespace G {
namespace internal {

inline uint64_t Hash(const char* s, size_t len) {
  return XXH64(s, len, 0xC0DE15D474);
}

inline int32_t MSIProbe(uint64_t hash, int exp, int32_t idx) {
  uint32_t mask = (static_cast<uint32_t>(1) << exp) - 1;
  uint32_t step = (hash >> (64 - exp)) | 1;
  return (idx + step) & mask;
}

}  // namespace internal

// Intended for static tables with keys that exist for the whole lifetime of the
// binary.
template <typename T, typename Allocator = SystemAllocator>
class LookupTable {
 public:
  LookupTable() { std::fill(key_lengths_.begin(), key_lengths_.end(), 0); }

  bool Lookup(std::string_view key, T* value = nullptr) const {
    const uint64_t h = internal::Hash(key.data(), key.size());
    for (int32_t i = h;;) {
      i = internal::MSIProbe(h, kLogTableSize, i);
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

  struct Insertion {
    std::string_view map_key;
    T* ptr = nullptr;
  };

  Insertion Insert(std::string_view key, T value) {
    const uint64_t h = internal::Hash(key.data(), key.size());
    for (int32_t i = h;;) {
      i = internal::MSIProbe(h, kLogTableSize, i);
      if (key_lengths_[i] == 0) {
        DCHECK(elements_ < keys_.size());
        auto* str = static_cast<char*>(strbufs_.Alloc(key.size(), /*align=*/1));
        std::memcpy(str, key.data(), key.size());
        keys_[i] = str;
        values_[i] = std::move(value);
        key_lengths_[i] = key.size();
        elements_++;
        return {.map_key = std::string_view(keys_[i], key_lengths_[i]),
                .ptr = &values_[i]};
      }
    }
    DIE("OOM");
  }

  template <typename Fn>
  void ForAll(Fn&& fn) {
    for (size_t i = 0; i < key_lengths_.size(); ++i) {
      if (key_lengths_[i] > 0) fn(keys_[i], values_[i]);
    }
  }

 private:
  inline static constexpr size_t kLogTableSize = 15;
  inline static constexpr size_t kKeysSize = 1 << 20;

  StaticAllocator<kKeysSize> strbufs_;
  std::array<const char*, 1 << kLogTableSize> keys_;
  std::array<size_t, 1 << kLogTableSize> key_lengths_;
  UninitializedArray<T, 1 << kLogTableSize> values_;
  size_t elements_ = 0;
};

}  // namespace G

#endif  // _GAME_LOOKUP_TABLE_H