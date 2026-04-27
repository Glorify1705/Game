#include "lua_scene.h"

#include <cstring>
#include <string_view>

#include "logging.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace G {
namespace {

// Registry key for the scene manager table.
constexpr std::string_view kManagerKey = "_SceneManager";

// Gets or creates the scene manager table in the Lua registry.
// Leaves the manager table on the stack.
void PushManager(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kManagerKey.data());
  if (!lua_isnil(state, -1)) return;
  lua_pop(state, 1);
  // Create: { scenes = {}, stack = {}, initialized = {} }
  lua_newtable(state);
  lua_newtable(state);
  lua_setfield(state, -2, "scenes");
  lua_newtable(state);
  lua_setfield(state, -2, "stack");
  lua_newtable(state);
  lua_setfield(state, -2, "initialized");
  lua_pushvalue(state, -1);
  lua_setfield(state, LUA_REGISTRYINDEX, kManagerKey.data());
}

// Pushes the scene table for a given name from manager.scenes[name].
// Returns true if found, false (and pushes nil) if not.
bool PushSceneByName(lua_State* state, int manager_idx, const char* name) {
  lua_getfield(state, manager_idx, "scenes");
  lua_getfield(state, -1, name);
  lua_remove(state, -2);  // Remove scenes table.
  if (lua_isnil(state, -1)) {
    lua_pop(state, 1);
    return false;
  }
  return true;
}

// Returns the name of the top scene on the stack, or empty if none.
// Leaves nothing on the stack.
std::string_view GetTopSceneName(lua_State* state, int manager_idx) {
  lua_getfield(state, manager_idx, "stack");
  int len = static_cast<int>(lua_objlen(state, -1));
  if (len == 0) {
    lua_pop(state, 1);
    return {};
  }
  lua_rawgeti(state, -1, len);
  size_t slen = 0;
  const char* data = lua_tolstring(state, -1, &slen);
  lua_pop(state, 2);
  return std::string_view(data, slen);
}

// Calls scene:method(args...) if the method exists. The scene table must
// be at scene_idx (absolute). Args (if any) must already be on the stack
// above scene_idx. nargs is the number of args on the stack.
void CallOptional(lua_State* state, int scene_idx, const char* method,
                  int nargs) {
  lua_getfield(state, scene_idx, method);
  if (lua_isnil(state, -1)) {
    lua_pop(state, 1 + nargs);
    return;
  }
  // Stack: ..., arg1, ..., argN, method
  // We need: method, self, arg1, ..., argN
  // Move method below the args and self.
  lua_insert(state, -(nargs + 1));
  // Stack: ..., method, arg1, ..., argN
  // Now push self after method.
  lua_pushvalue(state, scene_idx);
  lua_insert(state, -(nargs + 1));
  // Stack: ..., method, self, arg1, ..., argN
  if (lua_pcall(state, nargs + 1, 0, 0)) {
    lua_pop(state, 1);
  }
}

