#include "lua_json.h"

#include <cmath>

#include "allocators.h"
#include "json_alc.h"

namespace G {

int LuaPushJsonValue(lua_State* state, yyjson_val* val) {
  switch (yyjson_get_type(val)) {
    case YYJSON_TYPE_NULL:
      lua_pushnil(state);
      break;
    case YYJSON_TYPE_BOOL:
      lua_pushboolean(state, yyjson_get_bool(val));
      break;
    case YYJSON_TYPE_NUM:
      lua_pushnumber(state, yyjson_get_num(val));
      break;
    case YYJSON_TYPE_STR:
      lua_pushlstring(state, yyjson_get_str(val), yyjson_get_len(val));
      break;
    case YYJSON_TYPE_ARR: {
      size_t idx = 0;
      size_t max = 0;
      yyjson_val* item = nullptr;
      lua_createtable(state, yyjson_arr_size(val), 0);
      yyjson_arr_foreach(val, idx, max, item) {
        LuaPushJsonValue(state, item);
        lua_rawseti(state, -2, idx + 1);
      }
      break;
    }
    case YYJSON_TYPE_OBJ: {
      size_t idx = 0;
      size_t max = 0;
      yyjson_val* key = nullptr;
      yyjson_val* item = nullptr;
      lua_createtable(state, 0, yyjson_obj_size(val));
      yyjson_obj_foreach(val, idx, max, key, item) {
        lua_pushlstring(state, yyjson_get_str(key), yyjson_get_len(key));
        LuaPushJsonValue(state, item);
        lua_rawset(state, -3);
      }
      break;
    }
    default:
      lua_pushnil(state);
      break;
  }
  return 1;
}

ErrorOr<yyjson_mut_val*> LuaToJsonValue(lua_State* state, int index,
                                        yyjson_mut_doc* doc) {
  int type = lua_type(state, index);
  switch (type) {
    case LUA_TNIL:
      return yyjson_mut_null(doc);
    case LUA_TBOOLEAN:
      return yyjson_mut_bool(doc, lua_toboolean(state, index));
    case LUA_TNUMBER: {
      double n = lua_tonumber(state, index);
      if (std::isnan(n)) return Error::Message("cannot encode NaN as JSON");
      if (std::isinf(n)) return Error::Message("cannot encode Inf as JSON");
      // Use integer encoding when the value has no fractional part and fits
      // in a 64-bit signed integer, so 42 encodes as "42" not "42.0".
      auto i = static_cast<int64_t>(n);
      if (static_cast<double>(i) == n) return yyjson_mut_sint(doc, i);
      return yyjson_mut_real(doc, n);
    }
    case LUA_TSTRING: {
      size_t len = 0;
      const char* s = lua_tolstring(state, index, &len);
      return yyjson_mut_strncpy(doc, s, len);
    }
    case LUA_TTABLE: {
      // Convert index to absolute position before iterating.
      if (index < 0) index = lua_gettop(state) + index + 1;
      // Decide array vs object: if lua_objlen > 0 and key 1 exists, array.
      size_t len = lua_objlen(state, index);
      if (len > 0) {
        lua_rawgeti(state, index, 1);
        bool has_key_1 = !lua_isnil(state, -1);
        lua_pop(state, 1);
        if (has_key_1) {
          // Encode as array.
          yyjson_mut_val* arr = yyjson_mut_arr(doc);
          for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(state, index, i);
            auto result = LuaToJsonValue(state, -1, doc);
            lua_pop(state, 1);
            if (result.is_error()) return result.release_error();
            yyjson_mut_arr_append(arr, result.release_value());
          }
          return arr;
        }
      }
      // Encode as object.
      yyjson_mut_val* obj = yyjson_mut_obj(doc);
      lua_pushnil(state);
      while (lua_next(state, index) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING) {
          lua_pop(state, 2);
          return Error::Message("JSON object keys must be strings");
        }
        size_t klen = 0;
        const char* k = lua_tolstring(state, -2, &klen);
        yyjson_mut_val* key = yyjson_mut_strncpy(doc, k, klen);
        auto result = LuaToJsonValue(state, -1, doc);
        lua_pop(state, 1);
        if (result.is_error()) return result.release_error();
        yyjson_mut_obj_add(obj, key, result.release_value());
      }
      return obj;
    }
    default:
      break;
  }
  return Error::Message("unsupported Lua type for JSON encoding");
}

namespace {

const struct LuaApiFunction kJsonLib[] = {
    {"decode",
     "Parse a JSON string into a Lua value",
     {{"str", "JSON string to parse", "string"}},
     {{"error", "nil on success, error message on failure", "string"},
      {"result", "Parsed Lua value on success, nil on failure", "any"}},
     [](lua_State* state) {
       size_t len = 0;
       const char* str = luaL_checklstring(state, 1, &len);
       auto* arena = Registry<ArenaAllocator>::Retrieve(state);
       yyjson_read_err err{};
       yyjson_doc* doc = ReadJson(arena, str, len, &err);
       if (doc == nullptr) {
         lua_pushlstring(state, err.msg, err.msg ? strlen(err.msg) : 0);
         lua_pushnil(state);
         return 2;
       }
       lua_pushnil(state);
       LuaPushJsonValue(state, yyjson_doc_get_root(doc));
       return 2;
     }},
    {"encode",
     "Serialize a Lua value to a JSON string",
     {{"value", "Lua value to serialize", "any"}},
     {{"error", "nil on success, error message on failure", "string"},
      {"result", "JSON string on success, nil on failure", "string"}},
     [](lua_State* state) {
       auto* arena = Registry<ArenaAllocator>::Retrieve(state);
       yyjson_alc alc = MakeYyjsonAlc(arena);
       yyjson_mut_doc* doc =
           yyjson_mut_doc_new(&alc);  // mutable doc for encode
       auto result = LuaToJsonValue(state, 1, doc);
       if (result.is_error()) {
         auto msg = result.error().message();
         lua_pushlstring(state, msg.data(), msg.size());
         lua_pushnil(state);
         return 2;
       }
       yyjson_mut_doc_set_root(doc, result.release_value());
       yyjson_write_err werr{};
       size_t json_len = 0;
       char* json = yyjson_mut_write_opts(doc, YYJSON_WRITE_NOFLAG, &alc,
                                          &json_len, &werr);
       if (json == nullptr) {
         lua_pushlstring(state, werr.msg, werr.msg ? strlen(werr.msg) : 0);
         lua_pushnil(state);
         return 2;
       }
       lua_pushnil(state);
       lua_pushlstring(state, json, json_len);
       return 2;
     }},
};

}  // namespace

void AddJsonLibrary(Lua* lua) { lua->AddLibrary("json", kJsonLib); }

LuaLibraryDef GetJsonLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"json", kJsonLib, std::size(kJsonLib)},
  };
  return {kLibs, std::size(kLibs), nullptr, 0};
}

}  // namespace G
