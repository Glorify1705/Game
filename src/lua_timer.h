#pragma once
#ifndef _GAME_LUA_TIMER_H
#define _GAME_LUA_TIMER_H

#include "lua.h"

namespace G {

void AddTimerLibrary(Lua* lua);
LuaLibraryDef GetTimerLibraryDef();

}  // namespace G

#endif  // _GAME_LUA_TIMER_H
