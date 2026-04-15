#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "cli.h"
#include "config.h"
#include "error.h"
#include "executor.h"
#include "filesystem.h"
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
  const char* engine_binary = nullptr;
  bool strip = false;
  bool sfx = false;

  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if ((arg == "-o" || arg == "--output") && i + 1 < args.size()) {
      output_dir = args[++i];
    } else if (arg == "--name" && i + 1 < args.size()) {
      name_override = args[++i];
    } else if (arg == "--engine-binary" && i + 1 < args.size()) {
      engine_binary = args[++i];
    } else if (arg == "--strip") {
      strip = true;
    } else if (arg == "--sfx") {
      sfx = true;
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
  if (!LoadConfigFromFile(conf_path.str(), &config, &config_arena).is_error()) {
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
  MUST(MakeDirs(output_dir));

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

  ArenaAllocator packer_arena(allocator, Megabytes(512));
  LOG("Packing assets from ", source_directory);
  ThreadPoolExecutor executor(&packer_arena,
                              ThreadPoolExecutor::NumDefaultThreads());
  executor.Start();
  MUST(WriteAssetsToDb(source_directory, db, &packer_arena, &executor));
  executor.Shutdown();
  sqlite3_close(db);

  // Copy the engine binary.
  const char* src_binary = nullptr;
  char self_path[1024];
  if (engine_binary != nullptr) {
    if (!FileExists(engine_binary)) {
      fprintf(stderr, "Error: engine binary not found: '%s'.\n", engine_binary);
      return 1;
    }
    src_binary = engine_binary;
  } else {
    if (GetExePath(self_path, sizeof(self_path)).is_error()) {
      fprintf(stderr, "Error: could not determine engine binary path.\n");
      return 1;
    }
    src_binary = self_path;
  }

  // Detect target extension from the source binary path.
  const char* exe_ext = kExeExtension;
  if (HasSuffix(src_binary, ".exe")) {
    exe_ext = ".exe";
  }

  FixedStringBuffer<1024> binary_path(output_dir, "/", binary_name, exe_ext);
  LOG("Copying engine binary to ", binary_path.str());
  if (CopyFile(src_binary, binary_path.str()).is_error()) {
    fprintf(stderr, "Error: could not copy binary to '%s'.\n",
            binary_path.str());
    return 1;
  }

  // Copy DLLs adjacent to the engine binary (for cross-compiled Windows
  // builds).
  if (exe_ext[0] != '\0') {
    std::string_view src_path(src_binary);
    size_t last_sep = src_path.rfind('/');
    if (last_sep != std::string_view::npos) {
      std::string_view src_dir = src_path.substr(0, last_sep);
      const char* dlls[] = {"SDL3.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll",
                            "libwinpthread-1.dll"};
      for (const char* dll : dlls) {
        FixedStringBuffer<1024> dll_src(src_dir, "/", dll);
        if (FileExists(dll_src.str())) {
          FixedStringBuffer<1024> dll_dst(output_dir, "/", dll);
          LOG("Copying ", dll, " to ", dll_dst.str());
          MUST(CopyFile(dll_src.str(), dll_dst.str()));
        }
      }
    }
  }

  if (exe_ext[0] == '\0') {
    MUST(MakeExecutable(binary_path.str()));
  }

  // Optionally strip.
  if (strip) {
    LOG("Stripping binary: ", binary_path.str());
    FixedStringBuffer<1024> strip_cmd("strip ", binary_path.str());
    (void)system(strip_cmd.str());
  }

  PHYSFS_CHECK(PHYSFS_deinit(), "Could not close PhysFS");

  // Create a self-extracting archive from the output directory.
  if (sfx) {
    // Locate the SFX stub relative to our own executable.
    char exe_dir[1024];
    if (GetExeDir(exe_dir, sizeof(exe_dir)).is_error()) {
      fprintf(stderr, "Error: could not determine executable directory.\n");
      return 1;
    }
    FixedStringBuffer<1024> sfx_stub(exe_dir, "/../toolchains/7z-sfx/7zSD.sfx");
    if (!FileExists(sfx_stub.str())) {
      fprintf(stderr,
              "Error: SFX stub not found at '%s'.\n"
              "Run: scripts/setup-7z-sfx.sh\n",
              sfx_stub.str());
      return 1;
    }

    FixedStringBuffer<1024> sfx_output(output_dir, "/", binary_name, ".7z",
                                       exe_ext);
    FixedStringBuffer<1024> tmp_config(output_dir, "/sfx_config.txt");
    FixedStringBuffer<1024> tmp_archive(output_dir, "/sfx_archive.7z");

    // Write SFX config.
    FILE* cfg = fopen(tmp_config.str(), "w");
    if (cfg == nullptr) {
      fprintf(stderr, "Error: could not create SFX config.\n");
      return 1;
    }
    fprintf(cfg, ";!@Install@!UTF-8!\n");
    fprintf(cfg, "RunProgram=\"%s%s\"\n", binary_name, exe_ext);
    fprintf(cfg, ";!@InstallEnd@!\n");
    fclose(cfg);

    // Create 7z archive of the output directory contents.
    FixedStringBuffer<1024> archive_cmd;
    archive_cmd.AllowTruncation();
    archive_cmd.AppendF(
        "cd \"%s\" && 7z a -mx=3 sfx_archive.7z . "
        "-x!sfx_config.txt -x!sfx_archive.7z",
        output_dir);
    LOG("Creating SFX archive...");
    int ret = system(archive_cmd.str());
    if (ret != 0) {
      fprintf(stderr,
              "Error: 7z archive creation failed (is p7zip installed?).\n");
      remove(tmp_config.str());
      return 1;
    }

    // Concatenate: SFX stub + config + archive.
    LOG("Building SFX executable: ", sfx_output.str());
    FILE* out = fopen(sfx_output.str(), "wb");
    if (out == nullptr) {
      fprintf(stderr, "Error: could not create '%s'.\n", sfx_output.str());
      remove(tmp_config.str());
      remove(tmp_archive.str());
      return 1;
    }

    auto append_file = [](FILE* dst, const char* path) -> bool {
      FILE* src = fopen(path, "rb");
      if (src == nullptr) return false;
      char buf[8192];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
      }
      fclose(src);
      return true;
    };

    bool ok = true;
    if (!append_file(out, sfx_stub.str())) {
      fprintf(stderr, "Error: could not read SFX stub '%s'.\n", sfx_stub.str());
      ok = false;
    } else if (!append_file(out, tmp_config.str())) {
      fprintf(stderr, "Error: could not read SFX config '%s'.\n",
              tmp_config.str());
      ok = false;
    } else if (!append_file(out, tmp_archive.str())) {
      fprintf(stderr, "Error: could not read SFX archive '%s'.\n",
              tmp_archive.str());
      ok = false;
    }
    fclose(out);

    // Clean up temp files.
    remove(tmp_config.str());
    remove(tmp_archive.str());

    if (!ok) {
      fprintf(stderr, "Error: failed to build SFX executable.\n");
      remove(sfx_output.str());
      return 1;
    }

    printf("Created SFX: %s\n", sfx_output.str());
  } else {
    printf("Packaged game to '%s':\n", output_dir);
    printf("  %s\n", binary_path.str());
    printf("  %s\n", db_path.str());
    printf("\nRun with: %s\n", binary_path.str());
  }
  return 0;
}

}  // namespace G
