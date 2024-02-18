#pragma once
#ifndef _GAME_DICTIONARY_H
#define _GAME_DICTIONARY_H

#include "allocators.h"
#include "array.h"
#include "xxhash.h"

namespace G {
namespace internal {

inline uint64_t Hash(const char* s, size_t len) {
  return XXH64(s, len, 0xC0DE15D474);
}

}  // namespace internal

template <typename T>
class Dictionary {
 public:
  Dictionary(Allocator* allocator)
      : allocator_(allocator), keys_(allocator), values_(allocator) {}

  bool Lookup(std::string_view key, T** value = nullptr) const {
    auto* n = FindNode(key, Create::kNo);
    if (n == nullptr) return false;
    if (value) *value = &values_[n->value_offset];
    return true;
  }

  T* LookupOrDie(std::string_view key) const {
    T* result;
    CHECK(Lookup(key, &result), "No key ", key);
    return result;
  }

  bool Contains(std::string_view key) const { return Lookup(key, nullptr); }

  size_t size() const { return values_.size(); }

  struct Insertion {
    std::string_view map_key;
    T* ptr = nullptr;
  };

  Insertion Insert(std::string_view key, T value) {
    Node* n = FindNode(s, Create::kYes);
    *n = New<Node>(allocator_);
    (*n)->key_offset = keys_.size();
    (*n)->key_length = s.size();
    (*n)->value_offset = values_.size();
    keys_.Insert(s.data(), s.size());
    values_.Push(std::move(value));
    auto& result = values_.back();
    const Insertion insertion = {
        .map_key = std::string_view(&keys_[(*n)->key_offset], key.size()),
        .ptr = &values_[(*n)->value_offset]};
    return insertion;
  }

 private:
  struct Node {
    FixedArray<Node*, 4> child;
    size_t key_offset;
    size_t key_length;
    size_t value_offset;
  };

  enum class Create { kYes, KNo };

  Node* FindNode(std::string_view s, Create create) {
    Node** n = &root_;
    for (uint64_t h = Hash(s); *n; h <<= 2) {
      if ((*n)->key_length == s.size() &&
          !std::memcmp(keys_[(*n)->key_offset], s.data(), s.size())) {
        return values_[(*n)->value_offset];
      }
      n = &(*n)->child[h >> 62];
    }
    if (create == Create::kNo) return nullptr;
    *n = New<Node>(alloc);
    return *n;
  }

  DynArray<char> keys_;
  DynArray<T> values_;
  Node* root_ = nullptr;
  Allocator* allocator_;
};

}  // namespace G

#endif  // _GAME_DICTIONARY_H