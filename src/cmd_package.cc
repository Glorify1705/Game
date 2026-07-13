#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "array.h"
#include "blob_store.h"
#include "cli.h"
#include "config.h"
#include "defer.h"
#include "error.h"
#include "executor.h"
#include "filesystem.h"
#include "libraries/sqlite3.h"
#include "logging.h"
#include "packer.h"
#include "platform.h"
#include "sqlite_helpers.h"
#include "stringlib.h"
#include "units.h"
#include "zip_writer.h"

namespace G {

namespace {

// DLLs that must ship alongside a cross-compiled Windows binary:
// - SDL3.dll: SDL3 windowing/input/audio library
// - libgcc_s_seh-1.dll: GCC runtime (structured exception handling)
// - libstdc++-6.dll: C++ standard library (MinGW)
// - libwinpthread-1.dll: POSIX threads implementation for Windows
constexpr const char* kWindowsRuntimeDlls[] = {
    "SDL3.dll",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
};

// Copies runtime DLLs from the directory containing src_binary into output_dir.
void CopyRuntimeDlls(const char* src_binary, const char* output_dir) {
  std::string_view src_path(src_binary);
  size_t last_sep = src_path.rfind('/');
  if (last_sep == std::string_view::npos) return;
  std::string_view src_dir = src_path.substr(0, last_sep);
  for (const char* dll : kWindowsRuntimeDlls) {
    CmdBuffer dll_src(src_dir, "/", dll);
    if (FileExists(dll_src.str())) {
      CmdBuffer dll_dst(output_dir, "/", dll);
      LOG("Copying ", dll, " to ", dll_dst.str());
      MUST(CopyFile(dll_src.str(), dll_dst.str()));
    }
  }
}

// Writes every blob referenced by asset_metadata into a deterministic
// assets.zip: entries are named by their 16-hex-char content hash and added
// in ascending hash order, so packaging identical assets twice produces
// byte-identical archives.
ErrorOr<void> BuildAssetZip(sqlite3* db, const char* blob_dir,
                            const char* zip_path, Allocator* allocator) {
  DynArray<uint64_t> hashes(allocator);
  {
    SqlStmt stmt(db,
                 "SELECT DISTINCT blob_hash FROM asset_metadata "
                 "WHERE blob_hash != 0");
    if (!stmt.ok()) return Error::Message("failed to query blob hashes");
    while (TRY(stmt.Step())) {
      hashes.Push(static_cast<uint64_t>(stmt.ColumnInt64(0)));
    }
  }
  // Sort in unsigned order; SQL ORDER BY would compare as signed int64.
  std::sort(hashes.begin(), hashes.end());

  ZipWriter zip(allocator);
  TRY(zip.Open(zip_path));
  ArenaAllocator blob_scratch(allocator, Megabytes(256));
  for (size_t i = 0; i < hashes.size(); ++i) {
    char name[17];
    FormatBlobName(hashes[i], name);
    PathBuffer blob_path(blob_dir, "/", name);
    blob_scratch.Reset();
    uint8_t* contents = nullptr;
    const size_t size =
        TRY(ReadEntireFile(blob_path.str(), &contents, &blob_scratch));
    TRY(zip.AddEntry(name, ByteSlice(contents, size)));
  }
  TRY(zip.Finish());
  LOG("Wrote ", hashes.size(), " blob(s) to ", zip_path);
  return {};
}

// Appends the contents of src_path to an open file handle.
ErrorOr<void> AppendFileContents(FILE* dst, const char* src_path) {
  FILE* src = fopen(src_path, "rb");
  if (src == nullptr) return Error::Errno(errno);
  DEFER([src] { fclose(src); });
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
    if (fwrite(buf, 1, n, dst) != n) return Error::Errno(errno);
  }
  return {};
}

// Locates the 7-Zip SFX stub relative to our own executable.
ErrorOr<void> FindSfxStub(CmdBuffer* out) {
  char exe_dir[1024];
  TRY(GetExeDir(exe_dir, sizeof(exe_dir)));
  out->Set(exe_dir, "/../toolchains/7z-sfx/7zSD.sfx");
  if (!FileExists(out->str())) {
    fprintf(stderr,
            "Error: SFX stub not found at '%s'.\n"
            "Run: scripts/setup-7z-sfx.sh\n",
            out->str());
    return Error::Message("SFX stub not found");
  }
  return {};
}

// Creates a self-extracting archive from the output directory.
int BuildSfxArchive(const char* output_dir, const char* binary_name,
                    const char* exe_ext) {
  CmdBuffer sfx_stub;
  if (FindSfxStub(&sfx_stub).is_error()) return 1;

  CmdBuffer sfx_output(output_dir, "/", binary_name, ".7z", exe_ext);
  CmdBuffer tmp_config(output_dir, "/sfx_config.txt");
  CmdBuffer tmp_archive(output_dir, "/sfx_archive.7z");
  DEFER([&] {
    remove(tmp_config.str());
    remove(tmp_archive.str());
  });

  // Write SFX config.
  FixedStringBuffer<256> config_contents;
  config_contents.AppendF(
      ";!@Install@!UTF-8!\nRunProgram=\"%s%s\"\n;!@InstallEnd@!\n", binary_name,
      exe_ext);
  if (WriteFile(tmp_config.str(), config_contents.str()).is_error()) {
    fprintf(stderr, "Error: could not create SFX config.\n");
    return 1;
  }

  // Create 7z archive of the output directory contents.
  CmdBuffer archive_cmd(kTruncating);
  archive_cmd.AppendF(
      "cd \"%s\" && 7z a -mx=3 sfx_archive.7z . "
      "-x!sfx_config.txt -x!sfx_archive.7z -x!*.7z.exe",
      output_dir);
  LOG("Creating SFX archive...");
  if (system(archive_cmd.str()) != 0) {
    fprintf(stderr,
            "Error: 7z archive creation failed (is p7zip installed?).\n");
    return 1;
  }

  // Concatenate: SFX stub + config + archive.
  LOG("Building SFX executable: ", sfx_output.str());
  FILE* out = fopen(sfx_output.str(), "wb");
  if (out == nullptr) {
    fprintf(stderr, "Error: could not create '%s'.\n", sfx_output.str());
    return 1;
  }
  DEFER([out] { fclose(out); });

  const char* parts[] = {sfx_stub.str(), tmp_config.str(), tmp_archive.str()};
  for (const char* part : parts) {
    if (AppendFileContents(out, part).is_error()) {
      fprintf(stderr, "Error: could not read '%s'.\n", part);
      remove(sfx_output.str());
      return 1;
    }
  }

  printf("Created SFX: %s\n", sfx_output.str());
  return 0;
}

void PrintHelp() {
  printf("Usage: game package [directory] [options]\n");
  printf("\n");
  printf("Packages a game project for distribution. Asset metadata goes\n");
  printf("into a SQLite database, asset contents into a content-addressed\n");
  printf("assets.zip, and the engine binary is copied alongside them.\n");
  printf("\n");
  printf("Arguments:\n");
  printf(
      "  directory             Project directory (default: current "
      "directory)\n");
  printf("\n");
  printf("Options:\n");
  printf("  -o, --output <dir>    Output directory (default: dist)\n");
  printf(
      "  --name <name>         Override binary name (default: from "
      "conf.json)\n");
  printf(
      "  --engine-binary <path>  Use a specific engine binary instead of "
      "self\n");
  printf("  --strip               Strip debug symbols from the binary\n");
  printf(
      "  --sfx                 Build a self-extracting archive (requires "
      "7z)\n");
}

}  // namespace

