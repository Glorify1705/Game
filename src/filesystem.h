#pragma once
#ifndef _GAME_FILESYSTEM_H
#define _GAME_FILESYSTEM_H

#include "physfs.h"

namespace G {

class Filesystem {
 public:
#if 0
  bool Init(Allocator* allocator, const char* program_name,
            const char* error_buffer, size_t buffer_size) {
    PHYSFS_init(program_name);
    return true;
  }

  void Deinit() { PHYSFS_deinit(); }

  enum class AddToPath { kPrepend, kAppend };

  void AddDir(const char* directory, const char* mount, AddToPath add_to_path) {
  }

  int Write(const char* filename, const char* data, size_t size) { return 0; }
#endif
};

}  // namespace G

#endif  // _GAME_FILESYSTEM_H