// Dedicated headless game server. Runs Lua server scripts with networking
// and protobuf support, without SDL, OpenGL, audio, or rendering.
//
// Usage: server <assets_directory> [--port <port>]

#include <physfs.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// Simple logging to stderr.
void Log(const char* msg) { fprintf(stderr, "[server] %s\n", msg); }

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
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      config->port = static_cast<uint16_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "--tick-rate") == 0 && i + 1 < argc) {
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

  // Open pb module.
  luaopen_pb(L);
  lua_setglobal(L, "pb");

  // Set up traceback handler.
  lua_pushcfunction(L, LuaTraceback);
  int traceback_idx = lua_gettop(L);

  // Register a minimal G.network table for server scripts.
  lua_newtable(L);  // G
  lua_newtable(L);  // G.network
  // G.network will be populated by Lua helper functions, or we expose
  // the ENet host directly. For now, server scripts use _Server callbacks.
  lua_setfield(L, -2, "network");
  lua_newtable(L);  // G.clock
  lua_setfield(L, -2, "clock");
  lua_setglobal(L, "G");

  // Load server.lua from assets.
  PHYSFS_File* f = PHYSFS_openRead("server.lua");
  if (f == nullptr) {
    fprintf(stderr, "Failed to open server.lua from %s\n", config.assets_dir);
    return 1;
  }
  PHYSFS_sint64 len = PHYSFS_fileLength(f);
  char* script = static_cast<char*>(malloc(len + 1));
  PHYSFS_readBytes(f, script, len);
  script[len] = '\0';
  PHYSFS_close(f);

  if (luaL_loadbuffer(L, script, len, "@server.lua") != 0 ||
      lua_pcall(L, 0, 0, traceback_idx) != 0) {
    fprintf(stderr, "[server] Failed to load server.lua: %s\n",
            lua_tostring(L, -1));
    free(script);
    return 1;
  }
  free(script);

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
