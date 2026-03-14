#include <cstdio>
#include <cstring>
#include <string_view>

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
#include "platform.h"
#include "units.h"

namespace G {

int CmdStubs(Slice<const char*> args, Allocator* allocator) {
  const char* output = "definitions/game.lua";
  for (size_t i = 1; i < args.size(); ++i) {
    if (std::string_view(args[i]) == "--output" && i + 1 < args.size()) {
      output = args[++i];
    }
  }

  // Ensure the parent directory exists.
  if (const char* slash = strrchr(output, '/')) {
    char parent[1024];
    size_t len = slash - output;
    memcpy(parent, output, len);
    parent[len] = '\0';
    MakeDirs(parent);
  }

  // TODO: Decouple stub generation from the Lua class.  The type and library
  // metadata is statically known and should be loadable without instantiating
  // a full Lua runtime.
  MimallocAllocator lua_alloc(allocator->Alloc(Megabytes(16), kMaxAlign),
                              Megabytes(16));
  Lua lua(/*args=*/{}, /*db=*/nullptr, /*assets=*/nullptr, &lua_alloc);
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
  return 0;
}

}  // namespace G
