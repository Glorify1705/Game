#pragma once
#ifndef _GAME_LUA_PARTICLES_H
#define _GAME_LUA_PARTICLES_H

#include "lua.h"

namespace G {

void AddParticlesLibrary(Lua* lua);
LuaLibraryDef GetParticlesLibraryDef();

}  // namespace G

#endif
