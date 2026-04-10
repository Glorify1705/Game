#pragma once
#ifndef _GAME_LUA_JSON_H
#define _GAME_LUA_JSON_H

#include "error.h"
#include "libraries/yyjson.h"
#include "lua.h"

namespace G {

// Push the yyjson value onto the Lua stack as the equivalent Lua type.
// Returns the number of values pushed (always 1).
int LuaPushJsonValue(lua_State* state, yyjson_val* val);

// Read the Lua value at `index` and build the equivalent yyjson mutable
// node inside `doc`. Returns an error for unsupported types (function,
// userdata, thread) or unrepresentable values (NaN, Inf).
ErrorOr<yyjson_mut_val*> LuaToJsonValue(lua_State* state, int index,
                                        yyjson_mut_doc* doc);

// Registers the G.json library (decode, encode).
void AddJsonLibrary(Lua* lua);

}  // namespace G

#endif  // _GAME_LUA_JSON_H