// Executes a switch transition.
void DoSwitch(lua_State* state, int manager_idx) {
  lua_getfield(state, manager_idx, "pending");
  if (lua_isnil(state, -1)) {
    lua_pop(state, 1);
    return;
  }

  lua_getfield(state, -1, "name");
  const char* new_name = lua_tostring(state, -1);
  lua_pop(state, 1);

  lua_getfield(state, -1, "args");
  int args_idx = lua_gettop(state);
  int nargs = static_cast<int>(lua_objlen(state, args_idx));

  // Get the old scene name and call leave().
  std::string_view old_name = GetTopSceneName(state, manager_idx);
  if (!old_name.empty()) {
    if (PushSceneByName(state, manager_idx, old_name.data())) {
      CallOptional(state, lua_gettop(state), "leave", 0);
      lua_pop(state, 1);  // Pop old scene.
    }
  }

  // Clear the stack and push the new scene name.
  lua_getfield(state, manager_idx, "stack");
  // Clear the stack table.
  int stack_idx = lua_gettop(state);
  int stack_len = static_cast<int>(lua_objlen(state, stack_idx));
  for (int i = stack_len; i >= 1; --i) {
    lua_pushnil(state);
    lua_rawseti(state, stack_idx, i);
  }
  lua_pushstring(state, new_name);
  lua_rawseti(state, stack_idx, 1);
  lua_pop(state, 1);  // Pop stack table.

  // Init if first time.
  lua_getfield(state, manager_idx, "initialized");
  lua_getfield(state, -1, new_name);
  bool already_init = !lua_isnil(state, -1);
  lua_pop(state, 1);
  if (!already_init) {
    lua_pushboolean(state, 1);
    lua_setfield(state, -2, new_name);
    if (PushSceneByName(state, manager_idx, new_name)) {
      CallOptional(state, lua_gettop(state), "init", 0);
      lua_pop(state, 1);
    }
  }
  lua_pop(state, 1);  // Pop initialized table.

  // Call enter(prev_name, args...).
  if (PushSceneByName(state, manager_idx, new_name)) {
    int scene_idx = lua_gettop(state);
    if (!old_name.empty()) {
      lua_pushlstring(state, old_name.data(), old_name.size());
    } else {
      lua_pushnil(state);
    }
    for (int i = 1; i <= nargs; ++i) {
      lua_rawgeti(state, args_idx, i);
    }
    LOG("Scene switch: ", old_name.empty() ? "(none)" : old_name, " -> ",
        new_name, " (", nargs, " args)");
    CallOptional(state, scene_idx, "enter", 1 + nargs);
    lua_pop(state, 1);  // Pop scene.
  }

  lua_pop(state, 1);  // Pop args table.
  lua_pop(state, 1);  // Pop pending table.

  // Clear pending.
  lua_pushnil(state);
  lua_setfield(state, manager_idx, "pending");
}

// Executes a push transition.
void DoPush(lua_State* state, int manager_idx) {
  lua_getfield(state, manager_idx, "pending");
  if (lua_isnil(state, -1)) {
    lua_pop(state, 1);
    return;
  }

  lua_getfield(state, -1, "name");
  const char* new_name = lua_tostring(state, -1);
  lua_pop(state, 1);

  lua_getfield(state, -1, "args");
  int args_idx = lua_gettop(state);
  int nargs = static_cast<int>(lua_objlen(state, args_idx));

  // Call leave() on current top.
  std::string_view old_name = GetTopSceneName(state, manager_idx);
  if (!old_name.empty()) {
    if (PushSceneByName(state, manager_idx, old_name.data())) {
      CallOptional(state, lua_gettop(state), "leave", 0);
      lua_pop(state, 1);
    }
  }

  // Push new name onto stack.
  lua_getfield(state, manager_idx, "stack");
  int stack_idx = lua_gettop(state);
  int stack_len = static_cast<int>(lua_objlen(state, stack_idx));
  lua_pushstring(state, new_name);
  lua_rawseti(state, stack_idx, stack_len + 1);
  lua_pop(state, 1);

  // Init if first time.
  lua_getfield(state, manager_idx, "initialized");
  lua_getfield(state, -1, new_name);
  bool already_init = !lua_isnil(state, -1);
  lua_pop(state, 1);
  if (!already_init) {
    lua_pushboolean(state, 1);
    lua_setfield(state, -2, new_name);
    if (PushSceneByName(state, manager_idx, new_name)) {
      CallOptional(state, lua_gettop(state), "init", 0);
      lua_pop(state, 1);
    }
  }
  lua_pop(state, 1);

  // Call enter(prev_name, args...).
  if (PushSceneByName(state, manager_idx, new_name)) {
    int scene_idx = lua_gettop(state);
    if (!old_name.empty()) {
      lua_pushlstring(state, old_name.data(), old_name.size());
    } else {
      lua_pushnil(state);
    }
    for (int i = 1; i <= nargs; ++i) {
      lua_rawgeti(state, args_idx, i);
    }
    CallOptional(state, scene_idx, "enter", 1 + nargs);
    lua_pop(state, 1);
  }

  lua_pop(state, 1);  // args
  lua_pop(state, 1);  // pending

  lua_pushnil(state);
  lua_setfield(state, manager_idx, "pending");
}

