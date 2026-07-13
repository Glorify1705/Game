#pragma once
#ifndef _GAME_PLATFORM_H
#define _GAME_PLATFORM_H

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "array.h"
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
ErrorOr<void> WriteEntireFile(const char* path, ByteSlice data);
ErrorOr<void> WriteFile(const char* path, const char* contents);
ErrorOr<void> CopyFile(const char* src, const char* dst);
ErrorOr<void> MakeExecutable(const char* path);

template <typename... Args>
ErrorOr<void> WriteFileF(const char* path, const char* fmt, Args... args) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return Error::Errno(errno);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
  fprintf(f, fmt, args...);
#pragma GCC diagnostic pop
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

// Writes the per-project cache directory for a game source directory:
// <user cache dir>/game/<hex hash of the absolute source path>. Shared by
// `game run` and `game package` so packing reuses the dev cache.
void ComputeCacheDir(const char* source_directory, char* out, size_t out_size);

// Writes the platform-specific persistent save directory for the given app.
// Linux: $XDG_DATA_HOME/<app>/ or ~/.local/share/<app>/
// Windows: %APPDATA%/<app>
// macOS: ~/Library/Application Support/<app>/
void GetUserSaveDir(const char* app_name, char* out, size_t out_size);

#ifdef GAME_WEB
// Marks browser-persistent data (IDBFS) dirty; MaybeSyncIdb flushes it to
// IndexedDB with a debounce, SyncIdbNow immediately.
void RequestIdbSync();
void MaybeSyncIdb();
void SyncIdbNow();
#else
inline void RequestIdbSync() {}
inline void MaybeSyncIdb() {}
inline void SyncIdbNow() {}
#endif

// Platform constants.
extern const char* const kExeExtension;

}  // namespace G

#endif  // _GAME_PLATFORM_H
