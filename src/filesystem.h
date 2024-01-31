#pragma once
#ifndef _GAME_FILESYSTEM_H
#define _GAME_FILESYSTEM_H

#include <cstdint>
#include <string_view>

#include "allocators.h"
#include "physfs.h"

namespace G {

#define PHYSFS_CHECK(cond, ...)                               \
  CHECK(cond, "Failed Phys condition " #cond " with error: ", \
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()), ##__VA_ARGS__)

inline std::pair<uint8_t*, size_t> ReadWholeFile(const char* path,
                                                 Allocator* allocator) {
  auto* handle = PHYSFS_openRead(path);
  CHECK(handle != nullptr, "Could not read ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
  const int64_t bytes = PHYSFS_fileLength(handle);
  auto* buffer = static_cast<uint8_t*>(allocator->Alloc(bytes, /*align=*/1));
  CHECK(PHYSFS_readBytes(handle, buffer, bytes) == bytes, " failed to read ",
        path, " error = ", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
  CHECK(PHYSFS_close(handle), "failed to finish reading ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
  return {buffer, bytes};
}

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