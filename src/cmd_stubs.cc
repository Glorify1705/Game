#include <cstdio>
#include <cstring>

#include "allocators.h"
#include "cli.h"
#include "lua.h"
#include "lua_assets.h"
#include "lua_bytebuffer.h"
#include "lua_filesystem.h"
#include "lua_graphics.h"
#include "lua_input.h"
#include "lua_math.h"
#include "lua_physics.h"
#include "lua_random.h"
#include "lua_sound.h"
#include "lua_system.h"
#include "mimalloc_allocator.h"
#include "units.h"

namespace G {

int CmdStubs(int argc, const char* argv[]) {
  const char* output = "definitions/game.lua";
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
      output = argv[++i];
    }
  }

  auto* allocator = new StaticAllocator<Megabytes(32)>();
  MimallocAllocator lua_alloc(allocator->Alloc(Megabytes(16), kMaxAlign),
                              Megabytes(16));
  Lua lua(0, nullptr, nullptr, nullptr, &lua_alloc);
  lua.LoadLibraries();
  AddByteBufferLibrary(&lua);
  AddFilesystemLibrary(&lua);
  AddGraphicsLibrary(&lua);
  AddInputLibrary(&lua);
  AddMathLibrary(&lua);
  AddPhysicsLibrary(&lua);
  AddRandomLibrary(&lua);
  AddSoundLibrary(&lua);
  AddSystemLibrary(&lua);
  AddAssetsLibrary(&lua);
  lua.GenerateLuaLSStubs(output);
  printf("Wrote LuaLS stubs to %s\n", output);
  delete allocator;
  return 0;
}

}  // namespace G
