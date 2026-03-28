#include "lua_log.h"

#include <string_view>

#include "logging.h"

namespace G {
namespace {

#ifdef GAME_WITH_ASSERTS

struct ChannelEntry {
  std::string_view name;
  LogChannel channel;
};

constexpr ChannelEntry kChannels[] = {
    {"general", LogChannel::kGeneral}, {"graphics", LogChannel::kGraphics},
    {"physics", LogChannel::kPhysics}, {"audio", LogChannel::kAudio},
    {"input", LogChannel::kInput},     {"assets", LogChannel::kAssets},
    {"lua", LogChannel::kLua},
};

struct LevelEntry {
  std::string_view name;
  LogLevel level;
};

constexpr LevelEntry kLevels[] = {
    {"fatal", LogLevel::kFatal}, {"error", LogLevel::kError},
    {"warn", LogLevel::kWarn},   {"info", LogLevel::kInfo},
    {"debug", LogLevel::kDebug}, {"trace", LogLevel::kTrace},
};

bool ParseLevel(std::string_view name, LogLevel* out) {
  for (const auto& entry : kLevels) {
    if (name == entry.name) {
      *out = entry.level;
      return true;
    }
  }
  return false;
}

// G.log.set_level(channel, level) — sets runtime log level for a channel.
// channel: string name or "*" for all channels.
// level: "fatal", "error", "warn", "info", "debug", or "trace".
int LuaSetLevel(lua_State* state) {
  std::string_view channel_name = luaL_checkstring(state, 1);
  std::string_view level_name = luaL_checkstring(state, 2);
  LogLevel level;
  if (!ParseLevel(level_name, &level)) {
    return luaL_error(state, "unknown log level: %s",
                      luaL_checkstring(state, 2));
  }
  if (channel_name == "*") {
    for (size_t i = 0; i < static_cast<size_t>(LogChannel::kCount); ++i) {
      SetChannelLevel(static_cast<LogChannel>(i), level);
    }
    return 0;
  }
  for (const auto& entry : kChannels) {
    if (channel_name == entry.name) {
      SetChannelLevel(entry.channel, level);
      return 0;
    }
  }
  return luaL_error(state, "unknown log channel: %s",
                    luaL_checkstring(state, 1));
}

// G.log.get_level(channel) — returns the current log level for a channel.
int LuaGetLevel(lua_State* state) {
  std::string_view channel_name = luaL_checkstring(state, 1);
  for (const auto& entry : kChannels) {
    if (channel_name != entry.name) continue;
    LogLevel level = GetChannelLevel(entry.channel);
    for (const auto& l : kLevels) {
      if (l.level == level) {
        lua_pushlstring(state, l.name.data(), l.name.size());
        return 1;
      }
    }
  }
  return luaL_error(state, "unknown log channel: %s",
                    luaL_checkstring(state, 1));
}

constexpr struct luaL_Reg kLogLib[] = {
    {"set_level", LuaSetLevel},
    {"get_level", LuaGetLevel},
    {nullptr, nullptr},
};

#endif  // GAME_WITH_ASSERTS

}  // namespace

void AddLogLibrary(Lua* lua) {
#ifdef GAME_WITH_ASSERTS
  lua->AddLibrary("log", kLogLib);
#else
  (void)lua;
#endif
}

}  // namespace G
