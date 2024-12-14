#include "lua.h"

#include <algorithm>
#include <random>

#include "SDL.h"
#include "clock.h"
#include "console.h"
#include "defer.h"
#include "filesystem.h"
#include "image.h"
#include "input.h"
#include "libraries/pcg_random.h"
#include "physics.h"
#include "renderer.h"
#include "sound.h"

namespace G {
namespace {

#define LUA_ERROR(state, ...)                                                \
  do {                                                                       \
    FixedStringBuffer<kMaxLogLineLength> _luaerror_buffer;                   \
    _luaerror_buffer.Append(Basename(__FILE__), ":", __LINE__,               \
                            "]: ", ##__VA_ARGS__);                           \
    lua_pushlstring(state, _luaerror_buffer.str(), _luaerror_buffer.size()); \
    lua_error(state);                                                        \
    __builtin_unreachable();                                                 \
  } while (0);

Allocator* GetAllocator(lua_State* state) {
  return Registry<Lua>::Retrieve(state)->allocator();
}

std::string_view GetLuaString(lua_State* state, int index) {
  size_t len;
  const char* data = luaL_checklstring(state, index, &len);
  return std::string_view(data, len);
}

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

int Traceback(lua_State* L) {
  if (!lua_isstring(L, 1)) return 1;
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return 1;
  }
  lua_pushvalue(L, 1);
  lua_pushinteger(L, 2);
  lua_call(L, 2, 1);
  return 1;
}

int LoadLuaAsset(lua_State* state, const DbAssets::Script& asset,
                 int traceback_handler = INT_MAX) {
  FixedStringBuffer<kMaxPathLength + 1> buf("@", asset.name);
  if (luaL_loadbuffer(state, reinterpret_cast<const char*>(asset.contents),
                      asset.size, buf.str()) != 0) {
    lua_error(state);
    return 0;
  }
  if (traceback_handler != INT_MAX) {
    if (lua_pcall(state, 0, 1, traceback_handler)) {
      lua_error(state);
      return 0;
    }
  } else {
    lua_call(state, 0, 1);
  }
  return 1;
}

int LoadFennelAsset(lua_State* state, const DbAssets::Script& asset,
                    int traceback_handler = INT_MAX) {
  std::string_view name = asset.name;
  auto* lua = Registry<Lua>::Retrieve(state);
  if (!lua->LoadFromCache(name, state)) {
    LOG("Explicitly compiling ", name);
    TIMER("Compiling ", name);
    // Load fennel module if not present.
    lua_getfield(state, LUA_GLOBALSINDEX, "package");
    lua_getfield(state, -1, "loaded");
    CHECK(lua_istable(state, -1), "Missing loaded table");
    lua_getfield(state, -1, "fennel");
    if (lua_isnil(state, -1)) {
      LOG("Proactively loading Fennel compiler");
      lua_pop(state, 1);
      DbAssets* assets = Registry<DbAssets>::Retrieve(state);
      auto* fennel = assets->GetScript("fennel.lua");
      if (fennel == nullptr) {
        LUA_ERROR(state, "Fennel compiler is absent, cannot load fennel files");
        return 0;
      }
      // Fennel is not loaded. Load it.
      LoadLuaAsset(state, *fennel);
      CHECK(lua_istable(state, -1), "Invalid fennel compilation result");
      lua_pushvalue(state, -1);
      lua_setfield(state, -3, "fennel");
    }
    LOG("Compiling ", name);
    // Run string on the script contents.
    lua_getfield(state, -1, "compileString");
    CHECK(lua_isfunction(state, -1),
          "Invalid fennel compiler has no function 'eval'");
    lua_pushlstring(state, reinterpret_cast<const char*>(asset.contents),
                    asset.size);
    FixedStringBuffer<kMaxPathLength + 1> buf(name);
    lua_newtable(state);
    lua_pushstring(state, "filename");
    lua_pushstring(state, buf.str());
    lua_settable(state, -3);
    if (traceback_handler != INT_MAX) {
      if (lua_pcall(state, 2, 1, traceback_handler)) {
        lua_error(state);
        return 0;
      }
    } else {
      lua_call(state, 2, 1);
    }
    lua->InsertIntoCache(asset.name, state);
  }
  LOG("Executing script ", name);
  std::string_view script = GetLuaString(state, -1);
  FixedStringBuffer<kMaxPathLength + 1> buf("@", asset.name);
  if (luaL_loadbuffer(state, script.data(), script.size(), buf) != 0) {
    lua_error(state);
    return 0;
  }
  if (traceback_handler != INT_MAX) {
    if (lua_pcall(state, 0, 1, traceback_handler)) {
      lua_error(state);
      return 0;
    }
  } else {
    lua_call(state, 0, 1);
  }
  return 1;
}

int PackageLoader(lua_State* state) {
  const char* modname = luaL_checkstring(state, 1);
  FixedStringBuffer<kMaxPathLength> buf(modname, ".lua");
  DbAssets* assets = Registry<DbAssets>::Retrieve(state);
  LOG("Attempting to load package ", modname, " from file ", buf);
  auto* asset = assets->GetScript(buf.piece());
  if (asset != nullptr) {
    return LoadLuaAsset(state, *asset);
  }
  LOG("Could not find file ", buf, " trying with Fennel");
  buf.Clear();
  buf.Append(modname, ".fnl");
  LOG("Attempting to load package ", modname, " from file ", buf);
  asset = assets->GetScript(buf.piece());
  if (asset == nullptr) {
    LUA_ERROR(state, "Could not find asset %s.lua", modname);
    return 0;
  }
  return LoadFennelAsset(state, *asset);
}

PHYSFS_EnumerateCallbackResult LuaListDirectory(void* userdata, const char* dir,
                                                const char* file) {
  auto* state = static_cast<lua_State*>(userdata);
  FixedStringBuffer<kMaxPathLength> buf(dir, dir[0] ? "/" : "", file);
  lua_pushlstring(state, buf.str(), buf.size());
  lua_rawseti(state, -2, lua_objlen(state, -2) + 1);
  return PHYSFS_ENUM_OK;
}

struct ByteBuffer {
  size_t size;
  uint8_t contents[];
};

int LoadFileIntoBuffer(lua_State* state, std::string_view filename) {
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

const struct luaL_Reg kGraphicsLib[] = {
    {"draw_sprite",
     [](lua_State* state) {
       const int parameters = lua_gettop(state);
       std::string_view spritename = GetLuaString(state, 1);
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       float angle = 0;
       if (parameters == 4) angle = luaL_checknumber(state, 4);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Draw(spritename, FVec(x, y), angle);
       return 0;
     }},
    {"draw_rect",
     [](lua_State* state) {
       const int parameters = lua_gettop(state);
       const float x1 = luaL_checknumber(state, 1);
       const float y1 = luaL_checknumber(state, 2);
       const float x2 = luaL_checknumber(state, 3);
       const float y2 = luaL_checknumber(state, 4);
       float angle = 0;
       if (parameters == 5) angle = luaL_checknumber(state, 5);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawRect(FVec(x1, y1), FVec(x2, y2), angle);
       return 0;
     }},
    {"set_color",
     [](lua_State* state) {
       Color color = Color::Zero();
       if (lua_gettop(state) == 1) {
         std::string_view s = GetLuaString(state, 1);
         if (s.empty()) {
           LUA_ERROR(state, "Invalid empty color");
           return 0;
         }
         color = ColorFromTable(s);
       } else {
         auto clamp = [](float f) -> uint8_t {
           return std::clamp(f, 0.0f, 255.0f);
         };
         color.r = clamp(luaL_checknumber(state, 1));
         color.g = clamp(luaL_checknumber(state, 2));
         color.b = clamp(luaL_checknumber(state, 3));
         color.a = clamp(luaL_checknumber(state, 4));
       }
       auto* renderer = Registry<Renderer>::Retrieve(state);
       const Color previous = renderer->SetColor(color);
       lua_newtable(state);
       lua_pushnumber(state, previous.r);
       lua_setfield(state, -2, "r");
       lua_pushnumber(state, previous.g);
       lua_setfield(state, -2, "g");
       lua_pushnumber(state, previous.b);
       lua_setfield(state, -2, "b");
       lua_pushnumber(state, previous.a);
       lua_setfield(state, -2, "a");
       return 0;
     }},
    {"draw_circle",
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       const float radius = luaL_checknumber(state, 3);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawCircle(FVec(x, y), radius);
       return 0;
     }},
    {"draw_triangle",
     [](lua_State* state) {
       const auto p1 =
           FVec(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       const auto p2 =
           FVec(luaL_checknumber(state, 3), luaL_checknumber(state, 4));
       const auto p3 =
           FVec(luaL_checknumber(state, 5), luaL_checknumber(state, 6));
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawTriangle(p1, p2, p3);
       return 0;
     }},
    {"draw_line",
     [](lua_State* state) {
       const auto p1 =
           FVec(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       const auto p2 =
           FVec(luaL_checknumber(state, 4), luaL_checknumber(state, 4));
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawLine(p1, p2);
       return 0;
     }},
    {"draw_lines",
     [](lua_State* state) {
       if (!lua_istable(state, 1)) {
         LUA_ERROR(state, "not a table");
         return 0;
       }
       const size_t n = lua_objlen(state, 1);
       ArenaAllocator scratch(GetAllocator(state), (n + 1) * sizeof(FVec2));
       FixedArray<FVec2> temp(n, &scratch);
       for (size_t i = 1; i <= n; ++i) {
         lua_rawgeti(state, 1, i);
         lua_rawgeti(state, -1, 1);
         float x = luaL_checknumber(state, -1);
         lua_pop(state, 1);
         lua_rawgeti(state, -1, 2);
         float y = luaL_checknumber(state, -1);
         lua_pop(state, 2);
         temp.Push(FVec(x, y));
       }
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawLines(temp.data(), temp.size());
       return 0;
     }},
    {"draw_text",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       std::string_view font = GetLuaString(state, 1);
       const uint32_t font_size = luaL_checkinteger(state, 2);
       std::string_view text;
       switch (lua_type(state, 3)) {
         case LUA_TSTRING:
           text = GetLuaString(state, 3);
           break;
         case LUA_TUSERDATA: {
           auto* buf = reinterpret_cast<ByteBuffer*>(
               luaL_checkudata(state, 3, "byte_buffer"));
           text = std::string_view(reinterpret_cast<const char*>(buf->contents),
                                   buf->size);
         }; break;
         default:
           LUA_ERROR(state, "Argument 3 cannot be printed");
       }
       const float x = luaL_checknumber(state, 4);
       const float y = luaL_checknumber(state, 5);
       renderer->DrawText(font, font_size, text, FVec(x, y));
       return 0;
     }},
    {"text_dimensions",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       std::string_view font = GetLuaString(state, 1);
       const uint32_t font_size = luaL_checkinteger(state, 2);
       std::string_view text = GetLuaString(state, 3);
       const IVec2 result = renderer->TextDimensions(font, font_size, text);
       lua_pushinteger(state, result.x);
       lua_pushinteger(state, result.y);
       return 2;
     }},
    {"push",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Push();
       return 0;
     }},
    {"pop",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Pop();
       return 0;
     }},
    {"rotate",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Rotate(luaL_checknumber(state, 1));
       return 0;
     }},
    {"scale",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Scale(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       return 0;
     }},
    {"translate",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Translate(luaL_checknumber(state, 1),
                           luaL_checknumber(state, 2));
       return 0;
     }},
    {"new_shader",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       std::string_view code = GetLuaString(state, 2);
       const bool compiles = shaders->Compile(
           HasSuffix(name, ".vert") ? DbAssets::ShaderType::kVertex
                                    : DbAssets::ShaderType::kFragment,
           name, code);
       if (!compiles) {
         LUA_ERROR(state, "Could not compile shader ", name, ": ",
                   shaders->LastError());
       }
       return 0;
     }},
    {"attach_shader",
     [](lua_State* state) {
       auto* renderer = Registry<BatchRenderer>::Retrieve(state);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       std::string_view fragment_shader;
       if (lua_gettop(state) == 0) {
         fragment_shader = "post_pass.frag";
       } else {
         fragment_shader = GetLuaString(state, 1);
       }
       std::string_view program_name = fragment_shader;
       if (!ConsumeSuffix(&program_name, ".frag")) {
         LUA_ERROR(state, "Could not switch shader ", program_name,
                   ": not a fragment shader (i.e. "
                   "name does not end in .frag)",
                   program_name);
         return 0;
       }
       if (!shaders->Link(program_name, "pre_pass.vert", fragment_shader)) {
         LUA_ERROR(state, "Could not switch shader ", program_name, ": ",
                   shaders->LastError());
         return 0;
       }
       renderer->SetShaderProgram(program_name);
       shaders->UseProgram(program_name);
       return 0;
     }},
    {"send_uniform",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (lua_isnumber(state, 2)) {
         if (!shaders->SetUniformF(name, luaL_checknumber(state, 2))) {
           LUA_ERROR(state, "Could not set uniform ", name, ": ",
                     shaders->LastError());
         }
       } else {
         if (!lua_getmetatable(state, 2)) {
           LUA_ERROR(state, "Invalid parameter");
         }
         lua_getfield(state, -1, "__name");
         lua_pop(state, 1);
         lua_getfield(state, -1, "send_as_uniform");
         if (!lua_isfunction(state, -1)) {
           LUA_ERROR(state, "Passed parameter has no `send_as_uniform` method");
         }
         lua_pushvalue(state, 2);
         lua_pushvalue(state, 1);
         lua_call(state, 2, LUA_MULTRET);
       }
       return 0;
     }},
    {"new_canvas",
     [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }},
    {"set_canvas",
     [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }},
    {"draw_canvas", [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }}};

