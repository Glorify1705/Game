#pragma once
#ifndef GAME_LUA_DATA_H
#define GAME_LUA_DATA_H

#include "lua.h"

namespace G {

// Registers G.data library (protobuf encode/decode) and opens the pb module.
void AddDataLibrary(Lua* lua);
LuaLibraryDef GetDataLibraryDef();

}  // namespace G

#endif
