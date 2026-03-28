#include "lua_log.h"

#include "logging.h"

namespace G {
namespace {

#ifdef GAME_WITH_ASSERTS

struct ChannelEntry {
  const char* name;
  LogChannel channel;
};

constexpr ChannelEntry kChannels[] = {
    {"general", LogChannel::kGeneral}, {"graphics", LogChannel::kGraphics},
    {"physics", LogChannel::kPhysics}, {"audio", LogChannel::kAudio},
    {"input", LogChannel::kInput},     {"assets", LogChannel::kAssets},
    {"lua", LogChannel::kLua},
};

struct LevelEntry {
  const char* name;
  LogLevel level;
};

constexpr LevelEntry kLevels[] = {
    {"fatal", LogLevel::kFatal}, {"error", LogLevel::kError},
    {"warn", LogLevel::kWarn},   {"info", LogLevel::kInfo},
    {"debug", LogLevel::kDebug}, {"trace", LogLevel::kTrace},
};

bool ParseLevel(const char* name, LogLevel* out) {
  for (const auto& entry : kLevels) {
    if (strcmp(name, entry.name) == 0) {
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
  const char* channel_name = luaL_checkstring(state, 1);
  const char* level_name = luaL_checkstring(state, 2);
  LogLevel level;
  if (!ParseLevel(level_name, &level)) {
    return luaL_error(state, "unknown log level: %s", level_name);
  }
  if (strcmp(channel_name, "*") == 0) {
    for (size_t i = 0; i < static_cast<size_t>(LogChannel::kCount); ++i) {
      SetChannelLevel(static_cast<LogChannel>(i), level);
    }
    return 0;
  }
  for (const auto& entry : kChannels) {
    if (strcmp(channel_name, entry.name) == 0) {
      SetChannelLevel(entry.channel, level);
      return 0;
    }
  }
  return luaL_error(state, "unknown log channel: %s", channel_name);
}

// G.log.get_level(channel) — returns the current log level for a channel.
int LuaGetLevel(lua_State* state) {
  const char* channel_name = luaL_checkstring(state, 1);
  for (const auto& entry : kChannels) {
    if (strcmp(channel_name, entry.name) == 0) {
      LogLevel level = GetChannelLevel(entry.channel);
      for (const auto& l : kLevels) {
        if (l.level == level) {
          lua_pushstring(state, l.name);
          return 1;
        }
      }
    }
  }
  return luaL_error(state, "unknown log channel: %s", channel_name);
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
