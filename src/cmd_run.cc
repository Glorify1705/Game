#include <cstdio>
#include <cstring>
#include <string_view>

#include "cli.h"
#include "error.h"
#include "game.h"
#include "libraries/sqlite3.h"
#include "logging.h"
#include "memory_budgets.h"
#include "packer.h"
#include "platform.h"
#include "sqlite_helpers.h"
#include "stringlib.h"
#include "units.h"

namespace G {

namespace {

void PrintHelp() {
  printf("Usage: game run [directory] [options] [-- game-args...]\n");
  printf("\n");
  printf("Runs a game project with hot-reload support.\n");
  printf("\n");
  printf("Arguments:\n");
  printf(
      "  directory           Project directory (default: current directory)\n");
  printf("\n");
  printf("Options:\n");
  printf("  --no-hotreload      Disable file watching and hot-reload\n");
  printf(
      "  --clean             Delete the cached asset database before "
      "running\n");
  printf("  --test              Run in test mode (implies --no-hotreload)\n");
  printf("  --                  Pass remaining arguments to the game script\n");
}

}  // namespace

int CmdRun(Slice<const char*> args, Allocator* allocator) {
  const char* source_directory = ".";
  bool hotreload = true;
  bool clean = false;
  bool test_mode = false;
  Slice<const char*> game_args;

  // Parse arguments: game run [dir] [--flags] [-- game-args...]
  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if (arg == "--help" || arg == "-h") {
      PrintHelp();
      return 0;
    }
    if (arg == "--") {
      game_args = {args.data() + i + 1, args.size() - i - 1};
      break;
    }
    if (arg == "--no-hotreload") {
      hotreload = false;
    } else if (arg == "--clean") {
      clean = true;
    } else if (arg == "--test") {
      test_mode = true;
      hotreload = false;
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
  CmdBuffer conf_path(source_directory, "/conf.json");
  if (!FileExists(conf_path.str())) {
    fprintf(stderr,
            "Error: no game project found in '%s'.\n"
            "No conf.json file. Run 'game init' first.\n",
            source_directory);
    return 1;
  }

  // Compute cache directory, database path, and blob directory.
  char cache_dir[1024];
  ComputeCacheDir(source_directory, cache_dir, sizeof(cache_dir));
  LOG("Cache directory: ", cache_dir);
  MUST(MakeDirs(cache_dir));

  CmdBuffer db_path(cache_dir, "/assets.sqlite3");
  CmdBuffer blobs_dir(cache_dir, "/blobs");

  // Handle --clean: delete the cached database. Blobs left behind become
  // unreferenced after the fresh pack and are removed by the startup sweep.
  if (clean) {
    LOG("Deleting cached database: ", db_path.str());
    remove(db_path.str());
  }

  // Configure SQLite memory. The memsys5 heap is process-global and shared
  // by every database: asset metadata, the Lua compilation cache, and the
  // game's save database. RunGame logs the high-water mark at exit; retune
  // the budget from that if games start pushing against it.
  ArenaAllocator sqlite_arena(allocator, kSqliteHeapSize);
  CHECK(sqlite3_config(SQLITE_CONFIG_HEAP,
                       sqlite_arena.Alloc(kSqliteHeapSize, kMaxAlign),
                       kSqliteHeapSize, 64) == SQLITE_OK,
        "Failed to configure SQLite memsys5 heap");

  // Open/create the database.
  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.str(), &db) != SQLITE_OK) {
    fprintf(stderr, "Error: failed to open database '%s': %s\n", db_path.str(),
            sqlite3_errmsg(db));
    return 1;
  }
  sqlite3_busy_timeout(db, /*ms=*/1000);
  InitializeAssetDb(db);

  // Build GameOptions and run.
  GameOptions opts;
  opts.source_directory = source_directory;
  opts.blob_source = blobs_dir.str();
  opts.hotreload = hotreload;
  opts.test_mode = test_mode;
  opts.args = game_args;
  opts.all_args = args;

  return RunGame(opts, db);
}

int CmdRunPackaged(Slice<const char*> args, Allocator* allocator) {
  // Find the directory containing the binary.
  char exe_dir[1024];
  if (GetExeDir(exe_dir, sizeof(exe_dir)).is_error()) {
    fprintf(stderr, "Error: could not determine binary location.\n");
    return 1;
  }

  CmdBuffer db_path(exe_dir, "assets.sqlite3");
  CmdBuffer zip_path(exe_dir, "assets.zip");

  if (!FileExists(zip_path.str())) {
    fprintf(stderr,
            "Error: no asset archive found at '%s'.\n"
            "Re-run 'game package' to produce it.\n",
            zip_path.str());
    return 1;
  }

  // Configure SQLite memory. The memsys5 heap is process-global and shared
  // by every database: asset metadata, the Lua compilation cache, and the
  // game's save database. RunGame logs the high-water mark at exit; retune
  // the budget from that if games start pushing against it.
  ArenaAllocator sqlite_arena(allocator, kSqliteHeapSize);
  CHECK(sqlite3_config(SQLITE_CONFIG_HEAP,
                       sqlite_arena.Alloc(kSqliteHeapSize, kMaxAlign),
                       kSqliteHeapSize, 64) == SQLITE_OK,
        "Failed to configure SQLite memsys5 heap");

  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.str(), &db) != SQLITE_OK) {
    fprintf(stderr, "Error: failed to open database '%s': %s\n", db_path.str(),
            sqlite3_errmsg(db));
    return 1;
  }
  sqlite3_busy_timeout(db, /*ms=*/1000);

  // Refuse to run against a database packaged by an incompatible engine.
  {
    SqlStmt stmt(db, "PRAGMA user_version");
    if (!stmt.ok() || !MUST(stmt.Step()) ||
        stmt.ColumnInt(0) != kAssetDbSchemaVersion) {
      fprintf(stderr,
              "Error: '%s' was created by an incompatible engine version.\n"
              "Re-run 'game package' to regenerate it.\n",
              db_path.str());
      return 1;
    }
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
  opts.blob_source = zip_path.str();
  opts.hotreload = false;
  opts.args = game_args;
  opts.all_args = args;

  return RunGame(opts, db);
}

bool PackagedGameExists(const char* /*argv0*/) {
  char exe_dir[1024];
  if (GetExeDir(exe_dir, sizeof(exe_dir)).is_error()) return false;
  CmdBuffer asset_path(exe_dir, "assets.sqlite3");
  return FileExists(asset_path.str());
}

}  // namespace G
