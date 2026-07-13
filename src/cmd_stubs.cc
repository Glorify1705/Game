#include <cstdio>
#include <cstring>
#include <string_view>

#include "cli.h"
#include "error.h"
#include "lua_assets.h"
#include "lua_bytebuffer.h"
#include "lua_camera.h"
#include "lua_collision.h"
#include "lua_filesystem.h"
#include "lua_graphics.h"
#include "lua_input.h"
#include "lua_json.h"
#include "lua_log.h"
#include "lua_math.h"
#include "lua_particles.h"
#include "lua_physics.h"
#include "lua_random.h"
#include "lua_save.h"
#include "lua_scene.h"
#include "lua_sound.h"
#include "lua_stubs.h"
#include "lua_system.h"
#include "lua_test.h"
#include "lua_tilemap.h"
#include "lua_timer.h"
#include "platform.h"

namespace G {

namespace {

void PrintHelp() {
  printf("Usage: game stubs [options]\n");
  printf("\n");
  printf("Generates LuaLS type stubs for the engine's Lua API.\n");
  printf("The output file provides autocompletion and type checking\n");
  printf("in editors that support the Lua Language Server.\n");
  printf("\n");
  printf("Options:\n");
  printf(
      "  --output <path>       Output file (default: definitions/game.lua)\n");
}

}  // namespace

int CmdStubs(Slice<const char*> args, Allocator* /*allocator*/) {
  const char* output = "definitions/game.lua";
  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if (arg == "--help" || arg == "-h") {
      PrintHelp();
      return 0;
    }
    if (arg == "--output" && i + 1 < args.size()) {
      output = args[++i];
    }
  }

  // Ensure the parent directory exists.
  if (const char* slash = strrchr(output, '/')) {
    char parent[1024];
    size_t len = slash - output;
    memcpy(parent, output, len);
    parent[len] = '\0';
    MUST(MakeDirs(parent));
  }

  // Collect all library metadata. No Lua runtime needed.
  const LuaLibraryDef defs[] = {
      GetByteBufferLibraryDef(), GetCameraLibraryDef(),
      GetFilesystemLibraryDef(), GetGraphicsLibraryDef(),
      GetInputLibraryDef(),      GetLogLibraryDef(),
      GetMathLibraryDef(),       GetPhysicsLibraryDef(),
      GetRandomLibraryDef(),     GetSoundLibraryDef(),
      GetSystemLibraryDef(),     GetAssetsLibraryDef(),
      GetCollisionLibraryDef(),  GetJsonLibraryDef(),
      GetTestLibraryDef(),       GetTimerLibraryDef(),
      GetSceneLibraryDef(),      GetParticlesLibraryDef(),
      GetSaveLibraryDef(),       GetTilemapLibraryDef(),
  };

  WriteLuaLSStubs(output, defs, std::size(defs));
  printf("Wrote LuaLS stubs to %s\n", output);
  return 0;
}

}  // namespace G
