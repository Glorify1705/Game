#pragma once
#ifndef _GAME_LUA_SCENE_H
#define _GAME_LUA_SCENE_H

#include "lua.h"

namespace G {

// Registers the G.scene library.
void AddSceneLibrary(Lua* lua);

// Returns library metadata for stub generation.
LuaLibraryDef GetSceneLibraryDef();

// Pushes the active scene table onto the Lua stack. If no scene is active,
// pushes the _Game global instead. Used by Lua callback routing.
void PushActiveScene(lua_State* state);

// Returns true if the scene system has been activated (at least one
// G.scene.switch call has been made).
bool IsSceneActive(lua_State* state);

// Processes any pending scene transition (switch/push/pop). Called at the
// start of each frame before update(). Executes lifecycle callbacks
// (leave, init, enter, resume) as part of the transition.
void ProcessPendingTransition(lua_State* state);

}  // namespace G

#endif  // _GAME_LUA_SCENE_H