const struct luaL_Reg kSystemLib[] = {
    {"quit",
     [](lua_State* state) {
       auto* lua = Registry<Lua>::Retrieve(state);
       lua->HandleQuit();
       lua->Stop();
       return 0;
     }},
    {"operating_system",
     [](lua_State* state) {
       lua_pushstring(state, SDL_GetPlatform());
       return 1;
     }},
    {"cpu_count",
     [](lua_State* state) {
       lua_pushinteger(state, SDL_GetCPUCount());
       return 1;
     }},
    {"set_clipboard",
     [](lua_State* state) {
       const char* str = luaL_checkstring(state, 1);
       SDL_SetClipboardText(str);
       return 0;
     }},
    {"open_url",
     [](lua_State* state) {
       const char* url = luaL_checkstring(state, 1);
       const int result = SDL_OpenURL(url);
       if (result == 0) {
         lua_pushnil(state);
       } else {
         FixedStringBuffer<kMaxLogLineLength> buf("Could not open ", url, ": ",
                                                  SDL_GetError());
         lua_pushstring(state, SDL_GetError());
       }
       return 1;
     }},
    {"cli_arguments",
     [](lua_State* state) {
       auto* lua = Registry<Lua>::Retrieve(state);
       lua_createtable(state, lua->argc(), 0);
       for (size_t i = 0; i < lua->argc(); ++i) {
         std::string_view arg = lua->argv(i);
         lua_pushlstring(state, arg.data(), arg.size());
         lua_rawseti(state, -2, i + 1);
       }
       return 1;
     }},
    {"get_clipboard", [](lua_State* state) {
       char* result = SDL_GetClipboardText();
       const size_t length = strlen(result);
       if (length == 0) {
         LUA_ERROR(state, "Failed to get the clipboard: %s", SDL_GetError());
         return 0;
       }
       lua_pushlstring(state, result, length);
       return 1;
     }}};

