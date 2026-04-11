#pragma once
#ifndef _GAME_LUA_CAMERA_H
#define _GAME_LUA_CAMERA_H

#include "lua.h"

namespace G {

void AddCameraLibrary(Lua* lua);
LuaLibraryDef GetCameraLibraryDef();

}  // namespace G

#endif  // _GAME_LUA_CAMERA_H
