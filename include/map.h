#pragma once
#ifndef _GAME_MAP_H
#define _GAME_MAP_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

  bool Lookup(const char* key, size_t length, T* value = nullptr) const {
    const uint64_t h = internal::Hash(key, length);
    for (int32_t i = h;;) {
      i = internal::MSIProbe(h, kTableSize, i);
      if (key_lengths_[i] == 0) break;
      if (key_lengths_[i] == length && !std::memcmp(keys_[i], key, length)) {
        if (value) *value = values_[i];
        return true;
      }
    }
    return false;
  }

  template <size_t N>
  bool Lookup(const char (&key)[N], T* value) const {
    return Lookup(key, N - 1, value);
  }

  template <size_t N>
  void Insert(const char (&key)[N], T value) {
    Insert(key, N - 1, value);
  }

  void Insert(const char* key, size_t length, T value) {
    const uint64_t h = internal::Hash(key, length);
    for (int32_t i = h;;) {
      i = internal::MSIProbe(h, kTableSize, i);
      if (key_lengths_[i] == 0) {
        DCHECK(elements_ < keys_.size());
        values_[i] = std::move(value);
        keys_[i] = key;
        key_lengths_[i] = length;
        elements_++;
        break;
      }
    }
  }

 private:
  inline static constexpr size_t kTableSize = 15;
  std::array<const char*, 1 << kTableSize> keys_;
  std::array<size_t, 1 << kTableSize> key_lengths_;
  std::array<T, 1 << kTableSize> values_;
  size_t elements_ = 0;
};

}  // namespace G

#endif  // _GAME_MAP_H