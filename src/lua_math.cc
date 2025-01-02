#include "lua_math.h"

#include <algorithm>

#include "math.h"
#include "shaders.h"
#include "vec.h"

namespace G {
namespace {

template <typename T>
T FromLuaTable(lua_State* state, int index) {
  T result;
  if (!lua_istable(state, index)) {
    LUA_ERROR(state, "Not a table");
    return result;
  }
  for (size_t i = 0; i < T::kCardinality; ++i) {
    lua_rawgeti(state, index, i + 1);
    result.v[i] = luaL_checknumber(state, -1);
    lua_pop(state, 1);
  }
  return result;
}

template <typename T>
T FromLuaMatrix(lua_State* state, int index) {
  T result;
  if (!lua_istable(state, index)) {
    LUA_ERROR(state, "Not a table");
  }
  for (size_t i = 0; i < T::kDimension; ++i) {
    lua_rawgeti(state, index, i + 1);
    if (!lua_istable(state, index)) {
      LUA_ERROR(state, "Not a table");
      return result;
    }
    for (size_t j = 0; j < T::kDimension; ++j) {
      lua_rawgeti(state, -1, j + 1);
      result.v[i] = luaL_checknumber(state, -1);
      lua_pop(state, 1);
    }
    lua_pop(state, 1);
  }
  return result;
}

const struct luaL_Reg kMathLib[] = {
    {"clamp",
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float low = luaL_checknumber(state, 2);
       const float high = luaL_checknumber(state, 3);
       lua_pushnumber(state, std::clamp(x, low, high));
       return 1;
     }},
    {"v2",
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       NewUserdata<FVec2>(state, x, y);
       return 1;
     }},
    {"v3",
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       const float z = luaL_checknumber(state, 3);
       NewUserdata<FVec3>(state, x, y, z);
       return 1;
     }},
    {"v4",
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       const float z = luaL_checknumber(state, 3);
       const float w = luaL_checknumber(state, 4);
       NewUserdata<FVec4>(state, x, y, z, w);
       return 1;
     }},
    {"m2x2",
     [](lua_State* state) {
       std::array<float, FMat2x2::kCardinality> values;
       for (size_t i = 0; i < values.size(); i++) {
         values[i] = luaL_checknumber(state, i + 1);
       }
       NewUserdata<FMat2x2>(state, values.data());
       return 1;
     }},
    {"m3x3",
     [](lua_State* state) {
       std::array<float, FMat3x3::kCardinality> values;
       for (size_t i = 0; i < values.size(); i++) {
         values[i] = luaL_checknumber(state, i + 1);
       }
       NewUserdata<FMat3x3>(state, values.data());
       return 1;
     }},
    {"m4x4", [](lua_State* state) {
       std::array<float, FMat4x4::kCardinality> values;
       for (size_t i = 0; i < values.size(); i++) {
         values[i] = luaL_checknumber(state, i + 1);
       }
       NewUserdata<FMat4x4>(state, values.data());
       return 1;
     }}};

