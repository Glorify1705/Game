#include "config.h"
#include "logging.h"
#include "lua.h"

namespace G {

int SetWindowInfo(lua_State* state) {
  auto* config =
      static_cast<GameConfig*>(lua_touserdata(state, lua_upvalueindex(1)));
  size_t len;
  const char* keyv = lua_tolstring(state, 2, &len);
  std::string_view key(keyv, len);
  if (key == "width") {
    config->window_width = lua_tointeger(state, 3);
  } else if (key == "height") {
    config->window_height = lua_tointeger(state, 3);
  } else if (key == "msaa_samples") {
    config->msaa_samples = lua_tointeger(state, 3);
  } else if (key == "borderless") {
    config->borderless = lua_toboolean(state, 3);
  } else if (key == "enable_joystick") {
    config->enable_joystick = lua_toboolean(state, 3);
  } else if (key == "title") {
    size_t l;
    const char* k = lua_tolstring(state, 3, &l);
    l = std::min(sizeof(config->window_title) - 1, l);
    std::memcpy(config->window_title, k, l);
    config->window_title[l] = '\0';
  }
  return 0;
}

void LoadConfig(const Assets& assets, GameConfig* config,
                Allocator* /*allocator*/) {
  auto* state = luaL_newstate();
  auto* conf = assets.GetScript("conf.lua");
  if (conf == nullptr) {
    LOG("No config file detected, skipping");
    return;
  }
  std::string_view filename = FlatbufferStringview(conf->name());
  LOG("Loading configuration from ", filename);
  CHECK(luaL_loadbuffer(state,
                        reinterpret_cast<const char*>(conf->contents()->Data()),
                        conf->contents()->size(), filename.data()) == 0,
        "Failed to load ", filename, ": ", luaL_checkstring(state, -1));
  lua_call(state, 0, 0);
  lua_getglobal(state, "Conf");
  CHECK(!lua_isnil(state, -1), "no Conf function defined in ", filename);
  // Set table.
  lua_newtable(state);
  lua_pushstring(state, "window");
  // Set "window" with an index metatable and `config` as upvalue.
  lua_newtable(state);
  // Set table with a metatable.
  lua_newtable(state);
  lua_pushlightuserdata(state, static_cast<void*>(config));
  lua_pushcclosure(state, SetWindowInfo, 1);
  lua_setfield(state, -2, "__newindex");
  lua_setmetatable(state, -2);
  lua_settable(state, -3);
  // Now that table should be at the top.
  CHECK(lua_pcall(state, 1, LUA_MULTRET, 0) == 0,
        "Failed to load script: ", filename, ": ", luaL_checkstring(state, -1));
  lua_close(state);
}

}  // namespace G