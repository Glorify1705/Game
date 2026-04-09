#pragma once
#ifndef _GAME_LUA_TEST_H
#define _GAME_LUA_TEST_H

#include "lua.h"

namespace G {

// Registers the `test` library exposing input injection and coroutine
// helpers (wait_frames, wait_seconds) usable from `_Game.test_inputs`.
void AddTestLibrary(Lua* lua);

}  // namespace G

#endif
