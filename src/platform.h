#pragma once
#ifndef _GAME_PLATFORM_H
#define _GAME_PLATFORM_H

#include <cstddef>
#include <cstdio>

namespace G {

// Filesystem queries.
bool FileExists(const char* path);
bool DirectoryExists(const char* path);

// Directory creation (single level and recursive).
bool MakeDir(const char* path);
bool MakeDirs(const char* path);

// Resolves a path to an absolute path.  Caller must use the result before the
// next call (returns a static buffer).
const char* AbsolutePath(const char* path);

// File I/O.
bool WriteFile(const char* path, const char* contents);
bool CopyFile(const char* src, const char* dst);
bool MakeExecutable(const char* path);

template <typename... Args>
bool WriteFileF(const char* path, const char* fmt, Args... args) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return false;
  fprintf(f, fmt, args...);
  fclose(f);
  return true;
}

// Platform queries.
bool GetExePath(char* out, size_t out_size);
bool GetExeDir(char* out, size_t out_size);
bool GetCwd(char* out, size_t out_size);
void GetUserCacheDir(const char* app_name, char* out, size_t out_size);

// Platform constants.
extern const char* const kExeExtension;

}  // namespace G

#endif  // _GAME_PLATFORM_H