template <typename T, typename... Args>
T* NewUserData(const char* metatable, lua_State* state, Args... args) {
  auto* userdata = static_cast<T*>(lua_newuserdata(state, sizeof(T)));
  luaL_getmetatable(state, metatable);
  new (userdata) T(args...);
  lua_setmetatable(state, -2);
  return userdata;
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
       NewUserData<FVec2>("fvec2", state, x, y);
       return 1;
     }},
    {"v3",
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       const float z = luaL_checknumber(state, 3);
       NewUserData<FVec3>("fvec3", state, x, y, z);
       return 1;
     }},
    {"v4",
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       const float z = luaL_checknumber(state, 3);
       const float w = luaL_checknumber(state, 4);
       NewUserData<FVec4>("fvec4", state, x, y, z, w);
       return 1;
     }},
    {"m2x2",
     [](lua_State* state) {
       std::array<float, FMat2x2::kCardinality> values;
       for (size_t i = 0; i < values.size(); i++) {
         values[i] = luaL_checknumber(state, i + 1);
       }
       NewUserData<FMat2x2>("fmat2x2", state, values.data());
       return 1;
     }},
    {"m3x3",
     [](lua_State* state) {
       std::array<float, FMat3x3::kCardinality> values;
       for (size_t i = 0; i < values.size(); i++) {
         values[i] = luaL_checknumber(state, i + 1);
       }
       NewUserData<FMat2x2>("fmat3x3", state, values.data());
       return 1;
     }},
    {"m4x4", [](lua_State* state) {
       std::array<float, FMat4x4::kCardinality> values;
       for (size_t i = 0; i < values.size(); i++) {
         values[i] = luaL_checknumber(state, i + 1);
       }
       NewUserData<FMat2x2>("fmat4x4", state, values.data());
       return 1;
     }}};

constexpr luaL_Reg kV2Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = reinterpret_cast<FVec2*>(luaL_checkudata(state, 1, "fvec2"));
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kV3Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = reinterpret_cast<FVec2*>(luaL_checkudata(state, 1, "fvec3"));
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kV4Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = reinterpret_cast<FVec2*>(luaL_checkudata(state, 1, "fvec4"));
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kM2x2Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = reinterpret_cast<FVec2*>(luaL_checkudata(state, 1, "fmat2x2"));
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kM3x3Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = reinterpret_cast<FVec2*>(luaL_checkudata(state, 1, "fmat3x3"));
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

