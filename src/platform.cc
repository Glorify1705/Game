#include "platform.h"

#ifdef GAME_WEB
#include <emscripten.h>
#endif

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "allocators.h"
#include "defer.h"
#include "libraries/rapidhash.h"
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
    CmdBuffer full_path(dir, "/", ent->d_name);
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
  CHECK(buf != nullptr, "Failed to allocate bytes for file read");
  if (fread(buf, 1, len, f) != static_cast<size_t>(len)) {
    return Error::Errno(errno);
  }
  *out = buf;
  return static_cast<size_t>(len);
}

ErrorOr<void> WriteEntireFile(const char* path, ByteSlice data) {
  FILE* f = fopen(path, "wb");
  if (f == nullptr) return Error::Errno(errno);
  DEFER([f] { fclose(f); });
  if (fwrite(data.data(), 1, data.size(), f) != data.size())
    return Error::Errno(errno);
  return {};
}

ErrorOr<void> WriteFile(const char* path, const char* contents) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return Error::Errno(errno);
  DEFER([f] { fclose(f); });
  fputs(contents, f);
  return {};
}

ErrorOr<void> CopyFile(const char* src, const char* dst) {
  FILE* in = fopen(src, "rb");
  if (in == nullptr) return Error::Errno(errno);
  DEFER([in] { fclose(in); });
  FILE* out = fopen(dst, "wb");
  if (out == nullptr) return Error::Errno(errno);
  DEFER([out] { fclose(out); });
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) return Error::Errno(errno);
  }
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
#ifdef GAME_WEB
  // The web build has no real executable path. The HTML shell preloads the
  // packaged asset files into /game/, so pretending the binary lives there
  // makes the packaged-mode lookup (exe_dir + "assets.sqlite3") find them.
  snprintf(out, out_size, "/game/game");
  return {};
#elif defined(_WIN32)
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

void ComputeCacheDir(const char* source_directory, char* out, size_t out_size) {
  const char* abs_path = AbsolutePath(source_directory);
  const uint64_t hash = rapidhash(abs_path, strlen(abs_path));

  char hash_str[17];
  snprintf(hash_str, sizeof(hash_str), "%016llx",
           static_cast<unsigned long long>(hash));

  char base_cache[1024];
  GetUserCacheDir("game", base_cache, sizeof(base_cache));
  snprintf(out, out_size, "%s/%s", base_cache, hash_str);
}

void GetUserSaveDir(const char* app_name, char* out, size_t out_size) {
#ifdef GAME_WEB
  // Mounted as IDBFS by the HTML shell; contents persist to IndexedDB.
  snprintf(out, out_size, "/save/%s", app_name);
  return;
#endif
  PathBuffer buf;
#ifdef _WIN32
  const char* app_data = getenv("APPDATA");
  if (app_data != nullptr) {
    buf.Set(app_data, "\\", app_name);
  } else {
    buf.Set(".\\", app_name, "-save");
  }
#elif defined(__APPLE__)
  const char* home = getenv("HOME");
  if (home == nullptr) home = "/tmp";
  buf.Set(home, "/Library/Application Support/", app_name);
#else
  const char* xdg_data = getenv("XDG_DATA_HOME");
  if (xdg_data != nullptr && xdg_data[0] != '\0') {
    buf.Set(xdg_data, "/", app_name);
  } else {
    const char* home = getenv("HOME");
    if (home == nullptr) home = "/tmp";
    buf.Set(home, "/.local/share/", app_name);
  }
#endif
  size_t len = buf.size() < out_size - 1 ? buf.size() : out_size - 1;
  std::memcpy(out, buf.str(), len);
  out[len] = '\0';
}

#ifdef GAME_WEB
namespace {
// Set when save data changed since the last IndexedDB flush.
bool g_idb_dirty = false;
double g_last_idb_sync_ms = 0;
}  // namespace

void RequestIdbSync() { g_idb_dirty = true; }

void SyncIdbNow() {
  g_idb_dirty = false;
  // Asynchronous persist of the IDBFS mount; errors only mean the data
  // stays in memory (e.g. private browsing), which the shell logs.
  EM_ASM({
    Module.FS.syncfs(
        false, function(err) {
          if (err) console.warn('Save sync failed:', err);
        });
  });
}

void MaybeSyncIdb() {
  if (!g_idb_dirty) return;
  const double now = emscripten_get_now();
  // Debounce so bursts of writes (e.g. saving every frame) become one
  // IndexedDB transaction every half second.
  if (now - g_last_idb_sync_ms < 500.0) return;
  g_last_idb_sync_ms = now;
  SyncIdbNow();
}
#endif

#ifdef _WIN32
const char* const kExeExtension = ".exe";
#else
const char* const kExeExtension = "";
#endif

}  // namespace G
