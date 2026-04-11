#pragma once
#ifndef _GAME_LUA_ASSETS_H
#define _GAME_LUA_ASSETS_H

#include "lua.h"

namespace G {

void AddAssetsLibrary(Lua* lua);
LuaLibraryDef GetAssetsLibraryDef();

}  // namespace G

#endif  // _GAME_LUA_ASSETS_H
