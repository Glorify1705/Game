#include <cstdio>
#include <cstring>
#include <string_view>

#include "cli.h"
#include "libraries/rapidhash.h"
#include "platform.h"
#include "stringlib.h"

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

int CmdClean(Slice<const char*> args, Allocator*) {
  const char* source_directory = ".";

  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
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

  FixedStringBuffer<1024> db_path(cache_dir, "/assets.sqlite3");

  if (FileExists(db_path.str())) {
    remove(db_path.str());
    printf("Deleted %s\n", db_path.str());
  } else {
    printf("No cached database for '%s'\n", source_directory);
  }
  return 0;
}

}  // namespace G
