#pragma once
#ifndef _GAME_FILESYSTEM_H
#define _GAME_FILESYSTEM_H

#include <cstdint>
#include <string_view>

#include "allocators.h"
#include "array.h"
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

class Filesystem {
 public:
  using Handle = int;

  class Result {
    char error[128];
  };

  enum class Mode { kReading, kWriting };

  static void Initialize(std::string_view program_name);

  Filesystem(Allocator* allocator)
      : allocator_(allocator), buffer_(64, allocator) {}

  Handle Open(std::string_view file, Mode mode);

  size_t WriteToFile(Handle handle, std::string_view contents);

  size_t ReadFile(Handle handle, uint8_t* buffer, size_t size);

  struct StatResult {
    size_t total_size;
  };

  StatResult Stat(Handle handle);

  using DirCallback = PHYSFS_EnumerateCallbackResult (*)(void* userdata,
                                                         const char* file,
                                                         const char* dir);

  void EnumerateDirectory(std::string_view directory, DirCallback callback,
                          void* userdata);

 private:
  struct FreeListNode {
    FreeListNode* next;
  };
  Allocator* allocator_;
  FixedArray<Result> buffer_;
  FreeListNode* head_ = nullptr;
};

}  // namespace G

#endif  // _GAME_FILESYSTEM_H