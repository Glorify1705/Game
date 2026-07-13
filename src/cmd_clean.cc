#include <cstdio>
#include <cstring>
#include <string_view>

#include "cli.h"
#include "platform.h"
#include "stringlib.h"

namespace G {

namespace {

// Deletes every file in the blob cache directory, then the directory itself.
void RemoveBlobsDir(const char* blobs_dir) {
  if (!DirectoryExists(blobs_dir)) return;
  auto result = IterateDirectory(
      blobs_dir,
      [](const DirEntry& entry, void* ud) {
        CmdBuffer path(static_cast<const char*>(ud), "/", entry.name);
        remove(path.str());
      },
      const_cast<char*>(blobs_dir));
  if (result.is_error()) {
    fprintf(stderr, "Warning: could not enumerate '%s'\n", blobs_dir);
    return;
  }
  remove(blobs_dir);
  printf("Deleted %s\n", blobs_dir);
}

void PrintHelp() {
  printf("Usage: game clean [directory]\n");
  printf("\n");
  printf("Deletes the cached asset database for a game project.\n");
  printf("The cache is stored in a per-project directory under the\n");
  printf("user's cache folder.\n");
  printf("\n");
  printf("Arguments:\n");
  printf(
      "  directory             Project directory (default: current "
      "directory)\n");
}

}  // namespace

int CmdClean(Slice<const char*> args, Allocator*) {
  const char* source_directory = ".";

  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if (arg == "--help" || arg == "-h") {
      PrintHelp();
      return 0;
    }
    if (arg[0] != '-') {
      source_directory = args[i];
    }
  }

  if (!DirectoryExists(source_directory)) {
    fprintf(stderr, "Error: directory '%s' does not exist.\n",
            source_directory);
    return 1;
  }

  char cache_dir[1024];
  ComputeCacheDir(source_directory, cache_dir, sizeof(cache_dir));

  CmdBuffer db_path(cache_dir, "/assets.sqlite3");

  if (FileExists(db_path.str())) {
    remove(db_path.str());
    printf("Deleted %s\n", db_path.str());
  } else {
    printf("No cached database for '%s'\n", source_directory);
  }

  CmdBuffer blobs_dir(cache_dir, "/blobs");
  RemoveBlobsDir(blobs_dir.str());
  return 0;
}

}  // namespace G
