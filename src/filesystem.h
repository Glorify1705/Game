#pragma once
#ifndef _GAME_FILESYSTEM_H
#define _GAME_FILESYSTEM_H

#include <cstdint>
#include <string_view>

#include "allocators.h"
#include "array.h"
#include "config.h"
#include "dictionary.h"
#include "physfs.h"

namespace G {

inline static constexpr size_t kMaxPathLength = 256;

#define PHYSFS_CHECK(cond, ...)                               \
  CHECK(cond, "Failed Phys condition " #cond " with error: ", \
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()), " ", ##__VA_ARGS__)

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
  Filesystem(Allocator* allocator)
      : allocator_(allocator),
        for_read_(1024, allocator),
        for_write_(1024, allocator),
        filename_to_handle_(allocator_) {}
  ~Filesystem();

  void Initialize(const GameConfig& config);

  bool WriteToFile(std::string_view filename, std::string_view contents,
                   char* errbuf, size_t* errlen);

  bool ReadFile(std::string_view filename, uint8_t* buffer, size_t size,
                char* errbuf, size_t* errlen);

  bool Size(std::string_view filename, size_t* result, char* errbuf,
            size_t* errlen);

  using DirCallback = PHYSFS_EnumerateCallbackResult (*)(void* userdata,
                                                         const char* file,
                                                         const char* dir);

  void EnumerateDirectory(std::string_view directory, DirCallback callback,
                          void* userdata);

 private:
  Allocator* allocator_;
  FixedStringBuffer<kMaxPathLength> org_name_;
  FixedStringBuffer<kMaxPathLength> program_name_;
  FixedStringBuffer<kMaxPathLength + 1> pref_dir_;
  FixedArray<PHYSFS_File*> for_read_;
  FixedArray<PHYSFS_File*> for_write_;
  Dictionary<size_t> filename_to_handle_;
};

}  // namespace G

#endif  // _GAME_FILESYSTEM_H