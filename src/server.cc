// Dedicated headless game server. Runs Lua server scripts with networking
// and protobuf support, without SDL, OpenGL, audio, or rendering.
//
// Usage: server <assets_directory> [--port <port>]

#include <physfs.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <string_view>
#ifndef _WIN32
#include <unistd.h>
#endif

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "libraries/enet.h"

// lua-protobuf public API.
extern "C" {
int luaopen_pb(lua_State* L);
}

namespace {

volatile sig_atomic_t g_running = 1;

void SignalHandler(int) { g_running = 0; }

// Logs a message to stderr with [server] prefix.
void Log(const char* msg) { fprintf(stderr, "[server] %s\n", msg); }

// Reads a PhysFS file and pushes it as a Lua string. Returns true on success.
bool PushFileAsLuaString(lua_State* L, const char* path) {
  PHYSFS_File* f = PHYSFS_openRead(path);
  if (f == nullptr) return false;
  PHYSFS_sint64 len = PHYSFS_fileLength(f);
  // Use Lua's allocator for the temporary buffer via lua_newuserdata.
  char* buf = static_cast<char*>(lua_newuserdata(L, len));
  PHYSFS_readBytes(f, buf, len);
  PHYSFS_close(f);
  lua_pushlstring(L, buf, len);
  lua_remove(L, -2);  // remove the userdata, keep the string
  return true;
}

// Global ENet host pointer, accessed by Lua C functions.
ENetHost* g_host = nullptr;

// G.network.send(peer_id, data, opts)
int LuaNetSend(lua_State* L) {
  int peer_id = luaL_checkinteger(L, 1);
  size_t len;
  const char* data = luaL_checklstring(L, 2, &len);
  uint8_t channel = 0;
  bool reliable = true;
  if (lua_istable(L, 3)) {
    lua_getfield(L, 3, "channel");
    if (!lua_isnil(L, -1)) channel = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 3, "reliable");
    if (!lua_isnil(L, -1)) reliable = lua_toboolean(L, -1);
    lua_pop(L, 1);
  }
  if (g_host == nullptr || peer_id < 0 ||
      static_cast<size_t>(peer_id) >= g_host->peerCount) {
    return 0;
  }
  ENetPacket* packet = enet_packet_create(
      data, len, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  enet_peer_send(&g_host->peers[peer_id], channel, packet);
  return 0;
}

// G.network.broadcast(data, opts)
int LuaNetBroadcast(lua_State* L) {
  size_t len;
  const char* data = luaL_checklstring(L, 1, &len);
  uint8_t channel = 0;
  bool reliable = true;
  if (lua_istable(L, 2)) {
    lua_getfield(L, 2, "channel");
    if (!lua_isnil(L, -1)) channel = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "reliable");
    if (!lua_isnil(L, -1)) reliable = lua_toboolean(L, -1);
    lua_pop(L, 1);
  }
  if (g_host == nullptr) return 0;
  ENetPacket* packet = enet_packet_create(
      data, len, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  enet_host_broadcast(g_host, channel, packet);
  return 0;
}

// G.network.peer_count()
int LuaNetPeerCount(lua_State* L) {
  int count = 0;
  if (g_host != nullptr) {
    for (size_t i = 0; i < g_host->peerCount; ++i) {
      if (g_host->peers[i].state == ENET_PEER_STATE_CONNECTED) ++count;
    }
  }
  lua_pushinteger(L, count);
  return 1;
}

// G.data.encode(typename, table) — delegates to pb.encode
int LuaDataEncode(lua_State* L) {
  lua_getglobal(L, "pb");
  lua_getfield(L, -1, "encode");
  lua_remove(L, -2);
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_call(L, 2, 2);
  if (lua_isnil(L, -2)) {
    return luaL_error(L, "encode failed: %s", lua_tostring(L, -1));
  }
  lua_pop(L, 1);
  return 1;
}

// G.data.decode(typename, bytes) — delegates to pb.decode
int LuaDataDecode(lua_State* L) {
  lua_getglobal(L, "pb");
  lua_getfield(L, -1, "decode");
  lua_remove(L, -2);
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_call(L, 2, 2);
  if (lua_isnil(L, -2)) {
    return luaL_error(L, "decode failed: %s", lua_tostring(L, -1));
  }
  lua_pop(L, 1);
  return 1;
}

// Error function for lua_pcall: appends a traceback.
int LuaTraceback(lua_State* L) {
  const char* msg = lua_tostring(L, 1);
  luaL_traceback(L, L, msg, 1);
  return 1;
}

// Calls _Server:<method>(args...) if it exists. Returns false on error.
bool CallServerMethod(lua_State* L, const char* method, int traceback_idx,
                      int nargs) {
  lua_getglobal(L, "_Server");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1 + nargs);
    return true;
  }
  lua_getfield(L, -1, method);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2 + nargs);
    return true;
  }
  // Rearrange stack: func, self, args...
  lua_insert(L, -(nargs + 2));  // move func before self
  lua_insert(L, -(nargs + 1));  // move _Server (self) after func
  if (lua_pcall(L, 1 + nargs, 0, traceback_idx) != 0) {
    fprintf(stderr, "[server] Error in _Server:%s: %s\n", method,
            lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }
  return true;
}

