#include "lua_data.h"

#include <lua.h>
#include <lauxlib.h>

// lua-protobuf public API.
extern "C" {
int luaopen_pb(lua_State* L);
}

namespace G {
namespace {

// Thin wrappers that delegate to the pb module already loaded in the registry.
// G.data.encode(typename, table) → string
// G.data.decode(typename, string) → table
// G.data.load_schema(proto_text) → boolean
// G.data.types() → iterator
// G.data.fields(typename) → iterator

const struct LuaApiFunction kDataLib[] = {
    {"encode",
     "Encodes a Lua table to binary protobuf bytes",
     {{"typename", "fully qualified protobuf message name", "string"},
      {"table", "Lua table with message fields", "table"}},
     {{"bytes", "encoded binary protobuf data", "string"}},
     [](lua_State* state) {
       // pb.encode(typename, table)
       lua_getglobal(state, "pb");
       lua_getfield(state, -1, "encode");
       lua_remove(state, -2);  // remove pb table
       lua_pushvalue(state, 1);
       lua_pushvalue(state, 2);
       lua_call(state, 2, 2);  // returns bytes, err
       if (lua_isnil(state, -2)) {
         return luaL_error(state, "encode failed: %s", lua_tostring(state, -1));
       }
       lua_pop(state, 1);  // pop err
       return 1;
     }},
    {"decode",
     "Decodes binary protobuf bytes to a Lua table",
     {{"typename", "fully qualified protobuf message name", "string"},
      {"bytes", "binary protobuf data", "string"}},
     {{"table", "Lua table with decoded message fields", "table"}},
     [](lua_State* state) {
       // pb.decode(typename, bytes)
       lua_getglobal(state, "pb");
       lua_getfield(state, -1, "decode");
       lua_remove(state, -2);
       lua_pushvalue(state, 1);
       lua_pushvalue(state, 2);
       lua_call(state, 2, 2);
       if (lua_isnil(state, -2)) {
         return luaL_error(state, "decode failed: %s", lua_tostring(state, -1));
       }
       lua_pop(state, 1);
       return 1;
     }},
    {"load_schema",
     "Loads a protobuf schema from a .proto text string",
     {{"proto_text", "protobuf schema definition", "string"}},
     {{"ok", "true if schema loaded successfully", "boolean"}},
     [](lua_State* state) {
       // protoc:load(text)
       lua_getglobal(state, "pb");
       lua_getfield(state, -1, "_protoc");
       lua_remove(state, -2);
       if (lua_isnil(state, -1)) {
         return luaL_error(state, "protoc not loaded");
       }
       lua_getfield(state, -1, "load");
       lua_pushvalue(state, -2);  // self (protoc instance)
       lua_pushvalue(state, 1);   // proto text
       int status = lua_pcall(state, 2, 1, 0);
       if (status != 0) {
         return luaL_error(state, "load_schema failed: %s",
                           lua_tostring(state, -1));
       }
       return 1;
     }},
    {"types",
     "Returns an iterator over all registered protobuf message types",
     {},
     {{"iterator", "iterator function", "function"}},
     [](lua_State* state) {
       lua_getglobal(state, "pb");
       lua_getfield(state, -1, "types");
       lua_remove(state, -2);
       lua_call(state, 0, 3);  // returns iter, state, init
       return 3;
     }},
    {"fields",
     "Returns an iterator over fields of a protobuf message type",
     {{"typename", "fully qualified protobuf message name", "string"}},
     {{"iterator", "iterator function", "function"}},
     [](lua_State* state) {
       lua_getglobal(state, "pb");
       lua_getfield(state, -1, "fields");
       lua_remove(state, -2);
       lua_pushvalue(state, 1);
       lua_call(state, 1, 3);
       return 3;
     }},
};

}  // namespace

void AddDataLibrary(Lua* lua) {
  // Open the pb C module as a global 'pb'.
  luaopen_pb(lua->state_);
  lua_setglobal(lua->state_, "pb");

  // Register G.data library.
  lua->AddLibrary("data", kDataLib);
}

LuaLibraryDef GetDataLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"data", kDataLib, std::size(kDataLib)},
  };
  return {kLibs, std::size(kLibs), nullptr, 0};
}

}  // namespace G
