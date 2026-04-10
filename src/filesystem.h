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
#include "error.h"
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

  ErrorOr<void> WriteToFile(std::string_view filename,
                            std::string_view contents);

  // Opens, writes, and closes the file in one shot. Unlike WriteToFile, this
  // does not cache the handle, so the data is visible to reads immediately.
  ErrorOr<void> Spit(std::string_view filename, std::string_view contents);

  ErrorOr<void> ReadFile(std::string_view filename, uint8_t* buffer,
                         size_t size);

  // Opens, reads the entire file, and closes the handle. Unlike ReadFile/Size,
  // this always sees the latest data on disk.
  ErrorOr<size_t> Slurp(std::string_view filename, uint8_t* buffer,
                        size_t buffer_size);

  struct StatInfo {
    size_t size;
    enum Type { kFile, kDirectory };
    Type type;
    int64_t modtime_secs;
    int64_t created_time_secs;
    int64_t access_time_secs;
  };

  ErrorOr<size_t> Size(std::string_view filename);

  ErrorOr<StatInfo> Stat(std::string_view filename);

  bool Exists(std::string_view filename);

  // Delete a file from the write directory. No error if the file doesn't exist.
  ErrorOr<void> Delete(std::string_view filename);

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
