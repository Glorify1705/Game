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

const struct LuaApiFunction kMathLib[] = {
    {"clamp",
     "Clamps a value between a minimum and maximum",
     {{"x", "the value to clamp"},
      {"low", "the minimum value"},
      {"high", "the maximum value"}},
     {{"result", "the clamped value"}},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float low = luaL_checknumber(state, 2);
       const float high = luaL_checknumber(state, 3);
       lua_pushnumber(state, std::clamp(x, low, high));
       return 1;
     }},
    {"v2",
     "Creates a 2D vector",
     {{"x", "x component"}, {"y", "y component"}},
     {{"vec", "a new vec2"}},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       NewUserdata<FVec2>(state, x, y);
       return 1;
     }},
    {"v3",
     "Creates a 3D vector",
     {{"x", "x component"}, {"y", "y component"}, {"z", "z component"}},
     {{"vec", "a new vec3"}},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       const float z = luaL_checknumber(state, 3);
       NewUserdata<FVec3>(state, x, y, z);
       return 1;
     }},
    {"v4",
     "Creates a 4D vector",
     {{"x", "x component"},
      {"y", "y component"},
      {"z", "z component"},
      {"w", "w component"}},
     {{"vec", "a new vec4"}},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       const float z = luaL_checknumber(state, 3);
       const float w = luaL_checknumber(state, 4);
       NewUserdata<FVec4>(state, x, y, z, w);
       return 1;
     }},
    {"m2x2",
     "Creates a 2x2 matrix from 4 values in row-major order",
     {{"v1", "value 1"},
      {"v2", "value 2"},
      {"v3", "value 3"},
      {"v4", "value 4"}},
     {{"mat", "a new mat2x2"}},
     [](lua_State* state) {
       std::array<float, FMat2x2::kCardinality> values;
       for (size_t i = 0; i < values.size(); i++) {
         values[i] = luaL_checknumber(state, i + 1);
       }
       NewUserdata<FMat2x2>(state, values.data());
       return 1;
     }},
    {"m3x3",
     "Creates a 3x3 matrix from 9 values in row-major order",
     {{"v1", "value 1"},
      {"v2", "value 2"},
      {"v3", "value 3"},
      {"v4", "value 4"},
      {"v5", "value 5"},
      {"v6", "value 6"},
      {"v7", "value 7"},
      {"v8", "value 8"},
      {"v9", "value 9"}},
     {{"mat", "a new mat3x3"}},
     [](lua_State* state) {
       std::array<float, FMat3x3::kCardinality> values;
       for (size_t i = 0; i < values.size(); i++) {
         values[i] = luaL_checknumber(state, i + 1);
       }
       NewUserdata<FMat3x3>(state, values.data());
       return 1;
     }},
    {"m4x4",
     "Creates a 4x4 matrix from 16 values in row-major order",
     {{"v1", "value 1"},
      {"v2", "value 2"},
      {"v3", "value 3"},
      {"v4", "value 4"},
      {"v5", "value 5"},
      {"v6", "value 6"},
      {"v7", "value 7"},
      {"v8", "value 8"},
      {"v9", "value 9"},
      {"v10", "value 10"},
      {"v11", "value 11"},
      {"v12", "value 12"},
      {"v13", "value 13"},
      {"v14", "value 14"},
      {"v15", "value 15"},
      {"v16", "value 16"}},
     {{"mat", "a new mat4x4"}},
     [](lua_State* state) {
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
