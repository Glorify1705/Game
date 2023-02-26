#include "strings.h"

bool HasSuffix(std::string_view str, std::string_view suffix) {
  size_t i = 0;
  for (; i < str.size() && i < suffix.size(); i++) {
    if (suffix[suffix.size() - i] != str[str.size() - i]) return false;
  }
  return i == suffix.size();
}

bool ConsumeSuffix(std::string_view* str, std::string_view suffix) {
  const bool has_suffix = HasSuffix(*str, suffix);
  if (has_suffix) str->remove_suffix(suffix.size());
  return has_suffix;
}