#pragma once
#ifndef GAME_LUA_DATA_H
#define GAME_LUA_DATA_H

#include "assets.h"
#include "lua.h"

namespace G {

// Registers G.data library (protobuf encode/decode) and opens the pb module.
// If db_assets is provided, loads and compiles all .proto files from the DB.
void AddDataLibrary(Lua* lua, DbAssets* db_assets = nullptr);
// Returns the library definition for stub generation.
LuaLibraryDef GetDataLibraryDef();

}  // namespace G

#endif
