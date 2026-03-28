#pragma once
#ifndef _GAME_LUA_LOG_H
#define _GAME_LUA_LOG_H

#include "lua.h"

namespace G {

// Registers G.log library for runtime log level control.
void AddLogLibrary(Lua* lua);

}  // namespace G

#endif
