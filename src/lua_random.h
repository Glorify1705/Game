#pragma once
#ifndef _GAME_LUA_RANDOM_H
#define _GAME_LUA_RANDOM_H

#include "lua.h"

namespace G {

void AddRandomLibrary(Lua* lua);
LuaLibraryDef GetRandomLibraryDef();

}  // namespace G

#endif
