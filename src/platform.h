#pragma once
#ifndef _GAME_PLATFORM_H
#define _GAME_PLATFORM_H

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "error.h"

namespace G {

class Allocator;

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
// Read an entire file into an allocator-owned buffer.
ErrorOr<size_t> ReadEntireFile(const char* path, uint8_t** out,
                               Allocator* allocator);
// Write a buffer to a file.
ErrorOr<void> WriteEntireFile(const char* path, const void* data, size_t size);
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

// Entry type reported by IterateDirectory.
enum class DirEntryType { kFile, kDirectory };

// Information about a single directory entry.
struct DirEntry {
  // Filename (not the full path).
  const char* name;
  DirEntryType type;
};

// Type-erased callback for directory iteration.
using DirIterCallback = void (*)(const DirEntry& entry, void* userdata);

// Calls `callback` for each entry in `dir`, skipping "." and "..".
ErrorOr<void> IterateDirectory(const char* dir, DirIterCallback callback,
                               void* userdata);

// Platform queries.
ErrorOr<void> GetExePath(char* out, size_t out_size);
ErrorOr<void> GetExeDir(char* out, size_t out_size);
ErrorOr<void> GetCwd(char* out, size_t out_size);
void GetUserCacheDir(const char* app_name, char* out, size_t out_size);

// Platform constants.
extern const char* const kExeExtension;

}  // namespace G

#endif  // _GAME_PLATFORM_H
