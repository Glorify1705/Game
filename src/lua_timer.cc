#include "lua_timer.h"

#include "easing.h"
#include "timer.h"

namespace G {
namespace {

uint32_t TagFromLua(lua_State* state, int index) {
  if (lua_type(state, index) == LUA_TSTRING) {
    size_t len;
    const char* str = lua_tolstring(state, index, &len);
    return TimerSystem::HashTag(std::string_view(str, len));
  }
  return 0;
}

const struct LuaApiFunction kTimerLib[] = {
    {"after",
     "Fires a callback once after a delay",
     {{"delay", "seconds to wait", "number"},
      {"action", "function to call", "function"},
      {"tag?", "optional string tag for cancellation", "string"}},
     {{"tag", "numeric tag for this timer", "number"}},
     [](lua_State* state) {
       auto* timers = Registry<TimerSystem>::Retrieve(state);
       float delay = static_cast<float>(luaL_checknumber(state, 1));
       luaL_checktype(state, 2, LUA_TFUNCTION);
       lua_pushvalue(state, 2);
       int action_ref = luaL_ref(state, LUA_REGISTRYINDEX);
       uint32_t tag;
       if (lua_gettop(state) >= 3 && lua_type(state, 3) == LUA_TSTRING) {
         tag = TagFromLua(state, 3);
         tag = timers->After(delay, action_ref, tag);
       } else {
         tag = timers->After(delay, action_ref);
       }
       lua_pushnumber(state, tag);
       return 1;
     }},

    {"every",
     "Fires a callback repeatedly at an interval",
     {{"delay", "seconds between fires", "number"},
      {"action", "function to call", "function"},
      {"times?", "number of repetitions (0 or nil = infinite)", "number"},
      {"tag?", "optional string tag for cancellation", "string"}},
     {{"tag", "numeric tag for this timer", "number"}},
     [](lua_State* state) {
       auto* timers = Registry<TimerSystem>::Retrieve(state);
       float delay = static_cast<float>(luaL_checknumber(state, 1));
       luaL_checktype(state, 2, LUA_TFUNCTION);
       lua_pushvalue(state, 2);
       int action_ref = luaL_ref(state, LUA_REGISTRYINDEX);
       int32_t times = -1;
       int nargs = lua_gettop(state);
       if (nargs >= 3 && lua_isnumber(state, 3)) {
         times = static_cast<int32_t>(lua_tointeger(state, 3));
         if (times == 0) times = -1;
       }
       uint32_t tag;
       int tag_index = (nargs >= 3 && lua_isnumber(state, 3)) ? 4 : 3;
       if (nargs >= tag_index && lua_type(state, tag_index) == LUA_TSTRING) {
         tag = TagFromLua(state, tag_index);
         tag = timers->Every(delay, action_ref, times, tag);
       } else {
         tag = timers->Every(delay, action_ref, times);
       }
       lua_pushnumber(state, tag);
       return 1;
     }},

    {"during",
     "Calls a function every frame for a duration",
     {{"duration", "seconds to run", "number"},
      {"action", "function(dt, elapsed, fraction) called each frame",
       "function"},
      {"after?", "function called when duration ends", "function"},
      {"tag?", "optional string tag for cancellation", "string"}},
     {{"tag", "numeric tag for this timer", "number"}},
     [](lua_State* state) {
       auto* timers = Registry<TimerSystem>::Retrieve(state);
       float duration = static_cast<float>(luaL_checknumber(state, 1));
       luaL_checktype(state, 2, LUA_TFUNCTION);
       lua_pushvalue(state, 2);
       int action_ref = luaL_ref(state, LUA_REGISTRYINDEX);
       int after_ref = LUA_NOREF;
       int nargs = lua_gettop(state);
       int next_arg = 3;
       if (nargs >= next_arg && lua_isfunction(state, next_arg)) {
         lua_pushvalue(state, next_arg);
         after_ref = luaL_ref(state, LUA_REGISTRYINDEX);
         next_arg++;
       }
       uint32_t tag;
       if (nargs >= next_arg && lua_type(state, next_arg) == LUA_TSTRING) {
         tag = TagFromLua(state, next_arg);
         tag = timers->During(duration, action_ref, after_ref, tag);
       } else {
         tag = timers->During(duration, action_ref, after_ref);
       }
       lua_pushnumber(state, tag);
       return 1;
     }},

    {"tween",
     "Tweens table fields toward target values over a duration",
     {{"duration", "seconds for the tween", "number"},
      {"subject", "table whose fields will be modified", "table"},
      {"target", "table of {key = end_value} pairs", "table"},
      {"easing?", "easing name (default 'linear')", "string"},
      {"after?", "completion callback", "function"},
      {"tag?", "optional string tag", "string"}},
     {{"tag", "numeric tag for this timer", "number"}},
     [](lua_State* state) {
       auto* timers = Registry<TimerSystem>::Retrieve(state);
       float duration = static_cast<float>(luaL_checknumber(state, 1));
       luaL_checktype(state, 2, LUA_TTABLE);
       luaL_checktype(state, 3, LUA_TTABLE);

       EasingType easing = kLinear;
       int after_ref = LUA_NOREF;
       int nargs = lua_gettop(state);
       int next_arg = 4;

       if (nargs >= next_arg && lua_type(state, next_arg) == LUA_TSTRING) {
         size_t len;
         const char* name = lua_tolstring(state, next_arg, &len);
         easing = EasingFromName(std::string_view(name, len));
         next_arg++;
       }
       if (nargs >= next_arg && lua_isfunction(state, next_arg)) {
         lua_pushvalue(state, next_arg);
         after_ref = luaL_ref(state, LUA_REGISTRYINDEX);
         next_arg++;
       }

       // Build the keys array as a flat Lua table: {key1, start1, end1, key2,
       // start2, end2, ...}
       lua_newtable(state);
       int keys_table = lua_gettop(state);
       int entry = 1;

       lua_pushnil(state);
       while (lua_next(state, 3) != 0) {
         // stack: key, value (target end value)
         const char* key = lua_tostring(state, -2);
         float end_val = static_cast<float>(lua_tonumber(state, -1));
         lua_pop(state, 1);  // pop value, keep key for next iteration

         // Read start value from subject
         lua_getfield(state, 2, key);
         float start_val = static_cast<float>(lua_tonumber(state, -1));
         lua_pop(state, 1);

         // Store: key string, start, end
         lua_pushstring(state, key);
         lua_rawseti(state, keys_table, entry++);
         lua_pushnumber(state, start_val);
         lua_rawseti(state, keys_table, entry++);
         lua_pushnumber(state, end_val);
         lua_rawseti(state, keys_table, entry++);
       }

       int keys_ref = luaL_ref(state, LUA_REGISTRYINDEX);

       lua_pushvalue(state, 2);
       int target_ref = luaL_ref(state, LUA_REGISTRYINDEX);

       uint32_t tag;
       if (nargs >= next_arg && lua_type(state, next_arg) == LUA_TSTRING) {
         tag = TagFromLua(state, next_arg);
         tag = timers->Tween(duration, target_ref, keys_ref, easing, after_ref,
                             tag);
       } else {
         tag = timers->Tween(duration, target_ref, keys_ref, easing, after_ref);
       }

       lua_pushnumber(state, tag);
       return 1;
     }},

    {"cooldown",
     "Fires when both delay elapsed AND condition is true",
     {{"delay", "seconds between checks", "number"},
      {"condition", "function returning bool", "function"},
      {"action", "function to fire", "function"},
      {"times?", "repetitions (0 or nil = infinite)", "number"},
      {"tag?", "optional string tag", "string"}},
     {{"tag", "numeric tag for this timer", "number"}},
     [](lua_State* state) {
       auto* timers = Registry<TimerSystem>::Retrieve(state);
       float delay = static_cast<float>(luaL_checknumber(state, 1));
       luaL_checktype(state, 2, LUA_TFUNCTION);
       lua_pushvalue(state, 2);
       int condition_ref = luaL_ref(state, LUA_REGISTRYINDEX);
       luaL_checktype(state, 3, LUA_TFUNCTION);
       lua_pushvalue(state, 3);
       int action_ref = luaL_ref(state, LUA_REGISTRYINDEX);
       int nargs = lua_gettop(state);
       int32_t times = -1;
       int next_arg = 4;
       if (nargs >= next_arg && lua_isnumber(state, next_arg)) {
         times = static_cast<int32_t>(lua_tointeger(state, next_arg));
         if (times == 0) times = -1;
         next_arg++;
       }
       uint32_t tag;
       if (nargs >= next_arg && lua_type(state, next_arg) == LUA_TSTRING) {
         tag = TagFromLua(state, next_arg);
         tag = timers->Cooldown(delay, condition_ref, action_ref, times, tag);
       } else {
         tag = timers->Cooldown(delay, condition_ref, action_ref, times);
       }
       lua_pushnumber(state, tag);
       return 1;
     }},

    {"cancel",
     "Cancels a timer by tag",
     {{"tag", "string tag of the timer to cancel", "string"}},
     {},
     [](lua_State* state) {
       auto* timers = Registry<TimerSystem>::Retrieve(state);
       uint32_t tag = TagFromLua(state, 1);
       if (tag != 0) {
         timers->Cancel(tag);
       }
       return 0;
     }},

    {"cancel_all",
     "Cancels all active timers",
     {},
     {},
     [](lua_State* state) {
       auto* timers = Registry<TimerSystem>::Retrieve(state);
       timers->CancelAll();
       return 0;
     }},

    {"exists",
     "Checks if a timer with the given tag exists",
     {{"tag", "string tag to check", "string"}},
     {{"exists", "true if timer exists", "boolean"}},
     [](lua_State* state) {
       auto* timers = Registry<TimerSystem>::Retrieve(state);
       uint32_t tag = TagFromLua(state, 1);
       lua_pushboolean(state, tag != 0 && timers->Exists(tag));
       return 1;
     }},

    {"set_real_time",
     "Makes a timer ignore time scale (run in real time)",
     {{"tag", "string tag of the timer", "string"},
      {"real_time", "true to ignore time scale", "boolean"}},
     {},
     [](lua_State* state) {
       auto* timers = Registry<TimerSystem>::Retrieve(state);
       uint32_t tag = TagFromLua(state, 1);
       bool real_time = lua_toboolean(state, 2);
       if (tag != 0) {
         timers->SetRealTime(tag, real_time);
       }
       return 0;
     }},
};

}  // namespace

void AddTimerLibrary(Lua* lua) {
  auto* timers = Registry<TimerSystem>::Retrieve(lua->state_);
  timers->SetLuaState(lua->state_);
  lua->AddLibrary("timer", kTimerLib);
}

LuaLibraryDef GetTimerLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"timer", kTimerLib, std::size(kTimerLib)},
  };
  return {kLibs, std::size(kLibs), nullptr, 0};
}

}  // namespace G
