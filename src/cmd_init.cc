#include <cstdio>
#include <string_view>

#include "cli.h"
#include "logging.h"
#include "platform.h"
#include "stringlib.h"
#include "templates.h"

namespace G {

// Scaffolds a new game project by creating the directory structure and writing
// starter files (conf.json, main.lua, game.lua, .luarc.json), then generates
// LuaLS type stubs into definitions/.
int CmdInit(Slice<const char*> args, Allocator* allocator) {
  const char* dir = ".";
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i][0] != '-') {
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
    if (GetCwd(cwd, sizeof(cwd))) {
      project_name = Basename(std::string_view(cwd));
    }
    if (project_name.empty()) project_name = "my-game";
  }

  // Create directory if needed.
  MakeDir(dir);

  // Check if project already exists.
  FixedStringBuffer<1024> conf_path(dir, "/conf.json");
  if (FileExists(conf_path.str())) {
    fprintf(stderr,
            "Error: project already exists in '%s' (conf.json found).\n", dir);
    return 1;
  }

  // Write scaffold files.
  FixedStringBuffer<256> name_buf(project_name);

  LOG("Writing ", conf_path.str());
  if (!WriteFileF(conf_path.str(), templates::kConfJson.data(), name_buf.str(),
                  name_buf.str())) {
    fprintf(stderr, "Error: could not write %s\n", conf_path.str());
    return 1;
  }

  FixedStringBuffer<1024> main_path(dir, "/main.lua");
  LOG("Writing ", main_path.str());
  WriteFile(main_path.str(), templates::kMainLua.data());

  FixedStringBuffer<1024> game_path(dir, "/game.lua");
  LOG("Writing ", game_path.str());
  WriteFile(game_path.str(), templates::kGameLua.data());

  FixedStringBuffer<1024> luarc_path(dir, "/.luarc.json");
  LOG("Writing ", luarc_path.str());
  WriteFile(luarc_path.str(), templates::kLuarcJson.data());

  // Create definitions directory and generate stubs.
  FixedStringBuffer<1024> defs_dir(dir, "/definitions");
  MakeDir(defs_dir.str());

  FixedStringBuffer<1024> stubs_output(defs_dir.str(), "/game.lua");
  const char* stubs_argv[] = {"stubs", "--output", stubs_output.str()};
  CmdStubs({stubs_argv, 3}, allocator);

  printf("Created new game project in '%s'.\n", dir);
  printf("\n  cd %s && game run\n\n", dir);
  return 0;
}

}  // namespace G
