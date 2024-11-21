#pragma once
#ifndef _GAME_STRING_TABLE_H
#define _GAME_STRING_TABLE_H

#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace G {

class StringTable {
 public:
  StringTable();
  static StringTable& Instance() {
    static StringTable s;
    return s;
  }

  uint32_t Intern(std::string_view input);

  uint32_t Handle(std::string_view input);

  std::string_view Lookup(uint32_t handle) {
    return std::string_view(&buffer_[offsets_[handle]], sizes_[handle]);
  }

  struct Stats {
    int32_t strings_used = 0;
    int32_t space_used = 0;
    int32_t total_space = kTotalSize;
    int32_t total_strings = 1 << kTotalStringsLog;
  };

  Stats stats() { return stats_; }

 private:
  static uint64_t Hash(std::string_view s);

  bool IsThere(uint32_t pos, std::string_view s);

  inline static constexpr size_t kTotalSize = 1 << 24;
  inline static constexpr size_t kTotalStringsLog = 16;

  char buffer_[kTotalSize + 1];
  uint32_t offsets_[1 << kTotalStringsLog];
  uint32_t sizes_[1 << kTotalStringsLog];
  uint32_t pos_ = 0;

  Stats stats_;
};

inline uint32_t StringIntern(std::string_view input) {
  return StringTable::Instance().Intern(input);
}

inline uint32_t StringHandle(std::string_view input) {
  return StringTable::Instance().Handle(input);
}

inline std::string_view StringByHandle(uint32_t handle) {
  return StringTable::Instance().Lookup(handle);
}

}  // namespace G

#endif  // _GAME_STRING_TABLE_H
