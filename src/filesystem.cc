#include "filesystem.h"

namespace G {

void Filesystem::Initialize(std::string_view program_name) {
  if (PHYSFS_isInit()) return;
  PHYSFS_CHECK(PHYSFS_init(program_name.data()),
               "Could not initialize PhysFS: ", program_name);
}

Filesystem::Handle Filesystem::Open(std::string_view output,
                                    Filesystem::Mode mode) {
  (void)output;
  (void)mode;
  return 0;
}

size_t Filesystem::WriteToFile(Handle handle, std::string_view contents) {
  (void)handle;
  (void)contents;
  return 0;
}

size_t Filesystem::ReadFile(Handle handle, uint8_t* buffer, size_t size) {
  (void)handle;
  (void)buffer;
  (void)size;
  return 0;
}

Filesystem::StatResult Filesystem::Stat(Handle handle) {
  (void)handle;
  return {0};
}

void Filesystem::EnumerateDirectory(std::string_view directory,
                                    DirCallback callback, void* userdata) {
  (void)directory;
  (void)callback;
  (void)userdata;
  PHYSFS_enumerate(directory.data(), callback, userdata);
}

}  // namespace G
