#pragma once
#ifndef GAME_LUA_NETWORK_H
#define GAME_LUA_NETWORK_H

#include "lua.h"

namespace G {

// Registers the G.network Lua library (ENet wrapper).
void AddNetworkLibrary(Lua* lua);
// Returns the library definition for stub generation.
LuaLibraryDef GetNetworkLibraryDef();

}  // namespace G

#endif
