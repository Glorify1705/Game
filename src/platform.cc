#include "platform.h"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
// Windows CopyFile macro conflicts with our function name.
#undef CopyFile
#else
#include <errno.h>
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

bool MakeDir(const char* path) {
#ifdef _WIN32
  return CreateDirectoryA(path, nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
#else
  return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

bool MakeDirs(const char* path) {
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
  return CreateDirectoryA(tmp, nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
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
  return mkdir(tmp, 0755) == 0 || errno == EEXIST;
#endif
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

bool WriteFile(const char* path, const char* contents) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return false;
  fputs(contents, f);
  fclose(f);
  return true;
}

bool CopyFile(const char* src, const char* dst) {
  FILE* in = fopen(src, "rb");
  if (in == nullptr) return false;
  FILE* out = fopen(dst, "wb");
  if (out == nullptr) {
    fclose(in);
    return false;
  }
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    fwrite(buf, 1, n, out);
  }
  fclose(in);
  fclose(out);
  return true;
}

bool MakeExecutable(const char* path) {
#ifdef _WIN32
  (void)path;
  return true;
#else
  return chmod(path, 0755) == 0;
#endif
}

bool GetExePath(char* out, size_t out_size) {
#ifdef _WIN32
  DWORD len = GetModuleFileNameA(nullptr, out, (DWORD)out_size);
  return len > 0 && len < (DWORD)out_size;
#elif defined(__APPLE__)
  uint32_t size = (uint32_t)out_size;
  return _NSGetExecutablePath(out, &size) == 0;
#else
  ssize_t len = readlink("/proc/self/exe", out, out_size - 1);
  if (len <= 0) return false;
  out[len] = '\0';
  return true;
#endif
}

bool GetExeDir(char* out, size_t out_size) {
  if (!GetExePath(out, out_size)) return false;
  char* last_slash = strrchr(out, '/');
#ifdef _WIN32
  char* last_bslash = strrchr(out, '\\');
  if (last_bslash > last_slash) last_slash = last_bslash;
#endif
  if (last_slash == nullptr) return false;
  last_slash[1] = '\0';
  return true;
}

bool GetCwd(char* out, size_t out_size) {
#ifdef _WIN32
  return GetCurrentDirectoryA((DWORD)out_size, out) > 0;
#else
  return getcwd(out, out_size) != nullptr;
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