constexpr luaL_Reg kM4x4Methods[] = {
    {"send_as_uniform", [](lua_State* state) {
       auto* v = reinterpret_cast<FVec2*>(luaL_checkudata(state, 1, "fmat4x4"));
       auto name = GetLuaString(state, 2);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       bool result = shaders->SetUniform(name.data(), *v);
       lua_pushboolean(state, result);
       return 1;
     }}};

struct LogLine {
  FixedStringBuffer<kMaxLogLineLength> file;
  int line;
  FixedStringBuffer<kMaxLogLineLength> log;
};

void FillLogLine(lua_State* state, LogLine* l) {
  const int num_args = lua_gettop(state);
  lua_getglobal(state, "tostring");
  for (int i = 0; i < num_args; ++i) {
    // Call tostring to print the value of the argument.
    lua_pushvalue(state, -1);
    lua_pushvalue(state, i + 1);
    lua_call(state, 1, 1);
    size_t length;
    const char* s = lua_tolstring(state, -1, &length);
    if (s == nullptr) {
      LUA_ERROR(state, "'tostring' did not return string");
      return;
    }
    l->log.Append(std::string_view(s, length));
    if (i + 1 < num_args) l->log.Append(" ");
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
  lua_Debug ar;
  lua_getstack(state, 1, &ar);
  lua_getinfo(state, "nSl", &ar);
  int line = ar.currentline;
  // Filename starts with @ in the Lua source so that tracebacks
  // work properly.
  const char* file = ar.source + 1;
  lua_pop(state, 1);
  l->file.Append(file);
  l->line = line;
}

int LuaLogPrint(lua_State* state) {
  LogLine l;
  FillLogLine(state, &l);
  Log(l.file.str(), l.line, l.log);
  return 0;
}

const struct luaL_Reg kConsoleLib[] = {{"log", LuaLogPrint},
                                       {"crash", [](lua_State* state) {
                                          LogLine l;
                                          FillLogLine(state, &l);
                                          Crash(l.file.str(), l.line, l.log);
                                          return 0;
                                        }}};

const struct luaL_Reg kInputLib[] = {
    {"mouse_position",
     [](lua_State* state) {
       const FVec2 pos = Mouse::GetPosition();
       lua_pushnumber(state, pos.x);
       lua_pushnumber(state, pos.y);
       return 2;
     }},
    {"is_key_down",
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsDown(keyboard->MapKey(c)));
       return 1;
     }},
    {"is_key_released",
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsReleased(keyboard->MapKey(c)));
       return 1;
     }},
    {"is_key_pressed",
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsPressed(keyboard->MapKey(c)));
       return 1;
     }},
    {"mouse_wheel",
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const FVec2 wheel = mouse->GetWheel();
       lua_pushnumber(state, wheel.x);
       lua_pushnumber(state, wheel.y);
       return 2;
     }},
    {"is_mouse_pressed",
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const auto button = luaL_checknumber(state, 1);
       lua_pushboolean(state, mouse->IsPressed(button));
       return 1;
     }},
    {"is_mouse_released",
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const auto button = luaL_checknumber(state, 1);
       lua_pushboolean(state, mouse->IsReleased(button));
       return 1;
     }},
    {"is_mouse_down",
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const auto button = luaL_checknumber(state, 1);
       lua_pushboolean(state, mouse->IsDown(button));
       return 1;
     }},
    {"is_controller_button_pressed",
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushboolean(
           state, controllers->IsPressed(controllers->StrToButton(c),
                                         controllers->active_controller()));
       return 1;
     }},
    {"is_controller_button_down",
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushboolean(state,
                       controllers->IsDown(controllers->StrToButton(c),
                                           controllers->active_controller()));
       return 1;
     }},
    {"is_controller_button_released",
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushboolean(
           state, controllers->IsReleased(controllers->StrToButton(c),
                                          controllers->active_controller()));
       return 1;
     }},
    {"get_controller_axis", [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushnumber(
           state, controllers->AxisPositions(controllers->StrToAxisOrTrigger(c),
                                             controllers->active_controller()));
       return 1;
     }}};

const struct luaL_Reg kSoundLib[] = {
    {"play_music",
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* sound = Registry<Sound>::Retrieve(state);
       int repeat = Sound::kLoop;
       const int num_args = lua_gettop(state);
       if (num_args == 2) repeat = luaL_checknumber(state, 2);
       sound->PlayMusic(name, repeat);
       return 0;
     }},
    {"play_sfx",
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->PlaySoundEffect(name);
       return 0;
     }},
    {"stop",
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->Stop();
       return 0;
     }},
    {"set_music_volume",
     [](lua_State* state) {
       const float volume = luaL_checknumber(state, 1);
       if (volume < 0 || volume > 1) {
         LUA_ERROR(state, "Invalid volume ", volume, " must be in [0, 1)",
                   volume);
         return 0;
       }
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->SetMusicVolume(volume);
       return 0;
     }},
    {"set_sfx_volume", [](lua_State* state) {
       const float volume = luaL_checknumber(state, 1);
       if (volume < 0 || volume > 1) {
         LUA_ERROR(state, "Invalid volume ", volume, " must be in [0, 1)");
         return 0;
       }
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->SetSoundEffectVolume(volume);
       return 0;
     }}};

