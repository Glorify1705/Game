#pragma once
#ifndef _GAME_PLATFORM_H
#define _GAME_PLATFORM_H

#include <cerrno>
#include <cstddef>
#include <cstdio>

#include "error.h"

namespace G {

// Filesystem queries.
bool FileExists(const char* path);
bool DirectoryExists(const char* path);

// Directory creation (single level and recursive).
ErrorOr<void> MakeDir(const char* path);
ErrorOr<void> MakeDirs(const char* path);

// Resolves a path to an absolute path.  Caller must use the result before the
// next call (returns a static buffer).
const char* AbsolutePath(const char* path);

// File I/O.
ErrorOr<void> WriteFile(const char* path, const char* contents);
ErrorOr<void> CopyFile(const char* src, const char* dst);
ErrorOr<void> MakeExecutable(const char* path);

template <typename... Args>
ErrorOr<void> WriteFileF(const char* path, const char* fmt, Args... args) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return Error::Errno(errno);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
  fprintf(f, fmt, args...);
#pragma clang diagnostic pop
  fclose(f);
  return {};
}

// Platform queries.
ErrorOr<void> GetExePath(char* out, size_t out_size);
ErrorOr<void> GetExeDir(char* out, size_t out_size);
ErrorOr<void> GetCwd(char* out, size_t out_size);
void GetUserCacheDir(const char* app_name, char* out, size_t out_size);

// Platform constants.
extern const char* const kExeExtension;

}  // namespace G

#endif  // _GAME_PLATFORM_H
