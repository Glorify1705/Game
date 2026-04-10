#include "filesystem.h"

namespace G {

void Filesystem::Initialize(const GameConfig& config) {
  if (!PHYSFS_isInit()) {
    PHYSFS_CHECK(PHYSFS_init(config.app_name),
                 "Could not initialize PhysFS: ", config.app_name);
  }
  CHECK(config.app_name[0], "No App name was provided.");
  CHECK(config.org_name[0], "No Org name was provided.");
  LOG("Initializing filesystem with org ", config.org_name, " and app ",
      config.app_name);
  org_name_.Set(config.app_name);
  program_name_.Set(config.app_name);
  pref_dir_.Set(PHYSFS_getPrefDir(org_name_, program_name_));
  LOG("Output dir: ", pref_dir_);
  PHYSFS_setWriteDir(pref_dir_.str());
  PHYSFS_mount(pref_dir_.str(), "/app", /*appendToPath=*/true);
}

Filesystem::~Filesystem() {
  for (PHYSFS_File* ptr : for_read_) PHYSFS_close(ptr);
  for (PHYSFS_File* ptr : for_write_) PHYSFS_close(ptr);
}

ErrorOr<void> Filesystem::WriteToFile(std::string_view filename,
                                      std::string_view contents) {
  size_t handle;
  FixedStringBuffer<kMaxPathLength> path(filename);
  if (!filename_to_handle_.Lookup(filename, &handle)) {
    handle = for_write_.size();
    PHYSFS_File* f = PHYSFS_openWrite(path.str());
    if (f == nullptr) {
      LOG("Failed to open file ", path, ": ",
          PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
      return Error::Message("failed to open file for writing");
    }
    for_write_.Push(f);
    filename_to_handle_.Insert(filename, handle);
  }
  if (PHYSFS_writeBytes(for_write_[handle], contents.data(), contents.size()) !=
      static_cast<PHYSFS_sint64>(contents.size())) {
    LOG("Could not write to file ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    return Error::Message("failed to write to file");
  }
  return {};
}

ErrorOr<void> Filesystem::Spit(std::string_view filename,
                               std::string_view contents) {
  FixedStringBuffer<kMaxPathLength> path(filename);
  PHYSFS_File* f = PHYSFS_openWrite(path.str());
  if (f == nullptr) {
    LOG("Failed to open file ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    return Error::Message("failed to open file for writing");
  }
  if (PHYSFS_writeBytes(f, contents.data(), contents.size()) !=
      static_cast<PHYSFS_sint64>(contents.size())) {
    LOG("Could not write to file ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    PHYSFS_close(f);
    return Error::Message("failed to write to file");
  }
  PHYSFS_close(f);
  return {};
}

ErrorOr<void> Filesystem::ReadFile(std::string_view filename, uint8_t* buffer,
                                   size_t size) {
  size_t handle;
  FixedStringBuffer<kMaxPathLength> path(filename);
  if (!filename_to_handle_.Lookup(filename, &handle)) {
    handle = for_read_.size();
    PHYSFS_File* f = PHYSFS_openRead(path.str());
    if (f == nullptr) {
      LOG("Failed to open file ", path, ": ",
          PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
      return Error::Message("failed to open file for reading");
    }
    for_read_.Push(f);
    filename_to_handle_.Insert(filename, handle);
  }
  if (PHYSFS_readBytes(for_read_[handle], buffer, size) !=
      static_cast<PHYSFS_sint64>(size)) {
    LOG("Could not read file ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    return Error::Message("failed to read file");
  }
  return {};
}

ErrorOr<size_t> Filesystem::Slurp(std::string_view filename, uint8_t* buffer,
                                  size_t buffer_size) {
  FixedStringBuffer<kMaxPathLength> path(filename);
  PHYSFS_File* f = PHYSFS_openRead(path.str());
  if (f == nullptr) {
    LOG("Could not read file ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    return Error::Message("failed to open file for reading");
  }
  PHYSFS_sint64 length = PHYSFS_fileLength(f);
  if (length < 0 || static_cast<size_t>(length) > buffer_size) {
    PHYSFS_close(f);
    return Error::Message("file too large for buffer");
  }
  size_t sz = static_cast<size_t>(length);
  if (sz > 0 &&
      PHYSFS_readBytes(f, buffer, sz) != static_cast<PHYSFS_sint64>(sz)) {
    LOG("Could not read file ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    PHYSFS_close(f);
    return Error::Message("failed to read file");
  }
  PHYSFS_close(f);
  return sz;
}

ErrorOr<size_t> Filesystem::Size(std::string_view filename) {
  size_t handle;
  FixedStringBuffer<kMaxPathLength> path(filename);
  if (!filename_to_handle_.Lookup(filename, &handle)) {
    handle = for_read_.size();
    PHYSFS_File* f = PHYSFS_openRead(path.str());
    if (f == nullptr) {
      LOG("Failed to open file ", path, ": ",
          PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
      return Error::Message("failed to open file");
    }
    for_read_.Push(f);
    filename_to_handle_.Insert(filename, handle);
  }
  PHYSFS_sint64 length = PHYSFS_fileLength(for_read_[handle]);
  if (length == -1) {
    LOG("Could not get size of file ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    return Error::Message("failed to get file size");
  }
  return static_cast<size_t>(length);
}

ErrorOr<Filesystem::StatInfo> Filesystem::Stat(std::string_view filename) {
  FixedStringBuffer<kMaxPathLength> path(filename);
  PHYSFS_Stat stat;
  const int result = PHYSFS_stat(path.str(), &stat);
  if (result == 0) {
    LOG("Could not stat file ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    return Error::Message("failed to stat file");
  }
  StatInfo info;
  info.size = stat.filesize;
  info.access_time_secs = stat.accesstime;
  info.created_time_secs = stat.createtime;
  info.modtime_secs = stat.modtime;
  switch (stat.filetype) {
    case PHYSFS_FILETYPE_REGULAR:
      info.type = StatInfo::kFile;
      break;
    case PHYSFS_FILETYPE_DIRECTORY:
      info.type = StatInfo::kDirectory;
      break;
    default:
      LOG("Unknown file type for ", path);
      return Error::Message("unknown file type");
  }
  return info;
}

bool Filesystem::Exists(std::string_view filename) {
  FixedStringBuffer<kMaxPathLength> path(filename);
  return PHYSFS_exists(path);
}

ErrorOr<void> Filesystem::Delete(std::string_view filename) {
  FixedStringBuffer<kMaxPathLength> path(filename);
  // PHYSFS_delete operates on the write directory. It returns 0 on failure;
  // "not found" is not an error — treat it as a successful no-op.
  if (PHYSFS_delete(path) == 0) {
    PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
    if (err == PHYSFS_ERR_NOT_FOUND) return {};
    LOG("Could not delete file ", path, ": ", PHYSFS_getErrorByCode(err));
    return Error::Message("failed to delete file");
  }
  return {};
}

void Filesystem::EnumerateDirectory(std::string_view directory,
                                    DirCallback callback, void* userdata) {
  FixedStringBuffer<kMaxPathLength> d(directory);
  PHYSFS_enumerate(d.str(), callback, userdata);
}

}  // namespace G
