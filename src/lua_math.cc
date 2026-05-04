#include "lua_math.h"

#include <algorithm>
#include <cmath>

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
    {"lerp",
     "Linearly interpolates between a and b",
     {{"a", "start value", "number"},
      {"b", "end value", "number"},
      {"t", "interpolation factor (0-1)", "number"}},
     {{"result", "interpolated value", "number"}},
     [](lua_State* state) {
       const float a = luaL_checknumber(state, 1);
       const float b = luaL_checknumber(state, 2);
       const float t = luaL_checknumber(state, 3);
       lua_pushnumber(state, a + (b - a) * t);
       return 1;
     }},
    {"inverse_lerp",
     "Returns the interpolation factor for x between a and b",
     {{"a", "start value", "number"},
      {"b", "end value", "number"},
      {"x", "value to find factor for", "number"}},
     {{"t", "interpolation factor", "number"}},
     [](lua_State* state) {
       const float a = luaL_checknumber(state, 1);
       const float b = luaL_checknumber(state, 2);
       const float x = luaL_checknumber(state, 3);
       lua_pushnumber(state, (b != a) ? (x - a) / (b - a) : 0.0f);
       return 1;
     }},
    {"remap",
     "Remaps x from range [a1,b1] to range [a2,b2]",
     {{"x", "value to remap", "number"},
      {"a1", "source range start", "number"},
      {"b1", "source range end", "number"},
      {"a2", "target range start", "number"},
      {"b2", "target range end", "number"}},
     {{"result", "remapped value", "number"}},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float a1 = luaL_checknumber(state, 2);
       const float b1 = luaL_checknumber(state, 3);
       const float a2 = luaL_checknumber(state, 4);
       const float b2 = luaL_checknumber(state, 5);
       const float t = (b1 != a1) ? (x - a1) / (b1 - a1) : 0.0f;
       lua_pushnumber(state, a2 + (b2 - a2) * t);
       return 1;
     }},
    {"sign",
     "Returns -1, 0, or 1 depending on the sign of x",
     {{"x", "the value", "number"}},
     {{"result", "-1, 0, or 1", "number"}},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       lua_pushnumber(state, (x > 0.0f) ? 1.0f : (x < 0.0f) ? -1.0f : 0.0f);
       return 1;
     }},
    {"round",
     "Rounds x to the nearest integer",
     {{"x", "the value to round", "number"}},
     {{"result", "rounded value", "number"}},
     [](lua_State* state) {
       lua_pushnumber(state, std::round(luaL_checknumber(state, 1)));
       return 1;
     }},
    {"distance",
     "Euclidean distance between two 2D points",
     {{"x1", "first point x", "number"},
      {"y1", "first point y", "number"},
      {"x2", "second point x", "number"},
      {"y2", "second point y", "number"}},
     {{"dist", "distance", "number"}},
     [](lua_State* state) {
       const float dx = luaL_checknumber(state, 3) - luaL_checknumber(state, 1);
       const float dy = luaL_checknumber(state, 4) - luaL_checknumber(state, 2);
       lua_pushnumber(state, std::sqrt(dx * dx + dy * dy));
       return 1;
     }},
    {"distance2",
     "Squared distance between two 2D points (no sqrt)",
     {{"x1", "first point x", "number"},
      {"y1", "first point y", "number"},
      {"x2", "second point x", "number"},
      {"y2", "second point y", "number"}},
     {{"dist2", "squared distance", "number"}},
     [](lua_State* state) {
       const float dx = luaL_checknumber(state, 3) - luaL_checknumber(state, 1);
       const float dy = luaL_checknumber(state, 4) - luaL_checknumber(state, 2);
       lua_pushnumber(state, dx * dx + dy * dy);
       return 1;
     }},
    {"angle",
     "Angle in radians from point (x1,y1) to (x2,y2)",
     {{"x1", "from x", "number"},
      {"y1", "from y", "number"},
      {"x2", "to x", "number"},
      {"y2", "to y", "number"}},
     {{"radians", "angle in radians", "number"}},
     [](lua_State* state) {
       const float dx = luaL_checknumber(state, 3) - luaL_checknumber(state, 1);
       const float dy = luaL_checknumber(state, 4) - luaL_checknumber(state, 2);
       lua_pushnumber(state, std::atan2(dy, dx));
       return 1;
     }},
    {"direction",
     "Converts an angle and magnitude to x,y components",
     {{"angle", "angle in radians", "number"},
      {"magnitude", "length (default 1)", "number?"}},
     {{"x", "x component", "number"}, {"y", "y component", "number"}},
     [](lua_State* state) {
       const float angle = luaL_checknumber(state, 1);
       const float mag = luaL_optnumber(state, 2, 1.0);
       lua_pushnumber(state, std::cos(angle) * mag);
       lua_pushnumber(state, std::sin(angle) * mag);
       return 2;
     }},
    {"smoothstep",
     "Hermite smoothstep interpolation between edge0 and edge1",
     {{"edge0", "lower edge", "number"},
      {"edge1", "upper edge", "number"},
      {"x", "value to interpolate", "number"}},
     {{"result", "smoothed value in [0,1]", "number"}},
     [](lua_State* state) {
       const float edge0 = luaL_checknumber(state, 1);
       const float edge1 = luaL_checknumber(state, 2);
       const float x = luaL_checknumber(state, 3);
       float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
       lua_pushnumber(state, t * t * (3.0f - 2.0f * t));
       return 1;
     }},
    {"radians",
     "Converts degrees to radians",
     {{"degrees", "angle in degrees", "number"}},
     {{"radians", "angle in radians", "number"}},
     [](lua_State* state) {
       lua_pushnumber(state, luaL_checknumber(state, 1) * (M_PI / 180.0));
       return 1;
     }},
    {"degrees",
     "Converts radians to degrees",
     {{"radians", "angle in radians", "number"}},
     {{"degrees", "angle in degrees", "number"}},
     [](lua_State* state) {
       lua_pushnumber(state, luaL_checknumber(state, 1) * (180.0 / M_PI));
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
    {"length",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       lua_pushnumber(state, v->Length());
       return 1;
     }},
    {"normalized",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       NewUserdata<FVec2>(state, v->Normalized());
       return 1;
     }},
    {"distance",
     [](lua_State* state) {
       auto* a = AsUserdata<FVec2>(state, 1);
       auto* b = AsUserdata<FVec2>(state, 2);
       FVec2 d = *a - *b;
       lua_pushnumber(state, d.Length());
       return 1;
     }},
    {"distance2",
     [](lua_State* state) {
       auto* a = AsUserdata<FVec2>(state, 1);
       auto* b = AsUserdata<FVec2>(state, 2);
       FVec2 d = *a - *b;
       lua_pushnumber(state, d.Length2());
       return 1;
     }},
    {"angle",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       lua_pushnumber(state, std::atan2(v->y, v->x));
       return 1;
     }},
    {"angle_between",
     [](lua_State* state) {
       auto* a = AsUserdata<FVec2>(state, 1);
       auto* b = AsUserdata<FVec2>(state, 2);
       lua_pushnumber(state, std::atan2(b->y - a->y, b->x - a->x));
       return 1;
     }},
    {"lerp",
     [](lua_State* state) {
       auto* a = AsUserdata<FVec2>(state, 1);
       auto* b = AsUserdata<FVec2>(state, 2);
       float t = luaL_checknumber(state, 3);
       NewUserdata<FVec2>(state, a->x + (b->x - a->x) * t,
                          a->y + (b->y - a->y) * t);
       return 1;
     }},
    {"rotate",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       float angle = luaL_checknumber(state, 2);
       float c = std::cos(angle);
       float s = std::sin(angle);
       NewUserdata<FVec2>(state, v->x * c - v->y * s, v->x * s + v->y * c);
       return 1;
     }},
    {"perpendicular",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       NewUserdata<FVec2>(state, -v->y, v->x);
       return 1;
     }},
    {"reflect",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       auto* n = AsUserdata<FVec2>(state, 2);
       float d = 2.0f * v->Dot(*n);
       NewUserdata<FVec2>(state, v->x - d * n->x, v->y - d * n->y);
       return 1;
     }},
    {"project",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       auto* onto = AsUserdata<FVec2>(state, 2);
       float d = v->Dot(*onto) / onto->Length2();
       NewUserdata<FVec2>(state, onto->x * d, onto->y * d);
       return 1;
     }},
    {"unpack",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       lua_pushnumber(state, v->x);
       lua_pushnumber(state, v->y);
       return 2;
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
    {"__unm",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       NewUserdata<FVec2>(state, -(*v));
       return 1;
     }},
    {"__tostring",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       FixedStringBuffer<64> buf;
       buf.Append(*v);
       lua_pushlstring(state, buf.str(), buf.size());
       return 1;
     }},
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FVec2>(state, 1);
       const char* name = luaL_checkstring(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       auto result = shaders->SetUniform(name, *v);
       if (result.is_error()) {
         LUA_ERROR(state, "Could not set uniform '", name,
                   "': ", result.error().message());
       }
       return 0;
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
    {"length",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       lua_pushnumber(state, v->Length());
       return 1;
     }},
    {"normalized",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       NewUserdata<FVec3>(state, v->Normalized());
       return 1;
     }},
    {"lerp",
     [](lua_State* state) {
       auto* a = AsUserdata<FVec3>(state, 1);
       auto* b = AsUserdata<FVec3>(state, 2);
       float t = luaL_checknumber(state, 3);
       NewUserdata<FVec3>(state, a->x + (b->x - a->x) * t,
                          a->y + (b->y - a->y) * t, a->z + (b->z - a->z) * t);
       return 1;
     }},
    {"unpack",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       lua_pushnumber(state, v->x);
       lua_pushnumber(state, v->y);
       lua_pushnumber(state, v->z);
       return 3;
     }},
    {"__tostring",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       FixedStringBuffer<64> buf;
       buf.Append(*v);
       lua_pushlstring(state, buf.str(), buf.size());
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
    {"__unm",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       NewUserdata<FVec3>(state, -(*v));
       return 1;
     }},
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FVec3>(state, 1);
       const char* name = luaL_checkstring(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       auto result = shaders->SetUniform(name, *v);
       if (result.is_error()) {
         LUA_ERROR(state, "Could not set uniform '", name,
                   "': ", result.error().message());
       }
       return 0;
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
    {"length",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       lua_pushnumber(state, v->Length());
       return 1;
     }},
    {"normalized",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       NewUserdata<FVec4>(state, v->Normalized());
       return 1;
     }},
    {"lerp",
     [](lua_State* state) {
       auto* a = AsUserdata<FVec4>(state, 1);
       auto* b = AsUserdata<FVec4>(state, 2);
       float t = luaL_checknumber(state, 3);
       NewUserdata<FVec4>(state, a->x + (b->x - a->x) * t,
                          a->y + (b->y - a->y) * t, a->z + (b->z - a->z) * t,
                          a->w + (b->w - a->w) * t);
       return 1;
     }},
    {"unpack",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       lua_pushnumber(state, v->x);
       lua_pushnumber(state, v->y);
       lua_pushnumber(state, v->z);
       lua_pushnumber(state, v->w);
       return 4;
     }},
    {"__tostring",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       FixedStringBuffer<64> buf;
       buf.Append(*v);
       lua_pushlstring(state, buf.str(), buf.size());
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
    {"__unm",
     [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       NewUserdata<FVec4>(state, -(*v));
       return 1;
     }},
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FVec4>(state, 1);
       const char* name = luaL_checkstring(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       auto result = shaders->SetUniform(name, *v);
       if (result.is_error()) {
         LUA_ERROR(state, "Could not set uniform '", name,
                   "': ", result.error().message());
       }
       return 0;
     }}};

