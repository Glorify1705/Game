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
     {{"x", "the value to clamp", "number"},
      {"low", "the minimum value", "number"},
      {"high", "the maximum value", "number"}},
     {{"result", "the clamped value", "number"}},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float low = luaL_checknumber(state, 2);
       const float high = luaL_checknumber(state, 3);
       lua_pushnumber(state, std::clamp(x, low, high));
       return 1;
     }},
    {"v2",
     "Creates a 2D vector",
     {{"x", "x component", "number"}, {"y", "y component", "number"}},
     {{"vec", "a new vec2", "vec2"}},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       NewUserdata<FVec2>(state, x, y);
       return 1;
     }},
    {"v3",
     "Creates a 3D vector",
     {{"x", "x component", "number"},
      {"y", "y component", "number"},
      {"z", "z component", "number"}},
     {{"vec", "a new vec3", "vec3"}},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       const float z = luaL_checknumber(state, 3);
       NewUserdata<FVec3>(state, x, y, z);
       return 1;
     }},
    {"v4",
     "Creates a 4D vector",
     {{"x", "x component", "number"},
      {"y", "y component", "number"},
      {"z", "z component", "number"},
      {"w", "w component", "number"}},
     {{"vec", "a new vec4", "vec4"}},
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
     {{"v1", "value 1", "number"},
      {"v2", "value 2", "number"},
      {"v3", "value 3", "number"},
      {"v4", "value 4", "number"}},
     {{"mat", "a new mat2x2", "mat2x2"}},
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
     {{"v1", "value 1", "number"},
      {"v2", "value 2", "number"},
      {"v3", "value 3", "number"},
      {"v4", "value 4", "number"},
      {"v5", "value 5", "number"},
      {"v6", "value 6", "number"},
      {"v7", "value 7", "number"},
      {"v8", "value 8", "number"},
      {"v9", "value 9", "number"}},
     {{"mat", "a new mat3x3", "mat3x3"}},
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
     {{"v1", "value 1", "number"},
      {"v2", "value 2", "number"},
      {"v3", "value 3", "number"},
      {"v4", "value 4", "number"},
      {"v5", "value 5", "number"},
      {"v6", "value 6", "number"},
      {"v7", "value 7", "number"},
      {"v8", "value 8", "number"},
      {"v9", "value 9", "number"},
      {"v10", "value 10", "number"},
      {"v11", "value 11", "number"},
      {"v12", "value 12", "number"},
      {"v13", "value 13", "number"},
      {"v14", "value 14", "number"},
      {"v15", "value 15", "number"},
      {"v16", "value 16", "number"}},
     {{"mat", "a new mat4x4", "mat4x4"}},
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

// "self" is replaced by the type's own LuaLS alias during stub generation.
const LuaUserdataMethod kVecMethods[] = {
    {"dot",
     "Dot product with another vector",
     {{"other", "the other vector", "self"}},
     {{"result", "dot product", "number"}}},
    {"len2",
     "Squared length of the vector",
     {},
     {{"result", "squared length", "number"}}},
    {"normalized",
     "Returns a normalized copy of the vector",
     {},
     {{"result", "normalized vector", "self"}}},
    {"send_as_uniform",
     "Sends this value as a shader uniform",
     {{"name", "uniform name", "string"}},
     {{"ok", "whether the uniform was set", "boolean"}}},
};

const LuaUserdataOperator kVecOperators[] = {
    {"add", "self", "self"},
    {"sub", "self", "self"},
    {"mul", "number", "self"},
};

const LuaUserdataMethod kMatMethods[] = {
    {"send_as_uniform",
     "Sends this matrix as a shader uniform",
     {{"name", "uniform name", "string"}},
     {{"ok", "whether the uniform was set", "boolean"}}},
};

}  // namespace

void AddMathLibrary(Lua* lua) {
  lua->LoadMetatable("fvec2", kV2Methods);
  lua->LoadMetatable("fvec3", kV3Methods);
  lua->LoadMetatable("fvec4", kV4Methods);
  lua->LoadMetatable("fmat2x2", kM2x2Methods);
  lua->LoadMetatable("fmat3x3", kM3x3Methods);
  lua->LoadMetatable("fmat4x4", kM4x4Methods);
  lua->AddLibrary("math", kMathLib);

  lua->RegisterUserdataType({"fvec2", "vec2", "A 2D floating-point vector",
                             nullptr, 0, kVecMethods, std::size(kVecMethods),
                             kVecOperators, std::size(kVecOperators)});
  lua->RegisterUserdataType({"fvec3", "vec3", "A 3D floating-point vector",
                             nullptr, 0, kVecMethods, std::size(kVecMethods),
                             kVecOperators, std::size(kVecOperators)});
  lua->RegisterUserdataType({"fvec4", "vec4", "A 4D floating-point vector",
                             nullptr, 0, kVecMethods, std::size(kVecMethods),
                             kVecOperators, std::size(kVecOperators)});
  lua->RegisterUserdataType({"fmat2x2", "mat2x2", "A 2x2 floating-point matrix",
                             nullptr, 0, kMatMethods, std::size(kMatMethods)});
  lua->RegisterUserdataType({"fmat3x3", "mat3x3", "A 3x3 floating-point matrix",
                             nullptr, 0, kMatMethods, std::size(kMatMethods)});
  lua->RegisterUserdataType({"fmat4x4", "mat4x4", "A 4x4 floating-point matrix",
                             nullptr, 0, kMatMethods, std::size(kMatMethods)});
}

}  // namespace G
