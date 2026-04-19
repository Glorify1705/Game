#pragma once
#ifndef GAME_LUA_NETWORK_H
#define GAME_LUA_NETWORK_H

#include "lua.h"

namespace G {

void AddNetworkLibrary(Lua* lua);
LuaLibraryDef GetNetworkLibraryDef();

}  // namespace G

#endif
