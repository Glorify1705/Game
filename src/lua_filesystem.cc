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
     {{"name", "Filename to write to", "string"},
      {"str", "string to write", "string"}},
     {{"result", "nil on success, a string if there were any errors",
       "string"}},
     [](lua_State* state) {
       return LuaWriteToFile(state, 2, /*filename=*/GetLuaString(state, 1));
     }},
    {"slurp",
     "Reads a whole file into a string",
     {{"name", "Filename to read from", "string"}},
     {{"error", "nil on success, a string if there were any errors", "string"},
      {"contents", "File contents on success, nil in case of errors",
       "byte_buffer"}},
     [](lua_State* state) {
       return LuaLoadFileIntoBuffer(state, GetLuaString(state, 1));
     }},
    {"list_directory",
     "List all files in a givne directory",
     {{"name", "Directory to list", "string"}},
     {{"files", "A list with all the files in the given directory", "table"}},
     [](lua_State* state) {
       auto* filesystem = Registry<Filesystem>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       lua_newtable(state);
       filesystem->EnumerateDirectory(name, LuaListDirectory, state);
       return 1;
     }},
    {"exists",
     "Returns whether a file exists",
     {{"name", "Path to the potential file to check", "string"}},
     {{"exists", "Whether the file exists or not", "boolean"}},
     [](lua_State* state) {
       auto* filesystem = Registry<Filesystem>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       lua_pushboolean(state, filesystem->Exists(name));
       return 1;
     }},
    {"stat",
     "Get file metadata",
     {{"name", "Path to the file or directory", "string"}},
     {{"error", "nil on success, error message on failure", "string"},
      {"info", "Table with size, type, and modtime fields", "table"}},
     [](lua_State* state) {
       auto* filesystem = Registry<Filesystem>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       auto result = filesystem->Stat(name);
       if (result.is_error()) {
         auto msg = result.error().message();
         lua_pushlstring(state, msg.data(), msg.size());
         lua_pushnil(state);
         return 2;
       }
       auto info = result.release_value();
       lua_pushnil(state);
       lua_createtable(state, 0, 3);
       lua_pushnumber(state, static_cast<lua_Number>(info.size));
       lua_setfield(state, -2, "size");
       lua_pushstring(state, info.type == Filesystem::StatInfo::kFile
                                 ? "file"
                                 : "directory");
       lua_setfield(state, -2, "type");
       lua_pushnumber(state, static_cast<lua_Number>(info.modtime_secs));
       lua_setfield(state, -2, "modtime");
       return 2;
     }},
    {"delete",
     "Delete a file from the write directory",
     {{"name", "Path to the file to delete", "string"}},
     {{"error", "nil on success, error message on failure", "string"}},
     [](lua_State* state) {
       auto* filesystem = Registry<Filesystem>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       auto result = filesystem->Delete(name);
       if (result.is_error()) {
         auto msg = result.error().message();
         lua_pushlstring(state, msg.data(), msg.size());
       } else {
         lua_pushnil(state);
       }
       return 1;
     }},
};

}  // namespace

int LuaWriteToFile(lua_State* state, int index, std::string_view filename) {
  auto* filesystem = Registry<Filesystem>::Retrieve(state);
  auto write_to_fs = [&](std::string_view data) {
    LOG("Writing to ", filename);
    auto result = filesystem->Spit(filename, data);
    if (result.is_error()) {
      auto msg = result.error().message();
      lua_pushlstring(state, msg.data(), msg.size());
    } else {
      lua_pushnil(state);
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
  auto stat_result = filesystem->Stat(filename);
  if (stat_result.is_error()) {
    lua_pushnil(state);
    auto msg = stat_result.error().message();
    lua_pushlstring(state, msg.data(), msg.size());
    return 2;
  }
  size_t size = stat_result.release_value().size;
  auto* buf = static_cast<ByteBuffer*>(
      lua_newuserdata(state, sizeof(ByteBuffer) + size));
  buf->size = size;
  luaL_getmetatable(state, "byte_buffer");
  lua_setmetatable(state, -2);
  auto result = filesystem->Slurp(filename, buf->contents, size);
  if (result.is_error()) {
    lua_pop(state, 1);
    lua_pushnil(state);
    auto msg = result.error().message();
    lua_pushlstring(state, msg.data(), msg.size());
  } else {
    lua_pushnil(state);
  }
  return 2;
}

void AddFilesystemLibrary(Lua* lua) {
  lua->AddLibrary("filesystem", kFilesystemLib);
}

}  // namespace G