int CmdPackage(Slice<const char*> args, Allocator* allocator) {
  const char* source_directory = ".";
  const char* output_dir = "dist";
  const char* name_override = nullptr;
  const char* engine_binary = nullptr;
  bool strip = false;
  bool sfx = false;

  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if (arg == "--help" || arg == "-h") {
      PrintHelp();
      return 0;
    }
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
  CmdBuffer conf_path(source_directory, "/conf.json");
  if (!FileExists(conf_path.str())) {
    fprintf(stderr, "Error: no game project found in '%s'.\n",
            source_directory);
    return 1;
  }

  CmdBuffer main_path(source_directory, "/main.lua");
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
  CmdBuffer db_path(output_dir, "/assets.sqlite3");
  sqlite3* db = nullptr;
  LOG("Creating asset database: ", db_path.str());
  if (sqlite3_open(db_path.str(), &db) != SQLITE_OK) {
    fprintf(stderr, "Error: failed to create database '%s': %s\n",
            db_path.str(), sqlite3_errmsg(db));
    return 1;
  }
  sqlite3_busy_timeout(db, /*ms=*/1000);
  InitializeAssetDb(db);

  // Blobs are packed into the same per-project cache that `game run` uses,
  // then zipped, so packaging after a dev session rewrites almost nothing.
  char cache_dir[1024];
  ComputeCacheDir(source_directory, cache_dir, sizeof(cache_dir));
  CmdBuffer blobs_dir(cache_dir, "/blobs");
  BlobStore blobs = MUST(BlobStore::Create(blobs_dir.str()));

  ArenaAllocator packer_arena(allocator, Megabytes(512));
  LOG("Packing assets from ", source_directory);
  ThreadPoolExecutor executor(&packer_arena,
                              ThreadPoolExecutor::NumDefaultThreads());
  executor.Start();
  MUST(WriteAssetsToDb(source_directory, db, &blobs, &packer_arena, &executor));
  executor.Shutdown();

  CmdBuffer zip_path(output_dir, "/assets.zip");
  MUST(BuildAssetZip(db, blobs.directory(), zip_path.str(), &packer_arena));
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

  CmdBuffer binary_path(output_dir, "/", binary_name, exe_ext);
  LOG("Copying engine binary to ", binary_path.str());
  if (CopyFile(src_binary, binary_path.str()).is_error()) {
    fprintf(stderr, "Error: could not copy binary to '%s'.\n",
            binary_path.str());
    return 1;
  }

  // Copy runtime DLLs for cross-compiled Windows builds.
  if (exe_ext[0] != '\0') {
    CopyRuntimeDlls(src_binary, output_dir);
  }

  if (exe_ext[0] == '\0') {
    MUST(MakeExecutable(binary_path.str()));
  }

  // Optionally strip.
  if (strip) {
    LOG("Stripping binary: ", binary_path.str());
    CmdBuffer strip_cmd("strip ", binary_path.str());
    int result = system(strip_cmd.str());
    if (result != 0) WLOG("strip returned ", result);
  }

  PHYSFS_CHECK(PHYSFS_deinit(), "Could not close PhysFS");

  if (sfx) {
    return BuildSfxArchive(output_dir, binary_name, exe_ext);
  }

  printf("Packaged game to '%s':\n", output_dir);
  printf("  %s\n", binary_path.str());
  printf("  %s\n", db_path.str());
  printf("  %s\n", zip_path.str());
  printf("\nRun with: %s\n", binary_path.str());
  return 0;
}

}  // namespace G
