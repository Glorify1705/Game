#pragma once
#ifndef _GAME_XML_H
#define _GAME_XML_H

#include <string_view>

#include "allocators.h"
#include "error.h"

namespace G {

struct XmlAttribute {
  std::string_view name;
  std::string_view value;
  XmlAttribute* next = nullptr;
};

struct XmlElement {
  std::string_view tag;
  std::string_view text;  // Text content between open and close tags.
  XmlAttribute* first_attribute = nullptr;
  XmlElement* first_child = nullptr;
  XmlElement* next_sibling = nullptr;

  // Look up an attribute value by name. Returns empty string_view if missing.
  std::string_view Attr(std::string_view name) const;

  // Look up an integer attribute. Returns 0 if missing or non-numeric.
  int AttrInt(std::string_view name) const;

  // Look up a float attribute. Returns 0 if missing or non-numeric.
  float AttrFloat(std::string_view name) const;

  // Iterate children with a given tag name, calling fn(const XmlElement&) for
  // each match.
  template <typename Fn>
  void ForEachChild(std::string_view tag_name, Fn fn) const {
    for (auto* child = first_child; child; child = child->next_sibling) {
      if (child->tag == tag_name) fn(*child);
    }
  }
};

// Parse an XML document. All returned string_views point into the original
// input buffer. Tree nodes are allocated from the provided allocator (an arena
// allocator is ideal since there is no individual deallocation).
ErrorOr<XmlElement*> ParseXml(std::string_view input, Allocator* allocator);

}  // namespace G

#endif  // _GAME_XML_H
