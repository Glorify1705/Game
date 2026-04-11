#pragma once
#ifndef _GAME_LUA_PHYSICS_H
#define _GAME_LUA_PHYSICS_H

#include "lua.h"

namespace G {

void AddPhysicsLibrary(Lua* lua);
LuaLibraryDef GetPhysicsLibraryDef();

}  // namespace G

#endif  // _GAME_LUA_PHYSICS_H
