#pragma once
#ifndef _GAME_MAP_H
#define _GAME_MAP_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "allocators.h"

namespace G {
namespace internal {
// MSI probe and hash from https://nullprogram.com/blog/2022/08/08/.
// Does not need to be very good, just fast.
inline uint64_t Hash(const char* s, size_t len) {
  uint64_t h = 0x100;
  for (size_t i = 0; i < len; i++) {
    h ^= s[i] & 255;
    h *= 1111111111111111111;
  }
  return h;
}

inline int32_t MSIProbe(uint64_t hash, int exp, int32_t idx) {
  uint32_t mask = (static_cast<uint32_t>(1) << exp) - 1;
  uint32_t step = (hash >> (64 - exp)) | 1;
  return (idx + step) & mask;
}

}  // namespace internal

// Intended for static tables with keys that exist for the whole lifetime of the
// binary.
template <typename T>
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

  void Insert(std::string_view key, T value) {
    const uint64_t h = internal::Hash(key.data(), key.size());
    for (int32_t i = h;;) {
      i = internal::MSIProbe(h, kLogTableSize, i);
      if (key_lengths_[i] == 0) {
        DCHECK(elements_ < keys_.size());
        DCHECK(pos_ + key.size() < kKeysSize);
        auto* str = strbufs_->AllocArray<char>(key.size());
        std::memcpy(str, key.data(), key.size());
        keys_[i] = str;
        pos_ += key.size();
        values_[i] = std::move(value);
        key_lengths_[i] = key.size();
        elements_++;
        break;
      }
    }
  }

 private:
  inline static constexpr size_t kLogTableSize = 15;
  inline static constexpr size_t kKeysSize = 1 << 20;
  FixedArena<kKeysSize, BumpAllocator> strbufs_;
  size_t pos_ = 0;
  std::array<const char*, 1 << kLogTableSize> keys_;
  std::array<size_t, 1 << kLogTableSize> key_lengths_;
  std::array<T, 1 << kLogTableSize> values_;
  size_t elements_ = 0;
};

}  // namespace G

#endif  // _GAME_MAP_H