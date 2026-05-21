#include <cstdio>
#include <string_view>

#include "cli.h"
#include "error.h"
#include "logging.h"
#include "platform.h"
#include "stringlib.h"
#include "templates.h"

namespace G {
namespace {

void PrintHelp() {
  printf("Usage: game init [directory]\n");
  printf("\n");
  printf("Scaffolds a new game project by creating starter files:\n");
  printf("  conf.json       Project configuration\n");
  printf("  main.lua        Entry point (requires game)\n");
  printf("  game.lua        Game module with init/update/draw\n");
  printf("  .luarc.json     LuaLS configuration\n");
  printf("  definitions/    LuaLS type stubs\n");
  printf("\n");
  printf("Existing files are never overwritten.\n");
}

// Write a file only if it doesn't already exist. Returns true if written.
bool WriteIfMissing(const char* path, const char* contents) {
  if (FileExists(path)) {
    LOG("Skipping ", path, " (already exists)");
    return false;
  }
  LOG("Writing ", path);
  MUST(WriteFile(path, contents));
  return true;
}

bool WriteIfMissingF(const char* path, const char* fmt, const char* a1,
                     const char* a2) {
  if (FileExists(path)) {
    LOG("Skipping ", path, " (already exists)");
    return false;
  }
  LOG("Writing ", path);
  if (WriteFileF(path, fmt, a1, a2).is_error()) {
    fprintf(stderr, "Error: could not write %s\n", path);
    return false;
  }
  return true;
}

}  // namespace

// Scaffolds a new game project by creating the directory structure and writing
// starter files (conf.json, main.lua, game.lua, .luarc.json), then generates
// LuaLS type stubs into definitions/.
int CmdInit(Slice<const char*> args, Allocator* allocator) {
  const char* dir = ".";
  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg(args[i]);
    if (arg == "--help" || arg == "-h") {
      PrintHelp();
      return 0;
    }
    if (arg[0] != '-') {
      dir = args[i];
      break;
    }
  }

  // Extract project name from directory.
  std::string_view dir_sv(dir);
  std::string_view project_name = Basename(dir_sv);
  if (project_name.empty() || project_name == "." || project_name == "/") {
    // Use current working directory name.
    char cwd[1024];
    if (!GetCwd(cwd, sizeof(cwd)).is_error()) {
      project_name = Basename(std::string_view(cwd));
    }
    if (project_name.empty()) project_name = "my-game";
  }

  // Create directory if needed.
  MUST(MakeDir(dir));

  // Write scaffold files, skipping any that already exist.
  FixedStringBuffer<256> name_buf(project_name);

  CmdBuffer conf_path(dir, "/conf.json");
  WriteIfMissingF(conf_path.str(), templates::kConfJson, name_buf.str(),
                  name_buf.str());

  CmdBuffer main_path(dir, "/main.lua");
  WriteIfMissing(main_path.str(), templates::kMainLua);

  CmdBuffer game_path(dir, "/game.lua");
  WriteIfMissing(game_path.str(), templates::kGameLua);

  CmdBuffer luarc_path(dir, "/.luarc.json");
  WriteIfMissing(luarc_path.str(), templates::kLuarcJson);

  // Create definitions directory and generate stubs (always regenerated).
  CmdBuffer defs_dir(dir, "/definitions");
  MUST(MakeDir(defs_dir.str()));

  CmdBuffer stubs_output(defs_dir.str(), "/game.lua");
  const char* stubs_argv[] = {"stubs", "--output", stubs_output.str()};
  CmdStubs({stubs_argv, 3}, allocator);

  printf("Created new game project in '%s'.\n", dir);
  printf("\n  cd %s && game run\n\n", dir);
  return 0;
}

}  // namespace G
