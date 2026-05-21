#include "stringlib.h"

#include "allocators.h"
#include "libraries/double-conversion/double-to-string.h"
#include "libraries/double-conversion/string-to-double.h"

namespace G {

void* StringBufferAlloc(void* allocator, size_t size) {
  return static_cast<Allocator*>(allocator)->Alloc(size, /*align=*/1);
}

void* StringBufferRealloc(void* allocator, void* ptr, size_t old_size,
                          size_t new_size) {
  return static_cast<Allocator*>(allocator)->Realloc(ptr, old_size, new_size,
                                                     /*align=*/1);
}

void StringBufferDealloc(void* allocator, void* ptr, size_t size) {
  static_cast<Allocator*>(allocator)->Dealloc(ptr, size);
}
namespace {

static const double_conversion::DoubleToStringConverter kDoubleToJson(
    double_conversion::DoubleToStringConverter::UNIQUE_ZERO |
        double_conversion::DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN,
    "1e5000", "null", 'e', -6, 21, 6, 0);

}  // namespace

bool HasSuffix(std::string_view str, std::string_view suffix) {
  if (suffix.size() > str.size()) return false;
  size_t i = 0;
  for (; i < suffix.size(); i++) {
    if (suffix[suffix.size() - 1 - i] != str[str.size() - 1 - i]) return false;
  }
  return true;
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

namespace {

const double_conversion::StringToDoubleConverter kStringToDouble(
    double_conversion::StringToDoubleConverter::ALLOW_TRAILING_JUNK |
        double_conversion::StringToDoubleConverter::ALLOW_LEADING_SPACES |
        double_conversion::StringToDoubleConverter::ALLOW_TRAILING_SPACES,
    /*empty_string_value=*/0.0, /*junk_string_value=*/0.0, "inf", "nan");

}  // namespace

double ParseDouble(std::string_view s) {
  if (s.empty()) return 0.0;
  int processed = 0;
  return kStringToDouble.StringToDouble(s.data(), static_cast<int>(s.size()),
                                        &processed);
}

float ParseFloat(std::string_view s) {
  if (s.empty()) return 0.0f;
  int processed = 0;
  return kStringToDouble.StringToFloat(s.data(), static_cast<int>(s.size()),
                                       &processed);
}

const char* StrDupZ(Allocator* allocator, std::string_view s) {
  char* buf = static_cast<char*>(allocator->Alloc(s.size() + 1, 1));
  memcpy(buf, s.data(), s.size());
  buf[s.size()] = '\0';
  return buf;
}

}  // namespace G
