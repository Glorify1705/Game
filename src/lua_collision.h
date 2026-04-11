#pragma once
#ifndef _GAME_LUA_COLLISION_H
#define _GAME_LUA_COLLISION_H

#include "lua.h"

namespace G {

void AddCollisionLibrary(Lua* lua);
LuaLibraryDef GetCollisionLibraryDef();

}  // namespace G

#endif  // _GAME_LUA_COLLISION_H