const struct luaL_Reg kAssetsLib[] = {
    {"sprite",
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* assets = Registry<DbAssets>::Retrieve(state);
       auto* sprite = assets->GetSprite(name);
       if (sprite == nullptr) {
         LUA_ERROR(state, "Could not find a sprite called ", name);
       }
       lua_pushlightuserdata(state, const_cast<DbAssets::Sprite*>(sprite));
       lua_getfield(state, LUA_REGISTRYINDEX, "asset_sprite_ptr");
       lua_setmetatable(state, -2);
       return 1;
     }},
    {"sprite_info",
     [](lua_State* state) {
       const DbAssets::Sprite* ptr = nullptr;
       if (lua_isstring(state, 1)) {
         auto* assets = Registry<DbAssets>::Retrieve(state);
         std::string_view name = GetLuaString(state, 1);
         ptr = assets->GetSprite(name);
       } else {
         ptr = reinterpret_cast<const DbAssets::Sprite*>(
             luaL_checkudata(state, 1, "asset_sprite_ptr"));
       }
       if (ptr == nullptr) {
         LUA_ERROR(state, "Could not find sprite");
       }
       lua_newtable(state);
       lua_pushnumber(state, ptr->width);
       lua_setfield(state, -2, "width");
       lua_pushnumber(state, ptr->height);
       lua_setfield(state, -2, "height");
       return 1;
     }},
    {"list_sprites", [](lua_State* state) {
       auto* assets = Registry<DbAssets>::Retrieve(state);
       lua_newtable(state);
       for (const auto& sprite : assets->GetSprites()) {
         lua_pushlstring(state, sprite.name.data(), sprite.name.size());
         lua_newtable(state);
         lua_pushnumber(state, sprite.width);
         lua_setfield(state, -2, "width");
         lua_pushnumber(state, sprite.height);
         lua_setfield(state, -2, "height");
         lua_pushnumber(state, sprite.x);
         lua_setfield(state, -2, "x");
         lua_pushnumber(state, sprite.y);
         lua_setfield(state, -2, "y");
         const auto* spritesheet = assets->GetSpritesheet(sprite.spritesheet);
         CHECK(spritesheet != nullptr, "No spritesheet named ",
               sprite.spritesheet);
         lua_pushlstring(state, spritesheet->name.data(),
                         spritesheet->name.size());
         lua_setfield(state, -2, "spritesheet");
         lua_settable(state, -2);
       }
       return 1;
     }}};

const struct luaL_Reg kFilesystem[] = {
    {"spit",
     [](lua_State* state) {
       auto* filesystem = Registry<Filesystem>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       std::string_view data = GetLuaString(state, 2);
       FixedStringBuffer<kMaxLogLineLength> err;
       if (filesystem->WriteToFile(name, data, &err)) {
         lua_pushnil(state);
       } else {
         lua_pushlstring(state, err.str(), err.size());
       }
       return 1;
     }},
    {"slurp",
     [](lua_State* state) {
       return LoadFileIntoBuffer(state, GetLuaString(state, 1));
     }},
    {"load_lua",
     [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }},
    {"save_lua",
     [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }},
    {"list_directory",
     [](lua_State* state) {
       auto* filesystem = Registry<Filesystem>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       lua_newtable(state);
       filesystem->EnumerateDirectory(name, LuaListDirectory, state);
       return 1;
     }},
    {"exists",
     [](lua_State* state) {
       auto* filesystem = Registry<Filesystem>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       lua_pushboolean(state, filesystem->Exists(name));
       return 1;
     }},
};

const struct luaL_Reg kDataLib[] = {
    {"hash", [](lua_State* state) {
       std::string_view contents;
       switch (lua_type(state, 1)) {
         case LUA_TSTRING:
           contents = GetLuaString(state, 1);
           break;
         case LUA_TUSERDATA: {
           auto* buf = reinterpret_cast<ByteBuffer*>(
               luaL_checkudata(state, 1, "byte_buffer"));
           contents = std::string_view(
               reinterpret_cast<const char*>(buf->contents), buf->size);
         }; break;
         default:
           LUA_ERROR(state, "Argument 1 cannot be hashed");
       }
       lua_pushnumber(state,
                      XXH64(contents.data(), contents.size(), 0xC0D315D474));
       return 1;
     }}};

const struct luaL_Reg kPhysicsLib[] = {
    {"add_box",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const float tx = luaL_checknumber(state, 1);
       const float ty = luaL_checknumber(state, 2);
       const float bx = luaL_checknumber(state, 3);
       const float by = luaL_checknumber(state, 4);
       const float angle = luaL_checknumber(state, 5);
       auto* handle = static_cast<Physics::Handle*>(
           lua_newuserdata(state, sizeof(Physics::Handle)));
       luaL_getmetatable(state, "physics_handle");
       lua_setmetatable(state, -2);
       lua_pushvalue(state, 6);
       *handle = physics->AddBox(FVec(tx, ty), FVec(bx, by), angle,
                                 luaL_ref(state, LUA_REGISTRYINDEX));
       return 1;
     }},
    {"add_circle",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const float tx = luaL_checknumber(state, 1);
       const float ty = luaL_checknumber(state, 2);
       const float radius = luaL_checknumber(state, 3);
       auto* handle = static_cast<Physics::Handle*>(
           lua_newuserdata(state, sizeof(Physics::Handle)));
       luaL_getmetatable(state, "physics_handle");
       lua_setmetatable(state, -2);
       lua_pushvalue(state, 4);
       *handle = physics->AddCircle(FVec(tx, ty), radius,
                                    luaL_ref(state, LUA_REGISTRYINDEX));
       return 1;
     }},
    {"destroy_handle",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       physics->DestroyHandle(*handle);
       return 0;
     }},
    {"create_ground",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       physics->CreateGround();
       return 0;
     }},
    {"set_collision_callback",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       if (lua_gettop(state) != 1) {
         LUA_ERROR(state, "Must pass a function as collision callback");
         return 0;
       }
       struct CollisionContext {
         lua_State* state;
         int func_index;
         Allocator* allocator;
       };
       lua_pushvalue(state, 1);
       auto* allocator = GetAllocator(state);
       auto* context = BraceInit<CollisionContext>(
           allocator, state, luaL_ref(state, LUA_REGISTRYINDEX), allocator);
       physics->SetBeginContactCallback(
           [](uintptr_t lhs, uintptr_t rhs, void* userdata) {
             auto* context = reinterpret_cast<CollisionContext*>(userdata);
             lua_rawgeti(context->state, LUA_REGISTRYINDEX,
                         context->func_index);
             if (lhs != 0) {
               lua_rawgeti(context->state, LUA_REGISTRYINDEX, lhs);
             } else {
               lua_pushnil(context->state);
             }
             if (rhs != 0) {
               lua_rawgeti(context->state, LUA_REGISTRYINDEX, rhs);
             } else {
               lua_pushnil(context->state);
             }
             lua_call(context->state, 2, 0);
           },
           context);
       return 0;
     }},
    {"position",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const FVec2 pos = physics->GetPosition(*handle);
       lua_pushnumber(state, pos.x);
       lua_pushnumber(state, pos.y);
       return 2;
     }},
    {"angle",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float angle = physics->GetAngle(*handle);
       lua_pushnumber(state, angle);
       return 1;
     }},
    {"rotate",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float angle = luaL_checknumber(state, 2);
       physics->Rotate(*handle, angle);
       return 0;
     }},
    {"apply_linear_impulse",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       physics->ApplyLinearImpulse(*handle, FVec(x, y));
       return 0;
     }},
    {"apply_force",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       physics->ApplyForce(*handle, FVec(x, y));
       return 0;
     }},
    {"apply_torque",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float x = luaL_checknumber(state, 2);
       physics->ApplyTorque(*handle, x);
       return 0;
     }},
    {"rotate", [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float angle = luaL_checknumber(state, 2);
       physics->Rotate(*handle, angle);
       return 0;
     }}};

