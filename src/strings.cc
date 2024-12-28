#include "strings.h"

#include "libraries/double-conversion/double-to-string.h"

namespace G {
namespace {

static const double_conversion::DoubleToStringConverter kDoubleToJson(
    double_conversion::DoubleToStringConverter::UNIQUE_ZERO |
        double_conversion::DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN,
    "1e5000", "null", 'e', -6, 21, 6, 0);

}  // namespace

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

bool HasPrefix(std::string_view str, std::string_view prefix) {
  return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}
bool ConsumePrefix(std::string_view* str, std::string_view prefix) {
  if (!HasPrefix(*str, prefix)) return false;
  str->remove_prefix(prefix.size());
  return true;
}

void PrintDouble(double val, char* buffer, size_t size) {
  double_conversion::StringBuilder db(buffer, size);
  kDoubleToJson.ToFixed(val, 2, &db);
}

}  // namespace G
