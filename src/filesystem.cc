#include "filesystem.h"

namespace G {
namespace {

template <typename... Ts>
void SetError(StringBuffer* buf, Ts... ts) {
  buf->Append(std::forward<Ts>(ts)...);
  buf->Append(": ", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
}

}  // namespace

void Filesystem::Initialize(const GameConfig& config) {
  if (!PHYSFS_isInit()) {
    PHYSFS_CHECK(PHYSFS_init(config.app_name),
                 "Could not initialize PhysFS: ", config.app_name);
  }
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

bool Filesystem::WriteToFile(std::string_view filename,
                             std::string_view contents, StringBuffer* buf) {
  size_t handle;
  FixedStringBuffer<kMaxPathLength> path(filename);
  if (!filename_to_handle_.Lookup(filename, &handle)) {
    handle = for_write_.size();
    PHYSFS_File* f = PHYSFS_openWrite(path.str());
    if (f == nullptr) {
      SetError(buf, "Failed to open file ", path);
      return false;
    }
    for_write_.Push(f);
    filename_to_handle_.Insert(filename, handle);
  }
  if (PHYSFS_writeBytes(for_write_[handle], contents.data(), contents.size()) !=
      static_cast<PHYSFS_sint64>(contents.size())) {
    SetError(buf, "Could not write to file ", path);
    return false;
  }
  return true;
}

bool Filesystem::ReadFile(std::string_view filename, uint8_t* buffer,
                          size_t size, StringBuffer* buf) {
  size_t handle;
  FixedStringBuffer<kMaxPathLength> path(filename);
  if (!filename_to_handle_.Lookup(filename, &handle)) {
    handle = for_read_.size();
    PHYSFS_File* f = PHYSFS_openRead(path.str());
    if (f == nullptr) {
      SetError(buf, "Failed to open file ", path);
      return false;
    }
    for_read_.Push(f);
    filename_to_handle_.Insert(filename, handle);
  }
  if (PHYSFS_readBytes(for_read_[handle], buffer, size) !=
      static_cast<PHYSFS_sint64>(size)) {
    SetError(buf, "Could not read file ", path);
    return false;
  }
  return true;
}

bool Filesystem::Size(std::string_view filename, size_t* result,
                      StringBuffer* buf) {
  size_t handle;
  FixedStringBuffer<kMaxPathLength> path(filename);
  if (!filename_to_handle_.Lookup(filename, &handle)) {
    handle = for_read_.size();
    PHYSFS_File* f = PHYSFS_openRead(path.str());
    if (f == nullptr) {
      SetError(buf, "Failed to open file ", path);
      return false;
    }
    for_read_.Push(f);
    filename_to_handle_.Insert(filename, handle);
  }
  PHYSFS_sint64 length = PHYSFS_fileLength(for_read_[handle]);
  if (length == -1) {
    SetError(buf, "Could not read file ", path);
    return false;
  }
  *result = length;
  return true;
}

bool Filesystem::Stat(std::string_view filename, StatInfo* info,
                      StringBuffer* buf) {
  FixedStringBuffer<kMaxPathLength> path(filename);
  PHYSFS_Stat stat;
  const int result = PHYSFS_stat(path.str(), &stat);
  if (result == 0) {
    SetError(buf, "Could not read file ", path);
    return false;
  }
  info->size = stat.filesize;
  info->access_time_secs = stat.accesstime;
  info->created_time_secs = stat.createtime;
  info->modtime_secs = stat.modtime;
  switch (stat.filetype) {
    case PHYSFS_FILETYPE_REGULAR:
      info->type = StatInfo::kFile;
      break;
    case PHYSFS_FILETYPE_DIRECTORY:
      info->type = StatInfo::kDirectory;
      break;
    default:
      SetError(buf, "Tried to stat unknown file ", path);
      return false;
  }
  return true;
}

void Filesystem::EnumerateDirectory(std::string_view directory,
                                    DirCallback callback, void* userdata) {
  FixedStringBuffer<kMaxPathLength> d(directory);
  PHYSFS_enumerate(d.str(), callback, userdata);
}

}  // namespace G