struct ServerConfig {
  const char* assets_dir = nullptr;
  uint16_t port = 7777;
  int tick_rate = 20;
};

bool ParseArgs(int argc, const char* argv[], ServerConfig* config) {
  if (argc < 2) {
    fprintf(stderr, "Usage: server <assets_directory> [--port <port>]\n");
    return false;
  }
  config->assets_dir = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--port" && i + 1 < argc) {
      config->port = static_cast<uint16_t>(atoi(argv[++i]));
    } else if (arg == "--tick-rate" && i + 1 < argc) {
      config->tick_rate = atoi(argv[++i]);
    }
  }
  return true;
}

}  // namespace

int main(int argc, const char* argv[]) {
  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);

  ServerConfig config;
  if (!ParseArgs(argc, argv, &config)) return 1;

  // Initialize PhysFS for asset loading.
  if (!PHYSFS_init(argv[0])) {
    fprintf(stderr, "Failed to init PhysFS\n");
    return 1;
  }
  PHYSFS_mount(config.assets_dir, nullptr, 1);

  // Initialize ENet.
  if (enet_initialize() != 0) {
    fprintf(stderr, "Failed to init ENet\n");
    return 1;
  }

  // Create ENet server host.
  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = config.port;
  ENetHost* host = enet_host_create(&address, /*peerCount=*/32,
                                    /*channelLimit=*/3, 0, 0);
  if (host == nullptr) {
    fprintf(stderr, "Failed to create ENet server on port %d\n", config.port);
    return 1;
  }
  fprintf(stderr, "[server] Listening on port %d (tick rate: %d)\n",
          config.port, config.tick_rate);

  // Create Lua VM.
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);

  // Register pb in package.preload so require("pb") works in protoc.lua.
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "preload");
  lua_pushcfunction(L, luaopen_pb);
  lua_setfield(L, -2, "pb");
  lua_pop(L, 2);

  // Load pb as a global too for direct use.
  luaopen_pb(L);
  lua_setglobal(L, "pb");

  // Set up traceback handler.
  lua_pushcfunction(L, LuaTraceback);
  int traceback_idx = lua_gettop(L);

  // Register G.network with send, broadcast, peer_count.
  g_host = host;
  lua_newtable(L);  // G
  {
    lua_newtable(L);  // G.network
    lua_pushcfunction(L, LuaNetSend);
    lua_setfield(L, -2, "send");
    lua_pushcfunction(L, LuaNetBroadcast);
    lua_setfield(L, -2, "broadcast");
    lua_pushcfunction(L, LuaNetPeerCount);
    lua_setfield(L, -2, "peer_count");
    lua_setfield(L, -2, "network");
  }
  // Register G.data with encode, decode.
  {
    lua_newtable(L);  // G.data
    lua_pushcfunction(L, LuaDataEncode);
    lua_setfield(L, -2, "encode");
    lua_pushcfunction(L, LuaDataDecode);
    lua_setfield(L, -2, "decode");
    lua_setfield(L, -2, "data");
  }
  lua_setglobal(L, "G");

  // Load and compile .proto files from the assets directory.
  // The server uses protoc.lua at startup (same as the packer does at
  // pack time). This avoids needing the asset DB for the server binary.
  {
#include "protoc_lua.h"
    if (luaL_loadbuffer(L, kProtocLua, kProtocLuaLen, "@protoc.lua") == 0 &&
        lua_pcall(L, 0, 1, 0) == 0) {
      // protoc module on stack. Load messages.proto if it exists.
      if (PushFileAsLuaString(L, "messages.proto")) {
        // Parser:load(s) compiles and loads into pb in one call.
        lua_getfield(L, -2, "load");
        lua_pushvalue(L, -3);  // self (protoc module)
        lua_pushvalue(L, -3);  // proto text string
        lua_remove(L, -4);     // remove the proto string from below
        if (lua_pcall(L, 2, 1, 0) != 0) {
          fprintf(stderr, "[server] Failed to compile messages.proto: %s\n",
                  lua_tostring(L, -1));
          lua_pop(L, 1);
        } else {
          lua_pop(L, 1);  // pop result
          Log("Loaded proto schema messages.proto");
        }
      }
      lua_pop(L, 1);  // pop protoc module
    } else {
      fprintf(stderr, "[server] Failed to load protoc.lua: %s\n",
              lua_tostring(L, -1));
      lua_pop(L, 1);
    }
  }

  // Load server.lua from assets.
  if (!PushFileAsLuaString(L, "server.lua")) {
    fprintf(stderr, "Failed to open server.lua from %s\n", config.assets_dir);
    return 1;
  }
  {
    size_t script_len;
    const char* script = lua_tolstring(L, -1, &script_len);
    if (luaL_loadbuffer(L, script, script_len, "@server.lua") != 0) {
      fprintf(stderr, "[server] Failed to load server.lua: %s\n",
              lua_tostring(L, -1));
      return 1;
    }
    lua_remove(L, -2);  // remove the source string, keep the chunk
  }
  if (lua_pcall(L, 0, 0, traceback_idx) != 0) {
    fprintf(stderr, "[server] Failed to run server.lua: %s\n",
            lua_tostring(L, -1));
    return 1;
  }

  // Call _Server:init()
  CallServerMethod(L, "init", traceback_idx, 0);
  Log("Server initialized");

  // Main tick loop.
  const int tick_ms = 1000 / config.tick_rate;
  while (g_running) {
    // Poll ENet events.
    ENetEvent event;
    while (enet_host_service(host, &event, 0) > 0) {
      uint32_t peer_id =
          static_cast<uint32_t>(event.peer - host->peers);
      switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
          lua_pushinteger(L, peer_id);
          CallServerMethod(L, "on_connect", traceback_idx, 1);
          break;
        case ENET_EVENT_TYPE_DISCONNECT:
          lua_pushinteger(L, peer_id);
          CallServerMethod(L, "on_disconnect", traceback_idx, 1);
          break;
        case ENET_EVENT_TYPE_RECEIVE:
          lua_pushinteger(L, peer_id);
          lua_pushlstring(L, reinterpret_cast<const char*>(event.packet->data),
                          event.packet->dataLength);
          lua_pushinteger(L, event.channelID);
          CallServerMethod(L, "on_receive", traceback_idx, 3);
          enet_packet_destroy(event.packet);
          break;
        default:
          break;
      }
    }

    // Push tick dt and call _Server:tick(dt)
    lua_pushnumber(L, tick_ms / 1000.0);
    CallServerMethod(L, "tick", traceback_idx, 1);

    // Store the host pointer in a Lua light userdata so server scripts can
    // call send/broadcast via helper functions.
    // For now, provide G.network.send and G.network.broadcast as closures.
    // (This is done once; subsequent ticks reuse the same G.network table.)

    enet_host_flush(host);

    // Sleep for the remainder of the tick.
    // Use enet_host_service with timeout on the next iteration instead of
    // a separate sleep. For now, use a platform sleep.
#ifdef _WIN32
    Sleep(tick_ms);
#else
    usleep(tick_ms * 1000);
#endif
  }

  Log("Shutting down...");

  // Call _Server:shutdown() if it exists.
  CallServerMethod(L, "shutdown", traceback_idx, 0);

  // Disconnect all peers gracefully.
  for (size_t i = 0; i < host->peerCount; ++i) {
    if (host->peers[i].state == ENET_PEER_STATE_CONNECTED) {
      enet_peer_disconnect(&host->peers[i], 0);
    }
  }
  enet_host_flush(host);

  lua_close(L);
  enet_host_destroy(host);
  enet_deinitialize();
  PHYSFS_deinit();

  Log("Server stopped");
  return 0;
}