constexpr luaL_Reg kV2Methods[] = {
    {"dot",
     [](lua_State* state) {
       auto* a = AsUserdata<FVec2>(state, 1);
       auto* b = AsUserdata<FVec2>(state, 2);
       lua_pushnumber(state, a->Dot(*b));
       return 1;
     }},
    {"len2",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       lua_pushnumber(state, v->Length2());
       return 1;
     }},
    {"normalized",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       NewUserdata<FVec2>(state, v->Normalized());
       return 1;
     }},
    {"__add",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       auto* w = AsUserdata<FVec2>(state, 2);
       NewUserdata<FVec2>(state, *v + *w);
       return 1;
     }},
    {"__sub",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       auto* w = AsUserdata<FVec2>(state, 2);
       NewUserdata<FVec2>(state, *v - *w);
       return 1;
     }},
    {"__mul",
     [](lua_State* state) {
       if (lua_type(state, 1) == LUA_TNUMBER) {
         auto* v = AsUserdata<FVec2>(state, 2);
         const float w = luaL_checknumber(state, 1);
         NewUserdata<FVec2>(state, (*v) * w);
       } else {
         auto* v = AsUserdata<FVec2>(state, 1);
         const float w = luaL_checknumber(state, 2);
         NewUserdata<FVec2>(state, (*v) * w);
       }
       return 1;
     }},
    {"__tostring",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       FixedStringBuffer<32> buf;
       v->DebugString(buf);
       lua_pushlstring(state, buf.str(), buf.size());
       return 1;
     }},
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kV3Methods[] = {
    {"dot",
     [](lua_State* state) {
       auto* a = AsUserdata<FVec3>(state, 1);
       auto* b = AsUserdata<FVec3>(state, 2);
       lua_pushnumber(state, a->Dot(*b));
       return 1;
     }},
    {"len2",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       lua_pushnumber(state, v->Length2());
       return 1;
     }},
    {"__tostring",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       FixedStringBuffer<64> buf;
       v->DebugString(buf);
       lua_pushlstring(state, buf.str(), buf.size());
       return 1;
     }},
    {"normalized",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       NewUserdata<FVec3>(state, v->Normalized());
       return 1;
     }},
    {"__add",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       auto* w = AsUserdata<FVec3>(state, 2);
       NewUserdata<FVec3>(state, *v + *w);
       return 1;
     }},
    {"__sub",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       auto* w = AsUserdata<FVec3>(state, 2);
       NewUserdata<FVec3>(state, *v - *w);
       return 1;
     }},
    {"__mul",
     [](lua_State* state) {
       if (lua_type(state, 1) == LUA_TNUMBER) {
         auto* v = AsUserdata<FVec3>(state, 2);
         const float w = luaL_checknumber(state, 1);
         NewUserdata<FVec3>(state, (*v) * w);
       } else {
         auto* v = AsUserdata<FVec3>(state, 1);
         const float w = luaL_checknumber(state, 2);
         NewUserdata<FVec3>(state, (*v) * w);
       }
       return 1;
     }},
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kV4Methods[] = {
    {"dot",
     [](lua_State* state) {
       auto* a = AsUserdata<FVec4>(state, 1);
       auto* b = AsUserdata<FVec4>(state, 2);
       lua_pushnumber(state, a->Dot(*b));
       return 1;
     }},
    {"len2",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       lua_pushnumber(state, v->Length2());
       return 1;
     }},
    {"__tostring",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       FixedStringBuffer<64> buf;
       v->DebugString(buf);
       lua_pushlstring(state, buf.str(), buf.size());
       return 1;
     }},
    {"normalized",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       NewUserdata<FVec4>(state, v->Normalized());
       return 1;
     }},
    {"__add",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       auto* w = AsUserdata<FVec4>(state, 2);
       NewUserdata<FVec4>(state, *v + *w);
       return 1;
     }},
    {"__sub",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       auto* w = AsUserdata<FVec4>(state, 2);
       NewUserdata<FVec4>(state, *v - *w);
       return 1;
     }},
    {"__mul",
     [](lua_State* state) {
       if (lua_type(state, 1) == LUA_TNUMBER) {
         auto* v = AsUserdata<FVec4>(state, 2);
         const float w = luaL_checknumber(state, 1);
         NewUserdata<FVec4>(state, (*v) * w);
       } else {
         auto* v = AsUserdata<FVec4>(state, 1);
         const float w = luaL_checknumber(state, 2);
         NewUserdata<FVec4>(state, (*v) * w);
       }
       return 1;
     }},
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kM2x2Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FMat2x2>(state, 1);
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kM3x3Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FMat3x3>(state, 1);
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kM4x4Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FMat4x4>(state, 1);
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

}  // namespace

void AddMathLibrary(Lua* lua) {
  lua->LoadMetatable("fvec2", kV2Methods);
  lua->LoadMetatable("fvec3", kV3Methods);
  lua->LoadMetatable("fvec4", kV4Methods);
  lua->LoadMetatable("fmat2x2", kM2x2Methods);
  lua->LoadMetatable("fmat3x3", kM3x3Methods);
  lua->LoadMetatable("fmat4x4", kM4x4Methods);
  lua->AddLibrary("math", kMathLib);
}

}  // namespace G
