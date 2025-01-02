#pragma once
#ifndef _GAME_LUA_FILESYSTEM_H
#define _GAME_LUA_FILESYSTEM_H

#include "lua.h"

namespace G {

// Write the value at `index` in the Lua stack to a file with name `filename`.
int LuaWriteToFile(lua_State* state, int index, std::string_view filename);

// Load the file into a buffer and push it into the Lua stack.
int LuaLoadFileIntoBuffer(lua_State* state, std::string_view filename);

void AddFilesystemLibrary(Lua* lua);

}  // namespace G

#endif  // _GAME_LUA_FILESYSTEM_H
