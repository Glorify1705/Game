#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cli.h"
#include "config.h"
#include "filesystem.h"
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

bool CopyFile(const char* src, const char* dst) {
  FILE* in = fopen(src, "rb");
  if (in == nullptr) return false;
  FILE* out = fopen(dst, "wb");
  if (out == nullptr) {
    fclose(in);
    return false;
  }
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    fwrite(buf, 1, n, out);
  }
  fclose(in);
  fclose(out);
  return true;
}

}  // namespace

int CmdPackage(int argc, const char* argv[]) {
  const char* source_directory = ".";
  const char* output_dir = "dist";
  const char* name_override = nullptr;
  bool strip = false;

  for (int i = 1; i < argc; ++i) {
    if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) &&
        i + 1 < argc) {
      output_dir = argv[++i];
    } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
      name_override = argv[++i];
    } else if (strcmp(argv[i], "--strip") == 0) {
      strip = true;
    } else if (argv[i][0] != '-') {
      source_directory = argv[i];
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
  ArenaAllocator config_arena(static_cast<uint8_t*>(malloc(Megabytes(1))),
                              Megabytes(1));
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
  MakeDirs(output_dir);

  // Pack assets into a temporary database.
  FixedStringBuffer<1024> db_path(output_dir, "/assets.sqlite3");
  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.str(), &db) != SQLITE_OK) {
    fprintf(stderr, "Error: failed to create database '%s': %s\n",
            db_path.str(), sqlite3_errmsg(db));
    return 1;
  }
  InitializeAssetDb(db);

  ArenaAllocator packer_arena(static_cast<uint8_t*>(malloc(Megabytes(64))),
                              Megabytes(64));
  WriteAssetsToDb(source_directory, db, &packer_arena);
  sqlite3_close(db);

  // Copy the engine binary.
  FixedStringBuffer<1024> binary_path(output_dir, "/", binary_name);
#ifdef _WIN32
  binary_path.Append(".exe");
  char self_path[MAX_PATH];
  GetModuleFileNameA(nullptr, self_path, MAX_PATH);
#else
  char self_path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
  if (len <= 0) {
    fprintf(stderr, "Error: could not determine engine binary path.\n");
    return 1;
  }
  self_path[len] = '\0';
#endif

  if (!CopyFile(self_path, binary_path.str())) {
    fprintf(stderr, "Error: could not copy binary to '%s'.\n",
            binary_path.str());
    return 1;
  }

#ifndef _WIN32
  // Make the binary executable.
  chmod(binary_path.str(), 0755);
#endif

  // Optionally strip.
  if (strip) {
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
