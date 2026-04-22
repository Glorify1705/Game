// Lua binding entry point for lua-protobuf.
// Declares luaopen_pb so callers don't need a forward declaration.
#pragma once

#include <lua.h>

#ifdef __cplusplus
extern "C" {
#endif

int luaopen_pb(lua_State* L);

#ifdef __cplusplus
}
#endif
