#include <cstdio>
#include <cstring>
#include <string_view>

#include "cli.h"
#include "game.h"
#include "libraries/rapidhash.h"
#include "libraries/sqlite3.h"
#include "logging.h"
#include "packer.h"
#include "platform.h"
#include "stringlib.h"
#include "units.h"

namespace G {

namespace {

void ComputeCacheDir(const char* source_directory, char* out, size_t out_size) {
  const char* abs_path = AbsolutePath(source_directory);
  uint64_t hash = rapidhash(abs_path, strlen(abs_path));

  char hash_str[17];
  snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)hash);

  char base_cache[1024];
  GetUserCacheDir("game", base_cache, sizeof(base_cache));
  snprintf(out, out_size, "%s/%s", base_cache, hash_str);
}

}  // namespace

int CmdRun(Slice<const char*> args, Allocator* allocator) {
  const char* source_directory = ".";
  bool hotreload = true;
  bool clean = false;
  Slice<const char*> game_args;

  // Parse arguments: game run [dir] [--flags] [-- game-args...]
  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if (arg == "--") {
      game_args = {args.data() + i + 1, args.size() - i - 1};
      break;
    }
    if (arg == "--no-hotreload") {
      hotreload = false;
    } else if (arg == "--clean") {
      clean = true;
    } else if (arg[0] != '-') {
      source_directory = args[i];
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
  LOG("Cache directory: ", cache_dir);
  MakeDirs(cache_dir);

  FixedStringBuffer<1024> db_path(cache_dir, "/assets.sqlite3");

  // Handle --clean: delete the cached database.
  if (clean) {
    LOG("Deleting cached database: ", db_path.str());
    remove(db_path.str());
  }

  // Configure SQLite memory.
  ArenaAllocator sqlite_arena(allocator, Megabytes(16));
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
  opts.args = game_args;

  RunGame(opts, db);
  return 0;
}

int CmdRunPackaged(Slice<const char*> args, Allocator* allocator) {
  // Find the directory containing the binary.
  char exe_dir[1024];
  if (!GetExeDir(exe_dir, sizeof(exe_dir))) {
    fprintf(stderr, "Error: could not determine binary location.\n");
    return 1;
  }

  FixedStringBuffer<1024> db_path(exe_dir, "assets.sqlite3");

  // Configure SQLite memory.
  ArenaAllocator sqlite_arena(allocator, Megabytes(16));
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
  Slice<const char*> game_args;
  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if (arg == "--") {
      game_args = {args.data() + i + 1, args.size() - i - 1};
      break;
    }
  }

  GameOptions opts;
  opts.source_directory = nullptr;
  opts.hotreload = false;
  opts.args = game_args;

  RunGame(opts, db);
  return 0;
}

bool PackagedGameExists(const char* argv0) {
  char exe_dir[1024];
  if (!GetExeDir(exe_dir, sizeof(exe_dir))) return false;
  FixedStringBuffer<1024> asset_path(exe_dir, "assets.sqlite3");
  return FileExists(asset_path.str());
}

}  // namespace G
