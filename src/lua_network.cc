#include "lua_network.h"

#include "network.h"

namespace G {
namespace {

const struct LuaApiFunction kNetworkLib[] = {
    {"create_server",
     "Creates a server listening on the given port",
     {{"port", "port number to listen on", "integer"},
      {"max_clients", "maximum number of connected clients", "integer"},
      {"channels", "number of channels (default 3)", "integer?"}},
     {{"ok", "true if the server was created", "boolean"}},
     [](lua_State* state) {
       auto* net = Registry<Network>::Retrieve(state);
       int port = luaL_checkinteger(state, 1);
       int max_clients = luaL_checkinteger(state, 2);
       int channels = luaL_optinteger(state, 3, 3);
       auto result = net->CreateServer(port, max_clients, channels);
       if (result.is_error()) {
         return luaL_error(state, "%s", result.error().message());
       }
       return 0;
     }},
    {"create_client",
     "Creates a client host for connecting to a server",
     {{"channels", "number of channels (default 3)", "integer?"}},
     {},
     [](lua_State* state) {
       auto* net = Registry<Network>::Retrieve(state);
       int channels = luaL_optinteger(state, 1, 3);
       auto result = net->CreateClient(channels);
       if (result.is_error()) {
         return luaL_error(state, "%s", result.error().message());
       }
       return 0;
     }},
    {"connect",
     "Connects to a remote server",
     {{"host", "hostname or IP address", "string"},
      {"port", "port number", "integer"}},
     {},
     [](lua_State* state) {
       auto* net = Registry<Network>::Retrieve(state);
       const char* host = luaL_checkstring(state, 1);
       int port = luaL_checkinteger(state, 2);
       auto result = net->Connect(host, port);
       if (result.is_error()) {
         return luaL_error(state, "%s", result.error().message());
       }
       return 0;
     }},
    {"disconnect",
     "Disconnects from the server or disconnects all peers",
     {},
     {},
     [](lua_State* state) {
       auto* net = Registry<Network>::Retrieve(state);
       net->Disconnect();
       return 0;
     }},
    {"send",
     "Sends data to a specific peer",
     {{"peer_id", "peer to send to", "integer"},
      {"data", "binary data to send", "string"},
      {"opts",
       "options table with channel (int) and reliable (bool) fields",
       "table?"}},
     {},
     [](lua_State* state) {
       auto* net = Registry<Network>::Retrieve(state);
       int peer_id = luaL_checkinteger(state, 1);
       size_t len;
       const char* data = luaL_checklstring(state, 2, &len);
       uint8_t channel = 0;
       bool reliable = true;
       if (lua_istable(state, 3)) {
         lua_getfield(state, 3, "channel");
         if (!lua_isnil(state, -1)) channel = lua_tointeger(state, -1);
         lua_pop(state, 1);
         lua_getfield(state, 3, "reliable");
         if (!lua_isnil(state, -1)) reliable = lua_toboolean(state, -1);
         lua_pop(state, 1);
       }
       ByteSlice slice = MakeByteSlice(data, len);
       Reliability r = reliable ? Reliability::kReliable
                                : Reliability::kUnreliable;
       net->Send(peer_id, slice, channel, r);
       return 0;
     }},
    {"broadcast",
     "Broadcasts data to all connected peers",
     {{"data", "binary data to send", "string"},
      {"opts",
       "options table with channel (int) and reliable (bool) fields",
       "table?"}},
     {},
     [](lua_State* state) {
       auto* net = Registry<Network>::Retrieve(state);
       size_t len;
       const char* data = luaL_checklstring(state, 1, &len);
       uint8_t channel = 0;
       bool reliable = true;
       if (lua_istable(state, 2)) {
         lua_getfield(state, 2, "channel");
         if (!lua_isnil(state, -1)) channel = lua_tointeger(state, -1);
         lua_pop(state, 1);
         lua_getfield(state, 2, "reliable");
         if (!lua_isnil(state, -1)) reliable = lua_toboolean(state, -1);
         lua_pop(state, 1);
       }
       ByteSlice slice = MakeByteSlice(data, len);
       Reliability r = reliable ? Reliability::kReliable
                                : Reliability::kUnreliable;
       net->Broadcast(slice, channel, r);
       return 0;
     }},
    {"is_active",
     "Returns true if a network host has been created",
     {},
     {{"active", "whether a host exists", "boolean"}},
     [](lua_State* state) {
       auto* net = Registry<Network>::Retrieve(state);
       lua_pushboolean(state, net->IsActive());
       return 1;
     }},
    {"peer_count",
     "Returns the number of currently connected peers",
     {},
     {{"count", "number of connected peers", "integer"}},
     [](lua_State* state) {
       auto* net = Registry<Network>::Retrieve(state);
       lua_pushinteger(state, net->PeerCount());
       return 1;
     }},
    {"connected_peers",
     "Returns a list of connected peer IDs",
     {},
     {{"peers", "array of peer IDs", "table"}},
     [](lua_State* state) {
       auto* net = Registry<Network>::Retrieve(state);
       lua_newtable(state);
       (void)net;  // TODO: expose connected peer list from Network
       return 1;
     }},
};

}  // namespace

void AddNetworkLibrary(Lua* lua) {
  lua->AddLibrary("network", kNetworkLib);
}

LuaLibraryDef GetNetworkLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"network", kNetworkLib, std::size(kNetworkLib)},
  };
  return {kLibs, std::size(kLibs), nullptr, 0};
}

}  // namespace G
