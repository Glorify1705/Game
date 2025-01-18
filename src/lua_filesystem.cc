#include "lua_filesystem.h"

#include "filesystem.h"
#include "lua_bytebuffer.h"

namespace G {
namespace {

PHYSFS_EnumerateCallbackResult LuaListDirectory(void* userdata, const char* dir,
                                                const char* file) {
  auto* state = static_cast<lua_State*>(userdata);
  FixedStringBuffer<kMaxPathLength> buf(dir, dir[0] ? "/" : "", file);
  lua_pushlstring(state, buf.str(), buf.size());
  lua_rawseti(state, -2, lua_objlen(state, -2) + 1);
  return PHYSFS_ENUM_OK;
}

const struct LuaApiFunction kFilesystemLib[] = {
    {"spit",
     "Writes a string to a given file, overwriting all contents",
     {{"name", "Filename to write to"}, {"str", "string to write"}},
     {{"result", "nil on success, a string if there were any errors"}},
     [](lua_State* state) {
       return LuaWriteToFile(state, 2, /*name=*/GetLuaString(state, 1));
     }},
    {"slurp",
     "Reads a whole file into a string",
     {{"name", "Filename to read from"}},
     {{"error", "nil on success, a string if there were any errors"},
      {"contents", "File contents on success, nil in case of errors"}},
     [](lua_State* state) {
       return LuaLoadFileIntoBuffer(state, GetLuaString(state, 1));
     }},
    {"load_json",
     "Loads a Json file from a string.",
     {{"name", "Filename to read from"}},
     {{"error", "nil on success, a string if there were any errors"},
      {"result",
       "Table result of evaluating the Json file, nil if there were any "
       "errors"}},
     [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }},
    {"save_json",
     "Saves a Lua table into a file.",
     {{"name", "Filename to write to from"},
      {"contents", "Table to serialize"}},
     {{"error", "nil on success, a string if there were any errors"}},
     [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }},
    {"list_directory",
     "List all files in a givne directory",
     {{"name", "Directory to list"}},
     {{"files", "A list with all the files in the given directory"}},
     [](lua_State* state) {
       auto* filesystem = Registry<Filesystem>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       lua_newtable(state);
       filesystem->EnumerateDirectory(name, LuaListDirectory, state);
       return 1;
     }},
    {"exists",
     "Returns whether a file exists",
     {{"name", "Path to the potential file to check"}},
     {{"exists", "Whether the file exists or not"}},
     [](lua_State* state) {
       auto* filesystem = Registry<Filesystem>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       lua_pushboolean(state, filesystem->Exists(name));
       return 1;
     }},
};

}  // namespace

int LuaWriteToFile(lua_State* state, int index, std::string_view filename) {
  FixedStringBuffer<kMaxLogLineLength> err;
  auto* filesystem = Registry<Filesystem>::Retrieve(state);
  auto write_to_fs = [&](std::string_view data) {
    LOG("Writing to ", filename);
    if (filesystem->WriteToFile(filename, data, &err)) {
      lua_pushnil(state);
    } else {
      lua_pushlstring(state, err.str(), err.size());
    }
  };
  if (lua_type(state, index) == LUA_TSTRING) {
    std::string_view data = GetLuaString(state, index);
    write_to_fs(data);
  } else if (lua_type(state, index) == LUA_TUSERDATA) {
    auto* buf = AsUserdata<ByteBuffer>(state, index);
    std::string_view data(reinterpret_cast<const char*>(buf->contents),
                          buf->size);
    write_to_fs(data);
  }
  return 1;
}

int LuaLoadFileIntoBuffer(lua_State* state, std::string_view filename) {
  auto* filesystem = Registry<Filesystem>::Retrieve(state);
  FixedStringBuffer<kMaxLogLineLength> err;
  size_t size = 0;
  if (!filesystem->Size(filename, &size, &err)) {
    lua_pushnil(state);
    lua_pushlstring(state, err.str(), err.size());
    return 2;
  }
  auto* buf = static_cast<ByteBuffer*>(
      lua_newuserdata(state, sizeof(ByteBuffer) + size));
  buf->size = size;
  luaL_getmetatable(state, "byte_buffer");
  lua_setmetatable(state, -2);
  if (filesystem->ReadFile(filename, buf->contents, buf->size, &err)) {
    lua_pushnil(state);
  } else {
    lua_pop(state, 1);  // Pop the userdata, it will be GCed.
    lua_pushnil(state);
    lua_pushlstring(state, err.str(), err.size());
  }
  return 2;
}

void AddFilesystemLibrary(Lua* lua) {
  lua->AddLibrary("filesystem", kFilesystemLib);
}

}  // namespace G
