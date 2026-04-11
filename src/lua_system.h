#pragma once
#ifndef _GAME_LUA_SYSTEM_H
#define _GAME_LUA_SYSTEM_H

#include "lua.h"

namespace G {

void AddSystemLibrary(Lua* lua);
LuaLibraryDef GetSystemLibraryDef();

}  // namespace G

#endif  // _GAME_LUA_SYSTEM_H