constexpr luaL_Reg kM2x2Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FMat2x2>(state, 1);
       const char* name = luaL_checkstring(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       auto result = shaders->SetUniform(name, *v);
       if (result.is_error()) {
         LUA_ERROR(state, "Could not set uniform '", name,
                   "': ", result.error().message());
       }
       return 0;
     }}};

constexpr luaL_Reg kM3x3Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FMat3x3>(state, 1);
       const char* name = luaL_checkstring(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       auto result = shaders->SetUniform(name, *v);
       if (result.is_error()) {
         LUA_ERROR(state, "Could not set uniform '", name,
                   "': ", result.error().message());
       }
       return 0;
     }}};

constexpr luaL_Reg kM4x4Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = AsUserdata<FMat4x4>(state, 1);
       const char* name = luaL_checkstring(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       auto result = shaders->SetUniform(name, *v);
       if (result.is_error()) {
         LUA_ERROR(state, "Could not set uniform '", name,
                   "': ", result.error().message());
       }
       return 0;
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
    {"length",
     "Length (magnitude) of the vector",
     {},
     {{"result", "length", "number"}}},
    {"normalized",
     "Returns a normalized copy of the vector",
     {},
     {{"result", "normalized vector", "self"}}},
    {"lerp",
     "Linearly interpolates between this vector and another",
     {{"other", "target vector", "self"},
      {"t", "interpolation factor (0-1)", "number"}},
     {{"result", "interpolated vector", "self"}}},
    {"unpack",
     "Returns the vector components as separate numbers",
     {},
     {{"x", "x component", "number"}, {"y", "y component", "number"}}},
    {"send_as_uniform",
     "Sends this value as a shader uniform. Errors if uniform not found.",
     {{"name", "uniform name", "string"}},
     {}},
};

