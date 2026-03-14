#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "cli.h"
#include "config.h"
#include "libraries/sqlite3.h"
#include "logging.h"
#include "packer.h"
#include "platform.h"
#include "stringlib.h"
#include "units.h"

namespace G {

int CmdPackage(Slice<const char*> args, Allocator* allocator) {
  const char* source_directory = ".";
  const char* output_dir = "dist";
  const char* name_override = nullptr;
  bool strip = false;

  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if ((arg == "-o" || arg == "--output") && i + 1 < args.size()) {
      output_dir = args[++i];
    } else if (arg == "--name" && i + 1 < args.size()) {
      name_override = args[++i];
    } else if (arg == "--strip") {
      strip = true;
    } else if (arg[0] != '-') {
      source_directory = args[i];
    }
  }

  // Validate project.
  FixedStringBuffer<1024> conf_path(source_directory, "/conf.json");
  if (!FileExists(conf_path.str())) {
    fprintf(stderr, "Error: no game project found in '%s'.\n",
            source_directory);
    return 1;
  }

  FixedStringBuffer<1024> main_path(source_directory, "/main.lua");
  if (!FileExists(main_path.str())) {
    fprintf(stderr,
            "Error: no entry point script found in '%s'.\n"
            "Create main.lua or main.fnl.\n",
            source_directory);
    return 1;
  }

  // Determine binary name from conf.json or override.
  const char* binary_name = "game";
  GameConfig config;
  ArenaAllocator config_arena(allocator, Megabytes(1));
  LOG("Loading config from ", conf_path.str());
  if (LoadConfigFromFile(conf_path.str(), &config, &config_arena)) {
    if (config.app_name[0] != '\0') {
      binary_name = config.app_name;
    }
  }
  if (name_override != nullptr) {
    binary_name = name_override;
  }

  // Initialize PhysFS (required by the asset packer).
  PHYSFS_CHECK(PHYSFS_init("game"), "Could not initialize PhysFS");

  // Create output directory.
  LOG("Creating output directory: ", output_dir);
  MakeDirs(output_dir);

  // Pack assets into the database.
  FixedStringBuffer<1024> db_path(output_dir, "/assets.sqlite3");
  sqlite3* db = nullptr;
  LOG("Creating asset database: ", db_path.str());
  if (sqlite3_open(db_path.str(), &db) != SQLITE_OK) {
    fprintf(stderr, "Error: failed to create database '%s': %s\n",
            db_path.str(), sqlite3_errmsg(db));
    return 1;
  }
  InitializeAssetDb(db);

  ArenaAllocator packer_arena(allocator, Megabytes(64));
  LOG("Packing assets from ", source_directory);
  WriteAssetsToDb(source_directory, db, &packer_arena);
  sqlite3_close(db);

  // Copy the engine binary.
  char self_path[1024];
  if (!GetExePath(self_path, sizeof(self_path))) {
    fprintf(stderr, "Error: could not determine engine binary path.\n");
    return 1;
  }

  FixedStringBuffer<1024> binary_path(output_dir, "/", binary_name,
                                      kExeExtension);
  LOG("Copying engine binary to ", binary_path.str());
  if (!CopyFile(self_path, binary_path.str())) {
    fprintf(stderr, "Error: could not copy binary to '%s'.\n",
            binary_path.str());
    return 1;
  }

  MakeExecutable(binary_path.str());

  // Optionally strip.
  if (strip) {
    LOG("Stripping binary: ", binary_path.str());
    FixedStringBuffer<1024> strip_cmd("strip ", binary_path.str());
    (void)system(strip_cmd.str());
  }

  PHYSFS_CHECK(PHYSFS_deinit(), "Could not close PhysFS");

  printf("Packaged game to '%s':\n", output_dir);
  printf("  %s\n", binary_path.str());
  printf("  %s\n", db_path.str());
  printf("\nRun with: %s\n", binary_path.str());
  return 0;
}

}  // namespace G
