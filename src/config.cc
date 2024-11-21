#include "config.h"

#include "logging.h"
#include "lua.h"

namespace G {

template <size_t size>
void CopyLuaString(lua_State* state, char (&buf)[size]) {
  std::size_t l;
  const char* k = lua_tolstring(state, 3, &l);
  l = std::min(size - 1, l);
  std::memcpy(buf, k, l);
  buf[l] = '\0';
}

void ParseVersionFromString(const char* str, GameConfig* config) {
  // TODO: error handling.
  std::sscanf(str, "%d.%d", &config->version.major, &config->version.minor);
}

int SetWindowInfo(lua_State* state) {
  auto* config =
      static_cast<GameConfig*>(lua_touserdata(state, lua_upvalueindex(1)));
  std::size_t len;
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
  } else if (key == "enable_debug_ui") {
    config->enable_debug_ui = lua_toboolean(state, 3);
  } else if (key == "enable_debug_rendering") {
    config->enable_debug_rendering = lua_toboolean(state, 3);
  } else if (key == "title") {
    CopyLuaString(state, config->window_title);
  } else if (key == "org_name") {
    CopyLuaString(state, config->org_name);
  } else if (key == "app_name") {
    CopyLuaString(state, config->app_name);
  } else if (key == "version") {
    switch (lua_type(state, 3)) {
      case LUA_TSTRING:
        ParseVersionFromString(luaL_checkstring(state, 3), config);
        break;
      case LUA_TTABLE:
        lua_pushliteral(state, "major");
        lua_gettable(state, 3);
        config->version.major = luaL_checknumber(state, -1);
        lua_pop(state, 1);
        lua_pushliteral(state, "minor");
        lua_gettable(state, 3);
        config->version.minor = luaL_checknumber(state, -1);
        lua_pop(state, 1);
        break;
    }
  }
  return 0;
}

void LoadConfig(const DbAssets& assets, GameConfig* config,
                Allocator* /*allocator*/) {
  TIMER("Loading configuration");
  auto* state = luaL_newstate();
  auto* conf = assets.GetScript("conf.lua");
  if (conf == nullptr) {
    LOG("No config file detected, skipping");
    return;
  }
  LOG("Loading configuration from ", conf->name);
  CHECK(luaL_loadbuffer(state, reinterpret_cast<const char*>(conf->contents),
                        conf->size, conf->name.data()) == 0,
        "Failed to load ", conf->name, ": ", luaL_checkstring(state, -1));
  lua_call(state, 0, 0);
  lua_getglobal(state, "Conf");
  CHECK(!lua_isnil(state, -1), "no configuration function defined in ",
        conf->name);
  // Set table.
  lua_newtable(state);
  lua_pushstring(state, "game");
  // Set "game" with an index metatable and `config` as upvalue.
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
        "Failed to load script: ", conf->name, ": ",
        luaL_checkstring(state, -1));
  lua_close(state);
}

}  // namespace G
