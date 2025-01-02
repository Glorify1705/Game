#pragma once
#ifndef _GAME_FILESYSTEM_H
#define _GAME_FILESYSTEM_H

#include <cstdint>
#include <string_view>

#include "allocators.h"
#include "array.h"
#include "config.h"
#include "constants.h"
#include "dictionary.h"
#include "physfs.h"

namespace G {

#define PHYSFS_CHECK(cond, ...)                               \
  CHECK(cond, "Failed Phys condition " #cond " with error: ", \
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()), " ", ##__VA_ARGS__)

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
                   StringBuffer* err);

  bool ReadFile(std::string_view filename, uint8_t* buffer, size_t size,
                StringBuffer* err);

  struct StatInfo {
    size_t size;
    enum Type { kFile, kDirectory };
    Type type;
    int64_t modtime_secs;
    int64_t created_time_secs;
    int64_t access_time_secs;
  };

  bool Size(std::string_view filename, size_t* result, StringBuffer* err);

  bool Stat(std::string_view filename, StatInfo* info, StringBuffer* err);

  bool Exists(std::string_view filename);

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
