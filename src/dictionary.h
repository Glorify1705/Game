#pragma once
#ifndef _GAME_DICTIONARY_H
#define _GAME_DICTIONARY_H

#include <array>

#include "allocators.h"
#include "libraries/rapidhash.h"
#include "logging.h"
#include "string_table.h"

namespace G {
namespace internal_dictionary {

inline uint64_t Hash(std::string_view s) {
  return rapidhash(s.data(), s.size());
}

}  // namespace internal_dictionary

template <typename T>
class Dictionary {
 public:
  Dictionary(Allocator* allocator) : allocator_(allocator) {}

  ~Dictionary() { Clear(); }

  bool Lookup(std::string_view key, T* value = nullptr) const {
    Node* const* n = &root_;
    const uint32_t handle = StringHandle(key);
    for (uint64_t h = internal_dictionary::Hash(key); *n; h <<= 2) {
      if ((*n)->handle == handle) break;
      n = &(*n)->child[h >> 62];
    }
    if (*n == nullptr) return false;
    if (value) *value = (*n)->value;
    return true;
  }

  T LookupOrDie(std::string_view key) const {
    T result;
    CHECK(Lookup(key, &result), "No key ", key, " found");
    return result;
  }

  bool Contains(std::string_view key) const { return Lookup(key, nullptr); }

  void Insert(std::string_view key, T value) {
    Node** n = &root_;
    const uint32_t handle = StringIntern(key);
    for (uint64_t h = internal_dictionary::Hash(key); *n; h <<= 2) {
      if ((*n)->handle == handle) break;
      n = &(*n)->child[h >> 62];
    }
    if (*n == nullptr) {
      *n = allocator_->New<Node>();
      (*n)->handle = handle;
    }
    (*n)->value = std::move(value);
  }

  void Clear() {
    Dealloc(root_);
    root_ = nullptr;
  }

  // Calls fn(string_view key, const T& value) for every entry in the tree.
  template <typename Fn>
  void ForEach(Fn fn) const {
    ForEachNode(root_, fn);
  }

 private:
  struct Node {
    std::array<Node*, 4> child;
    uint32_t handle = std::numeric_limits<uint32_t>::max();
    T value;
  };

  template <typename Fn>
  void ForEachNode(Node* n, Fn& fn) const {
    if (n == nullptr) return;
    fn(StringByHandle(n->handle), n->value);
    for (Node* child : n->child) {
      ForEachNode(child, fn);
    }
  }

  void Dealloc(Node* n) {
    if (n == nullptr) return;
    for (Node* child : n->child) {
      if (child != nullptr) Dealloc(child);
    }
    allocator_->Destroy(n);
  }

  Allocator* allocator_;
  Node* root_ = nullptr;
};

}  // namespace G

#endif  // _GAME_DICTIONARY_H
