#pragma once
#ifndef _GAME_LUA_TILEMAP_H
#define _GAME_LUA_TILEMAP_H

#include "lua.h"

namespace G {

void AddTilemapLibrary(Lua* lua);
LuaLibraryDef GetTilemapLibraryDef();

}  // namespace G

#endif  // _GAME_LUA_TILEMAP_H
