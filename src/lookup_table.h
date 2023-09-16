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

#if 0
template <typename T, typename Allocator = SystemAllocator>
class Dictionary {
 public:
  Dictionary() { hash_table_.Reserve(); }

  bool Lookup(std::string_view key, T* value = nullptr) const {
    const size_t hash = internal::Hash(key) & (hash_table_.size() - 1);
    for (size_t i = hash, probes = 0;probes < max_probe_length_; ++probes) {
      if (hash_table_[i].probe_seq_length == 0) return false;
      if (hash_table_[i].key_size != key.size()) continue;
      if (Key(hash_table_[i]) == key) return &entry.value;
      i = (i + 1) & (hash_table_.size() - 1);
    }
  }

  struct Insertion {
    std::string_view map_key;
    T* ptr = nullptr;
  };

  Insertion Insert(std::string_view key, T value) {
    return Emplace(key, std::move(value));
  }

  template <typename... Args>
  Insertion Emplace(std::string_view key, Args... args) {
    const size_t hash = internal::Hash(key) & (hash_table_.size() - 1);
    size_t probes = 1;
    for (size_t i = hash;) {
      if (hash_table_[i].key_length == key.size()) {
        const std::string_view hash_key = Key(hash_table_[i]);
        if (hash_key == key) return Insertion{hash_key, &hash_table_[i].value};
      }
      if (hash_table_[i].probe_seq_length == 0) {
        hash_table_[i].key_length = key.size();
        hash_table_[i].probe_seq_length = probes;
        *hash_table_[i].Init(std::forward(args)...);
      }
      if (probes > hash_table_[i].probe_seq_length) {
        // Robin Hood hashing.
      }
      probes++;
      i = (i + 1) & (hash_table_.size() - 1);
      if (i == hash) {
        ResizeAndRehash();
        return Insert(key, std::move(value));
      }
      max_probe_length_ = std::max(probes, max_probe_length_);
    }
  }

  template <typename Fn>
  void ForAll(Fn&& fn) {
    for (const auto& entry : hash_table_)  {
      if (entry.probe_seq_length == 0) continue;
      fn(Key(entry), *entry.value);
    }
  }

 private:  
  struct Entry {
    uint64_t storage_offset = 0;
    uint32_t probe_seq_length = 0;
    uint32_t key_length = 0;
    Uninitialized<T> value;
  };

  std::string_view Key(const Entry& entry) const {
    return std::string_view(&key_storage_[entry.storage_offset], entry.key_length);
  }

  DynArray<char, Allocator> key_storage_;
  DynArray<Entry, Allocator> hash_table_;
  uint32_t max_probe_length_ = 0;
};
#endif

}  // namespace G

#endif  // _GAME_LOOKUP_TABLE_H