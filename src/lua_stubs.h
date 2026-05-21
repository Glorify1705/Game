#pragma once
#ifndef _GAME_LUA_STUBS_H
#define _GAME_LUA_STUBS_H

#include "lua.h"

namespace G {

// Writes LuaLS stub definitions to |output_path| from the given library defs.
void WriteLuaLSStubs(const char* output_path, const LuaLibraryDef* defs,
                     size_t def_count);

}  // namespace G

#endif  // _GAME_LUA_STUBS_H