// Vec2-specific methods (not on vec3/vec4).
const LuaUserdataMethod kVec2ExtraMethods[] = {
    {"distance",
     "Distance to another vector",
     {{"other", "the other vector", "vec2"}},
     {{"result", "distance", "number"}}},
    {"distance2",
     "Squared distance to another vector",
     {{"other", "the other vector", "vec2"}},
     {{"result", "squared distance", "number"}}},
    {"angle",
     "Angle of this vector in radians (atan2(y, x))",
     {},
     {{"radians", "angle", "number"}}},
    {"angle_between",
     "Angle from this vector to another",
     {{"other", "the other vector", "vec2"}},
     {{"radians", "angle", "number"}}},
    {"rotate",
     "Returns a copy rotated by the given angle",
     {{"angle", "rotation in radians", "number"}},
     {{"result", "rotated vector", "vec2"}}},
    {"perpendicular",
     "Returns the perpendicular vector (-y, x)",
     {},
     {{"result", "perpendicular vector", "vec2"}}},
    {"reflect",
     "Reflects this vector off a surface normal",
     {{"normal", "surface normal", "vec2"}},
     {{"result", "reflected vector", "vec2"}}},
    {"project",
     "Projects this vector onto another",
     {{"onto", "vector to project onto", "vec2"}},
     {{"result", "projected vector", "vec2"}}},
};

