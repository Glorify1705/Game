#include "string_table.h"

#include "logging.h"
#include "xxhash.h"

namespace G {
namespace {

uint32_t MSILookup(uint64_t hash, int exp, uint32_t idx) {
  uint32_t mask = ((uint32_t)1 << exp) - 1;
  uint32_t step = (hash >> (64 - exp)) | 1;
  return (idx + step) & mask;
}

}  // namespace

uint64_t StringTable::Hash(std::string_view s) {
  return XXH64(s.data(), s.size(), 0xC0D315D474);
}

bool StringTable::IsThere(uint32_t pos, std::string_view s) {
  return (pos + s.size()) <= pos_ &&
         !std::memcmp(&buffer_[pos], s.data(), s.size());
}

StringTable::StringTable() {
  for (size_t i = 0; i < (1 << kTotalStringsLog); ++i) {
    sizes_[i] = 0;
  }
}

uint32_t StringTable::Intern(std::string_view input) {
  const uint64_t hash = Hash(input);
  for (uint32_t i = hash;;) {
    i = MSILookup(hash, kTotalStringsLog, i);
    if (sizes_[i] != 0) {
      if (IsThere(offsets_[i], input)) return i;
    } else {
      const size_t size = input.size();
      CHECK((pos_ + size + 1) < kTotalSize, "Failed to insert string ", input);
      std::memcpy(&buffer_[pos_], input.data(), size);
      offsets_[i] = pos_;
      sizes_[i] = size;
      pos_ += size;
      buffer_[pos_++] = '\0';
      stats_.space_used += size;
      stats_.strings_used++;
      return i;
    }
  }
}

uint32_t StringTable::Handle(std::string_view input) {
  const uint64_t hash = Hash(input);
  for (uint32_t i = hash;;) {
    i = MSILookup(hash, kTotalStringsLog, i);
    if (sizes_[i] == 0) return kTotalSize;
    if (IsThere(offsets_[i], input)) return i;
  }
}

}  // namespace G
