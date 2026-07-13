#include "lua_save.h"

#include "json_alc.h"
#include "lua_json.h"
#include "save.h"

namespace G {
namespace {

// Serializes the Lua value at `index` to a JSON byte string. The caller
// owns the returned yyjson_mut_doc (freed when the arena is reset).
// Returns the JSON bytes as a string_view into the arena.
std::string_view LuaValueToJson(lua_State* state, int index,
                                ArenaAllocator* scratch) {
  yyjson_alc alc = MakeYyjsonAlc(scratch);
  yyjson_mut_doc* doc = yyjson_mut_doc_new(&alc);
  auto result = LuaToJsonValue(state, index, doc);
  if (result.is_error()) {
    luaL_error(state, "cannot serialize value: %s",
               result.error().message().data());
    return {};
  }
  yyjson_mut_doc_set_root(doc, result.release_value());
  size_t len = 0;
  const char* json =
      yyjson_mut_write_opts(doc, YYJSON_WRITE_NOFLAG, &alc, &len, nullptr);
  return {json, len};
}

// Deserializes a JSON blob and pushes the equivalent Lua value.
void JsonToLuaValue(lua_State* state, ByteSlice blob, ArenaAllocator* scratch) {
  if (blob.size() == 0) {
    lua_pushnil(state);
    return;
  }
  yyjson_read_err err{};
  yyjson_doc* doc = ReadJson(
      scratch,
      std::string_view(reinterpret_cast<const char*>(blob.data()), blob.size()),
      &err);
  if (doc == nullptr) {
    lua_pushnil(state);
    return;
  }
  LuaPushJsonValue(state, yyjson_doc_get_root(doc));
}

int SaveSet(lua_State* state) {
  auto* save = Registry<Save>::Retrieve(state);
  std::string_view ns = GetLuaString(state, 1);
  std::string_view key = GetLuaString(state, 2);
  auto* allocator = Registry<Lua>::Retrieve(state)->allocator();
  ArenaAllocator scratch(allocator, Kilobytes(64));
  std::string_view json = LuaValueToJson(state, 3, &scratch);
  ByteSlice blob = MakeByteSlice(json.data(), json.size());
  auto result = save->Set(ns, key, blob);
  if (result.is_error()) {
    return luaL_error(state, "save.set failed: %s",
                      result.error().message().data());
  }
  return 0;
}

int SaveGet(lua_State* state) {
  auto* save = Registry<Save>::Retrieve(state);
  std::string_view ns = GetLuaString(state, 1);
  std::string_view key = GetLuaString(state, 2);
  auto blob_result = save->Get(ns, key);
  if (blob_result.is_error()) {
    return luaL_error(state, "save.get failed: %s",
                      blob_result.error().message().data());
  }
  ByteSlice blob = blob_result.release_value();
  if (blob.size() == 0) {
    lua_pushnil(state);
    return 1;
  }
  auto* allocator = Registry<Lua>::Retrieve(state)->allocator();
  ArenaAllocator scratch(allocator, Kilobytes(64));
  JsonToLuaValue(state, blob, &scratch);
  return 1;
}

int SaveHas(lua_State* state) {
  auto* save = Registry<Save>::Retrieve(state);
  std::string_view ns = GetLuaString(state, 1);
  std::string_view key = GetLuaString(state, 2);
  auto result = save->Has(ns, key);
  if (result.is_error()) return luaL_error(state, "%s", result.error().message());
  lua_pushboolean(state, result.value());
  return 1;
}

int SaveDelete(lua_State* state) {
  auto* save = Registry<Save>::Retrieve(state);
  std::string_view ns = GetLuaString(state, 1);
  std::string_view key = GetLuaString(state, 2);
  auto result = save->Delete(ns, key);
  if (result.is_error()) {
    return luaL_error(state, "save.delete failed");
  }
  return 0;
}

int SaveList(lua_State* state) {
  auto* save = Registry<Save>::Retrieve(state);
  std::string_view ns = GetLuaString(state, 1);
  auto* allocator = Registry<Lua>::Retrieve(state)->allocator();

  lua_newtable(state);
  int table_idx = lua_gettop(state);

  struct Ctx {
    lua_State* state;
    Allocator* allocator;
    int table_idx;
  };
  Ctx ctx = {state, allocator, table_idx};

  auto result = save->List(
      ns,
      [](std::string_view key, ByteSlice value, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        lua_pushlstring(c->state, key.data(), key.size());
        ArenaAllocator scratch(c->allocator, Kilobytes(64));
        JsonToLuaValue(c->state, value, &scratch);
        lua_rawset(c->state, c->table_idx);
      },
      &ctx);
  if (result.is_error()) {
    return luaL_error(state, "save.list failed");
  }
  return 1;
}

int SaveKeys(lua_State* state) {
  auto* save = Registry<Save>::Retrieve(state);
  std::string_view ns = GetLuaString(state, 1);

  lua_newtable(state);
  int table_idx = lua_gettop(state);
  int n = 0;

  struct Ctx {
    lua_State* state;
    int table_idx;
    int* n;
  };
  Ctx ctx = {state, table_idx, &n};

  auto result = save->List(
      ns,
      [](std::string_view key, ByteSlice, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        lua_pushlstring(c->state, key.data(), key.size());
        lua_rawseti(c->state, c->table_idx, ++(*c->n));
      },
      &ctx);
  if (result.is_error()) {
    return luaL_error(state, "save.keys failed");
  }
  return 1;
}

int SaveClear(lua_State* state) {
  auto* save = Registry<Save>::Retrieve(state);
  std::string_view ns = GetLuaString(state, 1);
  auto result = save->Clear(ns);
  if (result.is_error()) {
    return luaL_error(state, "save.clear failed");
  }
  return 0;
}

int SaveNamespaces(lua_State* state) {
  auto* save = Registry<Save>::Retrieve(state);
  lua_newtable(state);
  int table_idx = lua_gettop(state);
  int n = 0;

  struct Ctx {
    lua_State* state;
    int table_idx;
    int* n;
  };
  Ctx ctx = {state, table_idx, &n};

  auto result = save->Namespaces(
      [](std::string_view name, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        lua_pushlstring(c->state, name.data(), name.size());
        lua_rawseti(c->state, c->table_idx, ++(*c->n));
      },
      &ctx);
  if (result.is_error()) {
    return luaL_error(state, "save.namespaces failed");
  }
  return 1;
}

int SaveFlush(lua_State* state) {
  auto* save = Registry<Save>::Retrieve(state);
  auto result = save->Flush();
  if (result.is_error()) {
    return luaL_error(state, "save.flush failed");
  }
  return 0;
}

const struct LuaApiFunction kSaveLib[] = {
    {"set",
     "Stores a value in the save database",
     {{"namespace", "Key namespace (e.g. \"save\", \"settings\")", "string"},
      {"key", "Key name", "string"},
      {"value", "Value to store (nil, bool, number, string, or table)", "any"}},
     {},
     SaveSet},
    {"get",
     "Retrieves a value from the save database",
     {{"namespace", "Key namespace", "string"}, {"key", "Key name", "string"}},
     {{"value", "The stored value, or nil if not found", "any"}},
     SaveGet},
    {"has",
     "Checks if a key exists in the save database",
     {{"namespace", "Key namespace", "string"}, {"key", "Key name", "string"}},
     {{"exists", "True if the key exists", "boolean"}},
     SaveHas},
    {"delete",
     "Deletes a key from the save database",
     {{"namespace", "Key namespace", "string"}, {"key", "Key name", "string"}},
     {},
     SaveDelete},
    {"list",
     "Returns all key-value pairs in a namespace as a table",
     {{"namespace", "Key namespace", "string"}},
     {{"entries", "Table of {key = value, ...}", "table"}},
     SaveList},
    {"keys",
     "Returns all keys in a namespace as an array",
     {{"namespace", "Key namespace", "string"}},
     {{"keys", "Array of key names", "table"}},
     SaveKeys},
    {"clear",
     "Deletes all keys in a namespace",
     {{"namespace", "Key namespace", "string"}},
     {},
     SaveClear},
    {"namespaces",
     "Returns all namespace names as an array",
     {},
     {{"names", "Array of namespace names", "table"}},
     SaveNamespaces},
    {"flush",
     "Checkpoints the WAL to the main database file",
     {},
     {},
     SaveFlush},
};

}  // namespace

void AddSaveLibrary(Lua* lua) { lua->AddLibrary("save", kSaveLib); }

LuaLibraryDef GetSaveLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"save", kSaveLib, std::size(kSaveLib)},
  };
  return {kLibs, std::size(kLibs), nullptr, 0};
}

}  // namespace G