// Executes a pop transition.
void DoPop(lua_State* state, int manager_idx) {
  lua_getfield(state, manager_idx, "pending");
  if (lua_isnil(state, -1)) {
    lua_pop(state, 1);
    return;
  }

  lua_getfield(state, -1, "args");
  int args_idx = lua_gettop(state);
  int nargs = static_cast<int>(lua_objlen(state, args_idx));

  // Call leave() on current top.
  std::string_view top_name = GetTopSceneName(state, manager_idx);
  if (!top_name.empty()) {
    if (PushSceneByName(state, manager_idx, top_name.data())) {
      CallOptional(state, lua_gettop(state), "leave", 0);
      lua_pop(state, 1);
    }
  }

  // Pop the stack.
  lua_getfield(state, manager_idx, "stack");
  int stack_idx = lua_gettop(state);
  int stack_len = static_cast<int>(lua_objlen(state, stack_idx));
  if (stack_len > 0) {
    lua_pushnil(state);
    lua_rawseti(state, stack_idx, stack_len);
  }
  lua_pop(state, 1);

  // Call resume(args...) on the new top.
  std::string_view new_top = GetTopSceneName(state, manager_idx);
  if (!new_top.empty()) {
    if (PushSceneByName(state, manager_idx, new_top.data())) {
      int scene_idx = lua_gettop(state);
      for (int i = 1; i <= nargs; ++i) {
        lua_rawgeti(state, args_idx, i);
      }
      CallOptional(state, scene_idx, "resume", nargs);
      lua_pop(state, 1);
    }
  }

  lua_pop(state, 1);  // args
  lua_pop(state, 1);  // pending

  lua_pushnil(state);
  lua_setfield(state, manager_idx, "pending");
}

// Stores a pending transition in the manager table.
void SetPending(lua_State* state, const char* type, const char* name,
                int first_arg, int nargs) {
  PushManager(state);
  int mgr = lua_gettop(state);
  lua_newtable(state);
  lua_pushstring(state, type);
  lua_setfield(state, -2, "type");
  if (name) {
    lua_pushstring(state, name);
    lua_setfield(state, -2, "name");
  }
  // Pack varargs into an array.
  lua_newtable(state);
  for (int i = 0; i < nargs; ++i) {
    lua_pushvalue(state, first_arg + i);
    lua_rawseti(state, -2, i + 1);
  }
  lua_setfield(state, -2, "args");
  lua_setfield(state, mgr, "pending");
  lua_pop(state, 1);  // manager
}

