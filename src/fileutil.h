#pragma once
#ifndef _GAME_FILEUTIL_H
#define _GAME_FILEUTIL_H

#include <cstdio>
#include <cstdlib>

#ifndef _WIN32
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#endif

namespace G {

inline bool FileExists(const char* path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

inline bool DirectoryExists(const char* path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

inline bool MakeDir(const char* path) {
#ifdef _WIN32
  return CreateDirectoryA(path, nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
#else
  return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

inline bool MakeDirs(const char* path) {
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

// Resolves a path to an absolute path. Caller must use the result before the
// next call (returns a static buffer).
inline const char* AbsolutePath(const char* path) {
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

inline bool WriteFile(const char* path, const char* contents) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return false;
  fputs(contents, f);
  fclose(f);
  return true;
}

template <typename... Args>
bool WriteFileF(const char* path, const char* fmt, Args... args) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return false;
  fprintf(f, fmt, args...);
  fclose(f);
  return true;
}

inline bool CopyFile(const char* src, const char* dst) {
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

}  // namespace G

#endif  // _GAME_FILEUTIL_H