const LuaUserdataOperator kVecOperators[] = {
    {"add", "self", "self"},
    {"sub", "self", "self"},
    {"mul", "number", "self"},
    {"unm", "", "self"},
};

const LuaUserdataMethod kMatMethods[] = {
    {"send_as_uniform",
     "Sends this matrix as a shader uniform. Errors if uniform not found.",
     {{"name", "uniform name", "string"}},
     {}},
};

}  // namespace

void AddMathLibrary(Lua* lua) {
  LOAD_METATABLE(lua, "fvec2", kV2Methods);
  LOAD_METATABLE(lua, "fvec3", kV3Methods);
  LOAD_METATABLE(lua, "fvec4", kV4Methods);
  LOAD_METATABLE(lua, "fmat2x2", kM2x2Methods);
  LOAD_METATABLE(lua, "fmat3x3", kM3x3Methods);
  LOAD_METATABLE(lua, "fmat4x4", kM4x4Methods);
  lua->AddLibrary("math", kMathLib);

  // Set direction constants on the math table.
  lua_State* L = lua->state();
  lua_getglobal(L, "G");
  lua_getfield(L, -1, "math");
  NewUserdata<FVec2>(L, 0, -1);
  lua_setfield(L, -2, "UP");
  NewUserdata<FVec2>(L, 0, 1);
  lua_setfield(L, -2, "DOWN");
  NewUserdata<FVec2>(L, -1, 0);
  lua_setfield(L, -2, "LEFT");
  NewUserdata<FVec2>(L, 1, 0);
  lua_setfield(L, -2, "RIGHT");
  lua_pop(L, 2);

  // Vec2 gets the shared methods plus vec2-specific methods.
  lua->RegisterUserdataType({"fvec2", "vec2", "A 2D floating-point vector",
                             nullptr, 0, kVec2ExtraMethods,
                             std::size(kVec2ExtraMethods), kVecOperators,
                             std::size(kVecOperators)});
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

LuaLibraryDef GetMathLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"math", kMathLib, std::size(kMathLib)},
  };
  static const LuaUserdataType kTypes[] = {
      {"fvec2", "vec2", "A 2D floating-point vector", nullptr, 0,
       kVec2ExtraMethods, std::size(kVec2ExtraMethods), kVecOperators,
       std::size(kVecOperators)},
      {"fvec3", "vec3", "A 3D floating-point vector", nullptr, 0, kVecMethods,
       std::size(kVecMethods), kVecOperators, std::size(kVecOperators)},
      {"fvec4", "vec4", "A 4D floating-point vector", nullptr, 0, kVecMethods,
       std::size(kVecMethods), kVecOperators, std::size(kVecOperators)},
      {"fmat2x2", "mat2x2", "A 2x2 floating-point matrix", nullptr, 0,
       kMatMethods, std::size(kMatMethods)},
      {"fmat3x3", "mat3x3", "A 3x3 floating-point matrix", nullptr, 0,
       kMatMethods, std::size(kMatMethods)},
      {"fmat4x4", "mat4x4", "A 4x4 floating-point matrix", nullptr, 0,
       kMatMethods, std::size(kMatMethods)},
  };
  return {kLibs, std::size(kLibs), kTypes, std::size(kTypes)};
}

}  // namespace G