const struct luaL_Reg kWindowLib[] = {
    {"dimensions",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       IVec2 viewport = renderer->viewport();
       lua_pushnumber(state, viewport.x);
       lua_pushnumber(state, viewport.y);
       return 2;
     }},
    {"set_fullscreen",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
       return 0;
     }},
    {"set_borderless",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
       return 0;
     }},
    {"set_windowed",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       // 0 means we use windowed mode.
       SDL_SetWindowFullscreen(window, 0);
       return 0;
     }},
    {"set_title",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowTitle(window, luaL_checkstring(state, 1));
       return 0;
     }},
    {"get_title",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       lua_pushstring(state, SDL_GetWindowTitle(window));
       return 1;
     }},
    {"has_input_focus",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       lua_pushboolean(state,
                       SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
       return 1;
     }},
    {"has_mouse_focus", [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       lua_pushboolean(state,
                       SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
       return 1;
     }}};

const struct luaL_Reg kClockLib[] = {
    {"walltime",
     [](lua_State* state) {
       lua_pushnumber(state, NowInSeconds());
       return 1;
     }},
    {"gametime",
     [](lua_State* state) {
       auto* lua = Registry<Lua>::Retrieve(state);
       lua_pushnumber(state, lua->time());
       return 1;
     }},
    {"sleep_ms",
     [](lua_State* state) {
       SDL_Delay(luaL_checknumber(state, 1));
       return 0;
     }},
    {"gamedelta", [](lua_State* state) {
       auto* lua = Registry<Lua>::Retrieve(state);
       lua_pushnumber(state, lua->dt());
       return 1;
     }}};

constexpr double kRandomRange = 1LL << 32;

const struct luaL_Reg kRandomLib[] = {
    {"from_seed",
     [](lua_State* state) {
       auto* handle =
           static_cast<pcg32*>(lua_newuserdata(state, sizeof(pcg64)));
       handle->seed(luaL_checkinteger(state, 1));
       luaL_getmetatable(state, "random_number_generator");
       lua_setmetatable(state, -2);
       return 1;
     }},
    {"non_deterministic",
     [](lua_State* state) {
       pcg_extras::seed_seq_from<std::random_device> seed_source;
       auto* handle =
           static_cast<pcg32*>(lua_newuserdata(state, sizeof(pcg64)));
       handle->seed(seed_source);
       luaL_getmetatable(state, "random_number_generator");
       lua_setmetatable(state, -2);
       return 1;
     }},
    {"sample",
     [](lua_State* state) {
       auto* handle = static_cast<pcg32*>(
           luaL_checkudata(state, 1, "random_number_generator"));
       lua_Number randnum = (*handle)();
       switch (lua_gettop(state)) {
         case 1:
           lua_pushnumber(state, randnum / kRandomRange);
           break;
         case 3: {
           const double start = luaL_checknumber(state, 2);
           const double end = luaL_checknumber(state, 3);
           lua_pushnumber(state,
                          start + (randnum / kRandomRange) * (end - start));
           break;
         }
       }
       return 1;
     }},
    {"pick", [](lua_State* state) {
       if (lua_gettop(state) != 2) {
         LUA_ERROR(state, "Insufficient arguments");
       }
       auto* handle = static_cast<pcg32*>(
           luaL_checkudata(state, 1, "random_number_generator"));
       if (!lua_istable(state, 2)) {
         LUA_ERROR(state, "Did not pass a sequential table");
       }
       const double val = (*handle)();
       const double size = lua_objlen(state, 2);
       const double pos = 1 + std::floor((val / kRandomRange) * size);
       lua_rawgeti(state, 2, static_cast<int>(pos));
       return 1;
     }}};

struct LuaError {
  std::string_view filename;
  int line = 0;
  std::string_view message;
};

LuaError ParseLuaError(std::string_view message) {
  enum State { FILE, LINE };
  State state = FILE;
  LuaError result;
  for (size_t i = 0; i < message.size(); ++i) {
    const char c = message[i];
    switch (state) {
      case FILE:
        if (c == ':') {
          result.filename = message.substr(0, i);
          state = LINE;
        }
        break;
      case LINE:
        if (c == ':') {
          result.message = message.substr(i + 2);
          return result;
        } else {
          result.line = 10 * result.line + (c - '0');
        }
        break;
    }
  }
  return result;
}

template <size_t N>
void AddLibrary(lua_State* state, const char* name,
                const luaL_Reg (&funcs)[N]) {
  lua_getglobal(state, "G");
  lua_newtable(state);
  for (size_t i = 0; i < N; ++i) {
    CHECK(funcs[i].name != nullptr, "Invalid entry for library ", name, ": ",
          i);
    const auto* func = &funcs[i];
    lua_pushstring(state, func->name);
    lua_pushcfunction(state, func->func);
    lua_settable(state, -3);
  }
  lua_setfield(state, -2, name);
  lua_pop(state, 1);
}

}  // namespace

void* Lua::Alloc(void* ptr, size_t osize, size_t nsize) {
  allocator_stats_.AddSample(nsize);
  if (nsize == 0) {
    if (ptr != nullptr) allocator_->Dealloc(ptr, osize);
    return nullptr;
  }
  if (ptr == nullptr) {
    return allocator_->Alloc(nsize, /*align=*/1);
  }
  return allocator_->Realloc(ptr, osize, nsize, /*align=*/1);
}

