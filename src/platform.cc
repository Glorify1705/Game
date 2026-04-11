#include "platform.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "allocators.h"
#include "defer.h"
#include "stringlib.h"

#ifdef _WIN32
#include <windows.h>
// Windows CopyFile macro conflicts with our function name.
#undef CopyFile
#else
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace G {

bool FileExists(const char* path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

bool DirectoryExists(const char* path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

ErrorOr<void> MakeDir(const char* path) {
#ifdef _WIN32
  if (!CreateDirectoryA(path, nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    return Error::Message("CreateDirectoryA failed");
  }
  return {};
#else
  if (mkdir(path, 0755) != 0 && errno != EEXIST) {
    return Error::Errno(errno);
  }
  return {};
#endif
}

ErrorOr<void> MakeDirs(const char* path) {
#ifdef _WIN32
  char tmp[MAX_PATH];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char* p = tmp + 1; *p; ++p) {
    if (*p == '/' || *p == '\\') {
      *p = '\0';
      CreateDirectoryA(tmp, nullptr);
      *p = '/';
    }
  }
  if (!CreateDirectoryA(tmp, nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    return Error::Message("CreateDirectoryA failed");
  }
  return {};
#else
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char* p = tmp + 1; *p; ++p) {
    if (*p == '/') {
      *p = '\0';
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
    return Error::Errno(errno);
  }
  return {};
#endif
}

ErrorOr<void> IterateDirectory(const char* dir, DirIterCallback callback,
                               void* userdata) {
#ifdef _WIN32
  char pattern[MAX_PATH];
  snprintf(pattern, sizeof(pattern), "%s\\*", dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) return Error::Message("FindFirstFileA failed");
  DEFER([h] { FindClose(h); });
  do {
    if (fd.cFileName[0] == '.') continue;
    DirEntry entry;
    entry.name = fd.cFileName;
    entry.type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                     ? DirEntryType::kDirectory
                     : DirEntryType::kFile;
    callback(entry, userdata);
  } while (FindNextFileA(h, &fd));
#else
  DIR* d = opendir(dir);
  if (d == nullptr) return Error::Errno(errno);
  DEFER([d] { closedir(d); });
  struct dirent* ent;
  while ((ent = readdir(d)) != nullptr) {
    if (ent->d_name[0] == '.') continue;
    struct stat st;
    FixedStringBuffer<1024> full_path(dir, "/", ent->d_name);
    if (stat(full_path.str(), &st) != 0) continue;
    DirEntry entry;
    entry.name = ent->d_name;
    entry.type =
        S_ISDIR(st.st_mode) ? DirEntryType::kDirectory : DirEntryType::kFile;
    callback(entry, userdata);
  }
#endif
  return {};
}

const char* AbsolutePath(const char* path) {
#ifdef _WIN32
  static char resolved[MAX_PATH];
  if (_fullpath(resolved, path, MAX_PATH) != nullptr) return resolved;
  return path;
#else
  static char resolved[PATH_MAX];
  if (realpath(path, resolved) != nullptr) return resolved;
  return path;
#endif
}

ErrorOr<size_t> ReadEntireFile(const char* path, uint8_t** out,
                               Allocator* allocator) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return Error::Errno(errno);
  DEFER([f] { fclose(f); });
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  if (len < 0) return Error::Errno(errno);
  fseek(f, 0, SEEK_SET);
  auto* buf = static_cast<uint8_t*>(allocator->Alloc(len, 1));
  if (fread(buf, 1, len, f) != static_cast<size_t>(len)) {
    return Error::Errno(errno);
  }
  *out = buf;
  return static_cast<size_t>(len);
}

ErrorOr<void> WriteEntireFile(const char* path, const void* data, size_t size) {
  FILE* f = fopen(path, "wb");
  if (f == nullptr) return Error::Errno(errno);
  DEFER([f] { fclose(f); });
  if (fwrite(data, 1, size, f) != size) return Error::Errno(errno);
  return {};
}

ErrorOr<void> WriteFile(const char* path, const char* contents) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return Error::Errno(errno);
  fputs(contents, f);
  fclose(f);
  return {};
}

ErrorOr<void> CopyFile(const char* src, const char* dst) {
  FILE* in = fopen(src, "rb");
  if (in == nullptr) return Error::Errno(errno);
  FILE* out = fopen(dst, "wb");
  if (out == nullptr) {
    int saved_errno = errno;
    fclose(in);
    return Error::Errno(saved_errno);
  }
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    fwrite(buf, 1, n, out);
  }
  fclose(in);
  fclose(out);
  return {};
}

ErrorOr<void> MakeExecutable([[maybe_unused]] const char* path) {
#ifdef _WIN32
  return {};
#else
  if (chmod(path, 0755) != 0) return Error::Errno(errno);
  return {};
#endif
}

ErrorOr<void> GetExePath(char* out, size_t out_size) {
#ifdef _WIN32
  DWORD len = GetModuleFileNameA(nullptr, out, (DWORD)out_size);
  if (len == 0 || len >= (DWORD)out_size) {
    return Error::Message("GetModuleFileNameA failed");
  }
  return {};
#elif defined(__APPLE__)
  uint32_t size = (uint32_t)out_size;
  if (_NSGetExecutablePath(out, &size) != 0) {
    return Error::Message("_NSGetExecutablePath failed");
  }
  return {};
#else
  ssize_t len = readlink("/proc/self/exe", out, out_size - 1);
  if (len <= 0) return Error::Errno(errno);
  out[len] = '\0';
  return {};
#endif
}

ErrorOr<void> GetExeDir(char* out, size_t out_size) {
  TRY(GetExePath(out, out_size));
  char* last_slash = strrchr(out, '/');
#ifdef _WIN32
  char* last_bslash = strrchr(out, '\\');
  if (last_bslash > last_slash) last_slash = last_bslash;
#endif
  if (last_slash == nullptr)
    return Error::Message("no directory separator in exe path");
  last_slash[1] = '\0';
  return {};
}

ErrorOr<void> GetCwd(char* out, size_t out_size) {
#ifdef _WIN32
  if (GetCurrentDirectoryA((DWORD)out_size, out) == 0) {
    return Error::Message("GetCurrentDirectoryA failed");
  }
  return {};
#else
  if (getcwd(out, out_size) == nullptr) return Error::Errno(errno);
  return {};
#endif
}

void GetUserCacheDir(const char* app_name, char* out, size_t out_size) {
#ifdef _WIN32
  const char* local_app_data = getenv("LOCALAPPDATA");
  if (local_app_data != nullptr) {
    snprintf(out, out_size, "%s\\%s\\cache", local_app_data, app_name);
  } else {
    snprintf(out, out_size, ".\\%s-cache", app_name);
  }
#elif defined(__APPLE__)
  const char* home = getenv("HOME");
  if (home == nullptr) home = "/tmp";
  snprintf(out, out_size, "%s/Library/Caches/%s", home, app_name);
#else
  const char* xdg_cache = getenv("XDG_CACHE_HOME");
  if (xdg_cache != nullptr && xdg_cache[0] != '\0') {
    snprintf(out, out_size, "%s/%s", xdg_cache, app_name);
  } else {
    const char* home = getenv("HOME");
    if (home == nullptr) home = "/tmp";
    snprintf(out, out_size, "%s/.cache/%s", home, app_name);
  }
#endif
}

#ifdef _WIN32
const char* const kExeExtension = ".exe";
#else
const char* const kExeExtension = "";
#endif

}  // namespace G
