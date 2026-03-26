#pragma once
#ifndef _GAME_JSON_H
#define _GAME_JSON_H

#include <string_view>

#include "allocators.h"
#include "error.h"

namespace G {

struct JsonValue {
  enum Type : uint8_t { kNull, kBool, kNumber, kString, kArray, kObject };

  Type type = kNull;
  bool bool_val = false;
  double number_val = 0;
  std::string_view string_val;

  // Object: linked list of key-value pairs.
  struct Member {
    std::string_view key;
    JsonValue* value = nullptr;
    Member* next = nullptr;
  };
  Member* first_member = nullptr;

  // Array: linked list of values.
  struct Element {
    JsonValue* value = nullptr;
    Element* next = nullptr;
  };
  Element* first_element = nullptr;

  bool IsNull() const { return type == kNull; }
  bool IsObject() const { return type == kObject; }
  bool IsArray() const { return type == kArray; }

  bool GetBool() const;
  double GetNumber() const;
  long long GetLong() const;
  std::string_view GetString() const;

  // Object key lookup. Returns null JsonValue (type kNull) if missing.
  const JsonValue& operator[](std::string_view key) const;

  // Iterate object members: fn(std::string_view key, const JsonValue& value).
  template <typename Fn>
  void ForEachMember(Fn fn) const {
    for (auto* m = first_member; m; m = m->next) fn(m->key, *m->value);
  }

  // Iterate array elements: fn(const JsonValue& value).
  template <typename Fn>
  void ForEachElement(Fn fn) const {
    for (auto* e = first_element; e; e = e->next) fn(*e->value);
  }
};

// Parse a JSON document. All string values point into the original input
// buffer (or into allocator memory when escape sequences are present).
// Tree nodes are allocated from the provided allocator.
ErrorOr<JsonValue*> ParseJson(std::string_view input, Allocator* allocator);

}  // namespace G

#endif  // _GAME_JSON_H
