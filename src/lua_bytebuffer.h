#pragma once
#ifndef _GAME_LUA_BYTEBUFFER_H
#define _GAME_LUA_BYTEBUFFER_H

#include <stdint.h>
#include <string.h>

#include "lua.h"

namespace G {

struct ByteBuffer {
  size_t size;
  uint8_t contents[];
};

uint8_t* PushBufferIntoLua(lua_State* state, size_t size);

void AddByteBufferLibrary(Lua* lua);

}  // namespace G

#endif  // _GAME_LUA_BYTEBUFFER_H