Lua::Lua(size_t argc, const char** argv, sqlite3* db, DbAssets* assets,
         Allocator* allocator)
    : argc_(argc),
      argv_(argv),
      allocator_(allocator),
      db_(db),
      assets_(assets),
      compilation_cache_(allocator) {}

void Lua::Crash() {
  std::string_view message = GetLuaString(state_, 1);
  LuaError e = ParseLuaError(message);
  Log(e.filename, e.line, e.message);
  SetError(e.filename, e.line, e.message);
  std::longjmp(on_error_buf_, 1);
}

#define READY()                \
  if (setjmp(on_error_buf_)) { \
    return;                    \
  }

constexpr luaL_Reg kByteBufferMethods[] = {
    {"__index",
     [](lua_State* state) {
       auto* buffer = reinterpret_cast<ByteBuffer*>(
           luaL_checkudata(state, 1, "byte_buffer"));
       size_t index = luaL_checkinteger(state, 2);
       if (index <= 0 || index > buffer->size) {
         LUA_ERROR(state, "Index out of bounds ", index, " not in range [1, ",
                   buffer->size, "]");
       }
       lua_pushinteger(state, buffer->contents[index]);
       return 1;
     }},
    {"__len",
     [](lua_State* state) {
       auto* buffer = reinterpret_cast<ByteBuffer*>(
           luaL_checkudata(state, 1, "byte_buffer"));
       lua_pushinteger(state, buffer->size);
       return 1;
     }},
    {"__tostring",
     [](lua_State* state) {
       auto* buffer = reinterpret_cast<ByteBuffer*>(
           luaL_checkudata(state, 1, "byte_buffer"));
       lua_pushlstring(state, reinterpret_cast<const char*>(buffer->contents),
                       buffer->size);
       return 1;
     }},
    {"__concat", [](lua_State* state) {
       lua_getglobal(state, "tostring");
       lua_pushvalue(state, 1);
       lua_call(state, 1, 1);
       lua_getglobal(state, "tostring");
       lua_pushvalue(state, 2);
       lua_call(state, 1, 1);
       std::string_view a = GetLuaString(state, -2);
       std::string_view b = GetLuaString(state, -1);
       luaL_Buffer buffer;
       luaL_buffinit(state, &buffer);
       luaL_addlstring(&buffer, a.data(), a.size());
       luaL_addlstring(&buffer, b.data(), b.size());
       lua_pop(state, 2);
       luaL_pushresult(&buffer);
       return 1;
     }}};

bool Lua::LoadFromCache(std::string_view script_name, lua_State* state) {
  std::string_view cached;
  if (compilation_cache_.Lookup(script_name, &cached)) {
    LOG("Found cached compilation for ", script_name);
    lua_pushlstring(state, cached.data(), cached.size());
    return true;
  }
  return false;
}

void Lua::InsertIntoCache(std::string_view script_name, lua_State* state) {
  std::string_view compiled = GetLuaString(state, -1);
  auto* buffer = allocator_->Alloc(compiled.size(), 1);
  std::memcpy(buffer, compiled.data(), compiled.size());
  compilation_cache_.Insert(script_name, compiled);
}

void Lua::FlushCompilationCache() {
  LOG("Flushing compilation cache");
  sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
  FixedStringBuffer<256> sql(R"(
    INSERT OR REPLACE 
    INTO compilation_cache (source_name, source_hash_low, source_hash_high, compiled) 
    VALUES (?, ?, ?, ?);"
  )");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
    return;
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  for (const auto& script : assets_->GetScripts()) {
    std::string_view contents;
    if (!compilation_cache_.Lookup(script.name, &contents)) {
      continue;
    }
    XXH128_hash_t checksum = assets_->GetChecksum(script.name);
    sqlite3_bind_text(stmt, 1, script.name.data(), script.name.size(),
                      SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, checksum.low64);
    sqlite3_bind_int64(stmt, 3, checksum.high64);
    sqlite3_bind_text(stmt, 4, contents.data(), contents.size(), SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Failed to flush compilation cache: ", sqlite3_errmsg(db_));
    }
    sqlite3_reset(stmt);
  }
  sqlite3_exec(db_, "END TRANSACTION", nullptr, nullptr, nullptr);
}

void Lua::LoadMetatable(const char* metatable_name, const luaL_Reg* registers,
                        size_t register_count) {
  luaL_newmetatable(state_, metatable_name);
  if (registers == nullptr) return;
  lua_pushstring(state_, metatable_name);
  lua_setfield(state_, -2, "__name");
  for (size_t i = 0; i < register_count; ++i) {
    const luaL_Reg& r = registers[i];
    lua_pushstring(state_, r.name);
    lua_pushcfunction(state_, r.func);
    lua_settable(state_, -3);
  }
  lua_pop(state_, 1);  // Pop the metatable from the stack.
}

void Lua::LoadLibraries() {
  if (state_ != nullptr) lua_close(state_);
  state_ = lua_newstate(&Lua::LuaAlloc, this);
  lua_atpanic(state_, [](lua_State* state) {
    auto* lua = Registry<Lua>::Retrieve(state);
    lua->Crash();
    DIE("What are you doing here? Get out of here, it's gonna blow!");
    return 0;
  });
  READY();
  Register(this);
  // Load metatables.
  LoadMetatable("fvec2", kV2Methods);
  LoadMetatable("fvec3", kV3Methods);
  LoadMetatable("fvec4", kV4Methods);
  LoadMetatable("fmat2x2", kM2x2Methods);
  LoadMetatable("fmat3x3", kM3x3Methods);
  LoadMetatable("fmat4x4", kM4x4Methods);
  LoadMetatable("physics_handle", /*registers=*/nullptr, /*register_count=*/0);
  LoadMetatable("random_number_generator", /*registers=*/nullptr,
                /*register_count=*/0);
  LoadMetatable("byte_buffer", kByteBufferMethods);
  // Create basic initial state.
  luaL_openlibs(state_);
  // Create the global namespace table (G) so functions live under it (e.g.
  // G.graphics.draw_rect).
  lua_newtable(state_);
  lua_setglobal(state_, "G");
  lua_pushcfunction(state_, Traceback);
  traceback_handler_ = lua_gettop(state_);
  Register(assets_);
  // Add all libraries.
  AddLibrary(state_, "console", kConsoleLib);
  AddLibrary(state_, "graphics", kGraphicsLib);
  AddLibrary(state_, "input", kInputLib);
  AddLibrary(state_, "sound", kSoundLib);
  AddLibrary(state_, "filesystem", kFilesystem);
  AddLibrary(state_, "physics", kPhysicsLib);
  AddLibrary(state_, "data", kDataLib);
  AddLibrary(state_, "clock", kClockLib);
  AddLibrary(state_, "system", kSystemLib);
  AddLibrary(state_, "math", kMathLib);
  AddLibrary(state_, "assets", kAssetsLib);
  AddLibrary(state_, "window", kWindowLib);
  AddLibrary(state_, "random", kRandomLib);
  // Set print as G.console.log for consistency.
  lua_pushcfunction(state_, LuaLogPrint);
  lua_setglobal(state_, "print");
}

