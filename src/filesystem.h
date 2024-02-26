#pragma once
#ifndef _GAME_FILESYSTEM_H
#define _GAME_FILESYSTEM_H

#include <cstdint>
#include <string_view>

#include "allocators.h"
#include "physfs.h"

namespace G {

inline static constexpr size_t kMaxPathLength = 256;

#define PHYSFS_CHECK(cond, ...)                               \
  CHECK(cond, "Failed Phys condition " #cond " with error: ", \
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()), ##__VA_ARGS__)

inline std::string_view Basename(std::string_view p) {
  size_t pos = p.size() - 1;
  for (; pos != 0 && p[pos] != '/';) {
    pos--;
  }
  return p[pos] == '/' ? p.substr(pos + 1) : p;
}

inline std::string_view WithoutExt(std::string_view p) {
  size_t pos = p.size() - 1;
  for (; pos != 0 && p[pos] != '.';) {
    pos--;
  }
  return p[pos] == '.' ? p.substr(0, pos) : p;
}

inline std::string_view Extension(std::string_view p) {
  size_t pos = p.size() - 1;
  for (; pos != 0 && p[pos] != '.';) {
    pos--;
  }
  return (pos == 0 && p[pos] != '.') ? p : p.substr(pos + 1);
}

}  // namespace G

#endif  // _GAME_FILESYSTEM_H