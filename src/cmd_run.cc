#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cli.h"
#include "config.h"
#include "game.h"
#include "libraries/rapidhash.h"
#include "libraries/sqlite3.h"
#include "packer.h"
#include "stringlib.h"
#include "units.h"

#ifndef _WIN32
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace G {

namespace {

// Resolves a path to an absolute path. Caller must use the result before the
// next call (returns a static buffer).
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

bool DirectoryExists(const char* path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool FileExists(const char* path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

bool MakeDirs(const char* path) {
#ifdef _WIN32
  // Simple recursive mkdir for Windows.
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

void ComputeCacheDir(const char* source_directory, char* out, size_t out_size) {
  const char* abs_path = AbsolutePath(source_directory);
  uint64_t hash = rapidhash(abs_path, strlen(abs_path));

  const char* home = getenv("HOME");
  if (home == nullptr) home = "/tmp";

  char hash_str[17];
  snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)hash);

#ifdef __APPLE__
  snprintf(out, out_size, "%s/Library/Caches/game/%s", home, hash_str);
#else
  const char* xdg_cache = getenv("XDG_CACHE_HOME");
  if (xdg_cache != nullptr && xdg_cache[0] != '\0') {
    snprintf(out, out_size, "%s/game/%s", xdg_cache, hash_str);
  } else {
    snprintf(out, out_size, "%s/.cache/game/%s", home, hash_str);
  }
#endif
}

}  // namespace

int CmdRun(int argc, const char* argv[]) {
  const char* source_directory = ".";
  bool hotreload = true;
  bool clean = false;
  size_t game_argc = 0;
  const char** game_argv = nullptr;

  // Parse arguments: game run [dir] [--flags] [-- game-args...]
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--") == 0) {
      game_argc = argc - i - 1;
      game_argv = argv + i + 1;
      break;
    }
    if (strcmp(argv[i], "--no-hotreload") == 0) {
      hotreload = false;
    } else if (strcmp(argv[i], "--clean") == 0) {
      clean = true;
    } else if (argv[i][0] != '-') {
      source_directory = argv[i];
    }
  }

  // Verify the project directory exists.
  if (!DirectoryExists(source_directory)) {
    fprintf(stderr, "Error: directory '%s' does not exist.\n",
            source_directory);
    return 1;
  }

  // Verify conf.json exists.
  FixedStringBuffer<1024> conf_path(source_directory, "/conf.json");
  if (!FileExists(conf_path.str())) {
    fprintf(stderr,
            "Error: no game project found in '%s'.\n"
            "No conf.json file. Run 'game init' first.\n",
            source_directory);
    return 1;
  }

  // Compute cache directory and database path.
  char cache_dir[1024];
  ComputeCacheDir(source_directory, cache_dir, sizeof(cache_dir));
  MakeDirs(cache_dir);

  FixedStringBuffer<1024> db_path(cache_dir, "/assets.sqlite3");

  // Handle --clean: delete the cached database.
  if (clean) {
    remove(db_path.str());
  }

  // Configure SQLite memory.
  ArenaAllocator sqlite_arena(static_cast<uint8_t*>(malloc(Megabytes(16))),
                              Megabytes(16));
  CHECK(sqlite3_config(SQLITE_CONFIG_HEAP,
                       sqlite_arena.Alloc(Megabytes(16), kMaxAlign),
                       Megabytes(16), 64) == SQLITE_OK,
        "Failed to configure SQLite memsys5 heap");

  // Open/create the database.
  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.str(), &db) != SQLITE_OK) {
    fprintf(stderr, "Error: failed to open database '%s': %s\n", db_path.str(),
            sqlite3_errmsg(db));
    return 1;
  }
  InitializeAssetDb(db);

  // Build GameOptions and run.
  GameOptions opts;
  opts.source_directory = source_directory;
  opts.hotreload = hotreload;
  opts.argc = game_argc;
  opts.argv = game_argv;

  RunGame(opts, db);
  return 0;
}

int CmdRunPackaged(int argc, const char* argv[]) {
  // Find the directory containing the binary.
#ifdef _WIN32
  char exe_path[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
#else
  char exe_path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len <= 0) {
    fprintf(stderr, "Error: could not determine binary location.\n");
    return 1;
  }
  exe_path[len] = '\0';
#endif

  char* last_slash = strrchr(exe_path, '/');
#ifdef _WIN32
  char* last_bslash = strrchr(exe_path, '\\');
  if (last_bslash > last_slash) last_slash = last_bslash;
#endif
  if (last_slash != nullptr) last_slash[1] = '\0';

  FixedStringBuffer<1024> db_path(exe_path, "assets.sqlite3");

  // Configure SQLite memory.
  ArenaAllocator sqlite_arena(static_cast<uint8_t*>(malloc(Megabytes(16))),
                              Megabytes(16));
  CHECK(sqlite3_config(SQLITE_CONFIG_HEAP,
                       sqlite_arena.Alloc(Megabytes(16), kMaxAlign),
                       Megabytes(16), 64) == SQLITE_OK,
        "Failed to configure SQLite memsys5 heap");

  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.str(), &db) != SQLITE_OK) {
    fprintf(stderr, "Error: failed to open database '%s': %s\n", db_path.str(),
            sqlite3_errmsg(db));
    return 1;
  }

  // Parse game arguments (after --).
  size_t game_argc = 0;
  const char** game_argv = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--") == 0) {
      game_argc = argc - i - 1;
      game_argv = argv + i + 1;
      break;
    }
  }

  GameOptions opts;
  opts.source_directory = nullptr;
  opts.hotreload = false;
  opts.argc = game_argc;
  opts.argv = game_argv;

  RunGame(opts, db);
  return 0;
}

bool PackagedGameExists(const char* argv0) {
#ifdef _WIN32
  char exe_path[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
#elif defined(__APPLE__)
  char exe_path[PATH_MAX];
  uint32_t size = sizeof(exe_path);
  if (_NSGetExecutablePath(exe_path, &size) != 0) return false;
#else
  char exe_path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len <= 0) return false;
  exe_path[len] = '\0';
#endif

  // Find the directory containing the binary.
  char* last_slash = strrchr(exe_path, '/');
#ifdef _WIN32
  char* last_bslash = strrchr(exe_path, '\\');
  if (last_bslash > last_slash) last_slash = last_bslash;
#endif
  if (last_slash == nullptr) return false;

  last_slash[1] = '\0';
  FixedStringBuffer<1024> asset_path(exe_path, "assets.sqlite3");

#ifdef _WIN32
  DWORD attr = GetFileAttributesA(asset_path.str());
  return attr != INVALID_FILE_ATTRIBUTES;
#else
  struct stat st;
  return stat(asset_path.str(), &st) == 0;
#endif
}

}  // namespace G