void Lua::LoadScripts() {
  READY();
  BuildCompilationCache();
  for (const auto& script : assets_->GetScripts()) {
    std::string_view asset_name = script.name;
    if (asset_name == "main.lua") continue;
    ConsumeSuffix(&asset_name, ".lua");
    ConsumeSuffix(&asset_name, ".fnl");
    SetPackagePreload(asset_name);
  }
}

size_t Lua::Error(char* buf, size_t max_size) {
  if (error_.empty()) return 0;
  const size_t size = std::min(error_.size(), max_size);
  std::memcpy(buf, error_.str(), size);
  buf[size] = '\0';
  return size;
}

std::string_view Trim(std::string_view s) {
  size_t i = 0, j = s.size() - 1;
  auto is_whitespace = [&](size_t p) {
    return s[p] == ' ' || s[p] == '\t' || s[p] == '\n';
  };
  while (i < s.size() && is_whitespace(i)) i++;
  while (j > 0 && is_whitespace(j)) j--;
  return s.substr(i, j - i);
}

void Lua::SetError(std::string_view file, int line, std::string_view error) {
  error_.Set("[", file, ":", line, "] ");
  // Remove lines with [C] or "(tail call)" since they are not useful.
  size_t st = 0, en = 0;
  auto flush = [&] {
    if (en <= st) return;
    std::string_view line = error.substr(st, en - st + 1);
    std::string_view trimmed = Trim(line);
    if (!HasPrefix(trimmed, "[C]") && !HasPrefix(trimmed, "(tail call)")) {
      error_.Append(line);
      error_.Append("\n");
    }
  };
  for (size_t i = 0; i < error.size(); ++i) {
    if (error[i] == '\n') {
      flush();
      st = en = i + 1;
    } else {
      en = i;
    }
  }
  flush();
}

void Lua::BuildCompilationCache() {
  FixedStringBuffer<512> sql(R"(
      SELECT c.source_name, c.compiled FROM asset_metadata a 
      INNER JOIN compilation_cache c 
      WHERE a.name = c.source_name AND c.source_hash_low = a.hash_low 
      AND c.source_hash_high = a.hash_high;
    )");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto* contents =
        reinterpret_cast<const char*>(sqlite3_column_blob(stmt, 1));
    size_t content_size = sqlite3_column_bytes(stmt, 1);
    auto* buffer = static_cast<char*>(allocator_->Alloc(content_size, 1));
    std::memcpy(buffer, contents, content_size);
    compilation_cache_.Insert(name, std::string_view(buffer, content_size));
  }
}

void Lua::LoadMain() {
  READY();
  LOG("Loading main file main.lua");
  auto* main = assets_->GetScript("main.lua");
  CHECK(main != nullptr, "Unknown script main.lua");
  LoadLuaAsset(state_, *main, traceback_handler_);
  CHECK(lua_istable(state_, -1), "Main script does not define a table");
  FlushCompilationCache();
  // Check all important functions are defined.
  for (const char* fn : {"init", "update", "draw"}) {
    lua_getfield(state_, -1, fn);
    if (lua_isnil(state_, -1)) {
      DIE("Cannot run main code: ", fn, " is not defined in main.lua");
    }
    lua_pop(state_, 1);
  }
  lua_setglobal(state_, "_Game");
  LOG("Loaded main successfully");
}

void Lua::SetPackagePreload(std::string_view filename) {
  // We use a buffer to ensure that filename is null terminated.
  FixedStringBuffer<kMaxPathLength> buf(filename);
  lua_getglobal(state_, "package");
  lua_getfield(state_, -1, "preload");
  lua_pushcfunction(state_, &PackageLoader);
  lua_setfield(state_, -2, buf);
  lua_pop(state_, 2);
}

void Lua::Init() {
  LOG("Initializing game");
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "init");
  if (lua_isnil(state_, -1)) {
    LUA_ERROR(state_, "No main function");
    return;
  }
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, LUA_MULTRET, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::Update(float t, float dt) {
  if (!error_.empty()) return;
  READY();
  t_ = t;
  dt_ = dt;
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "update");
  lua_insert(state_, -2);
  lua_pushnumber(state_, t);
  lua_pushnumber(state_, dt);
  if (lua_pcall(state_, 3, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::Draw() {
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "draw");
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleKeypressed(int scancode) {
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "keypressed");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, scancode);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleKeyreleased(int scancode) {
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "keyreleased");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, scancode);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleMousePressed(int button) {
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "mousepressed");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, button);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleTextInput(std::string_view input) {
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "textinput");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushlstring(state_, input.data(), input.size());
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleMouseReleased(int button) {
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "mousereleased");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, button);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleMouseMoved(FVec2 pos, FVec2 delta) {
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "mousemoved");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, pos.x);
  lua_pushnumber(state_, pos.y);
  lua_pushnumber(state_, delta.x);
  lua_pushnumber(state_, delta.y);
  if (lua_pcall(state_, 5, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleQuit() {
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "quit");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

}  // namespace G
