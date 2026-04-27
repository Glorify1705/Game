#pragma once
#ifndef _GAME_LUA_SAVE_H
#define _GAME_LUA_SAVE_H

#include "lua.h"

namespace G {

void AddSaveLibrary(Lua* lua);
LuaLibraryDef GetSaveLibraryDef();

}  // namespace G

#endif