const LuaApiFunction kSceneLib[] = {
    {"register",
     "Registers a scene by name",
     {{"name", "unique scene name", "string"},
      {"scene", "scene table with lifecycle methods", "table"}},
     {},
     [](lua_State* state) -> int {
       const char* name = luaL_checkstring(state, 1);
       luaL_checktype(state, 2, LUA_TTABLE);
       PushManager(state);
       lua_getfield(state, -1, "scenes");
       lua_pushvalue(state, 2);
       lua_setfield(state, -2, name);
       lua_pop(state, 2);
       return 0;
     }},
    {"switch",
     "Switches to a named scene (deferred to next frame)",
     {{"name", "scene name to switch to", "string"},
      {"...", "data passed to enter()", "any"}},
     {},
     [](lua_State* state) -> int {
       const char* name = luaL_checkstring(state, 1);
       int nargs = lua_gettop(state) - 1;
       SetPending(state, "switch", name, 2, nargs);
       return 0;
     }},
    {"push",
     "Pushes a scene onto the stack (overlay)",
     {{"name", "scene name to push", "string"},
      {"...", "data passed to enter()", "any"}},
     {},
     [](lua_State* state) -> int {
       const char* name = luaL_checkstring(state, 1);
       int nargs = lua_gettop(state) - 1;
       SetPending(state, "push", name, 2, nargs);
       return 0;
     }},
    {"pop",
     "Pops the top scene off the stack",
     {{"...", "data passed to resume()", "any"}},
     {},
     [](lua_State* state) -> int {
       int nargs = lua_gettop(state);
       SetPending(state, "pop", nullptr, 1, nargs);
       return 0;
     }},
    {"current",
     "Returns the name of the currently active scene",
     {},
     {{"name", "current scene name or nil", "string?"}},
     [](lua_State* state) -> int {
       PushManager(state);
       std::string_view name = GetTopSceneName(state, lua_gettop(state));
       lua_pop(state, 1);
       if (!name.empty()) {
         lua_pushlstring(state, name.data(), name.size());
       } else {
         lua_pushnil(state);
       }
       return 1;
     }},
    {"depth",
     "Returns the number of scenes on the stack",
     {},
     {{"count", "stack depth", "integer"}},
     [](lua_State* state) -> int {
       PushManager(state);
       lua_getfield(state, -1, "stack");
       int len = static_cast<int>(lua_objlen(state, -1));
       lua_pop(state, 2);
       lua_pushinteger(state, len);
       return 1;
     }},
    {"draw_below",
     "Draws the scene below the current one on the stack",
     {},
     {},
     [](lua_State* state) -> int {
       PushManager(state);
       int mgr = lua_gettop(state);
       lua_getfield(state, mgr, "stack");
       int stack_idx = lua_gettop(state);
       int len = static_cast<int>(lua_objlen(state, stack_idx));
       if (len < 2) {
         lua_pop(state, 2);
         return 0;
       }
       lua_rawgeti(state, stack_idx, len - 1);
       const char* below_name = lua_tostring(state, -1);
       lua_pop(state, 1);
       lua_pop(state, 1);  // stack
       if (below_name && PushSceneByName(state, mgr, below_name)) {
         CallOptional(state, lua_gettop(state), "draw", 0);
         lua_pop(state, 1);
       }
       lua_pop(state, 1);  // manager
       return 0;
     }},
};

}  // namespace

void PushActiveScene(lua_State* state) {
  PushManager(state);
  int mgr = lua_gettop(state);
  std::string_view name = GetTopSceneName(state, mgr);
  if (!name.empty() && PushSceneByName(state, mgr, name.data())) {
    lua_remove(state, mgr);  // Remove manager, leave scene.
    return;
  }
  lua_pop(state, 1);              // Pop manager.
  lua_getglobal(state, "_Game");  // Fallback.
}

void ProcessPendingTransition(lua_State* state) {
  PushManager(state);
  int mgr = lua_gettop(state);
  lua_getfield(state, mgr, "pending");
  if (lua_isnil(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_getfield(state, -1, "type");
  const char* type = lua_tostring(state, -1);
  lua_pop(state, 2);  // Pop type and pending.

  if (strcmp(type, "switch") == 0) {
    DoSwitch(state, mgr);
  } else if (strcmp(type, "push") == 0) {
    DoPush(state, mgr);
  } else if (strcmp(type, "pop") == 0) {
    DoPop(state, mgr);
  }
  lua_pop(state, 1);  // Pop manager.
}

void AddSceneLibrary(Lua* lua) { lua->AddLibrary("scene", kSceneLib); }

LuaLibraryDef GetSceneLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"scene", kSceneLib, std::size(kSceneLib)},
  };
  return {kLibs, std::size(kLibs), nullptr, 0};
}

}  // namespace G
