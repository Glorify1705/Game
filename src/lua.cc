#include "lua.h"

#include "input.h"
#include "physics.h"
#include "renderer.h"
#include "sound.h"

namespace {

static int PackageLoader(lua_State* state) {
  const char* modname = luaL_checkstring(state, 1);
  StringBuffer<127> buf(modname, ".lua");
  const auto* asset = Registry<Assets>::Retrieve(state)->GetScript(buf);
  LOG("Loading package ", modname, " from file ", buf);
  if (asset == nullptr) {
    luaL_error(state, "Could not find asset %s.lua", modname);
    return 0;
  }
  const char* name = asset->filename()->c_str();
  auto data = reinterpret_cast<const char*>(asset->contents()->Data());
  if (luaL_loadbuffer(state, data, asset->contents()->size(), name) != LUA_OK) {
    luaL_error(state, "Failure in %s: %s", name, lua_tostring(state, -1));
    return 0;
  }
  if (lua_pcall(state, 0, LUA_MULTRET, 0) != LUA_OK) {
    luaL_error(state, "Failure in %s: %s", name, lua_tostring(state, -1));
    return 0;
  }
  return 1;
}

static const struct luaL_Reg kRendererLib[] = {
    {"draw_sprite",
     [](lua_State* state) {
       const int parameters = lua_gettop(state);
       size_t length;
       const char* texture = luaL_checklstring(state, 1, &length);
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       float angle = 0;
       if (parameters == 4) angle = luaL_checknumber(state, 4);
       auto* renderer = Registry<SpriteSheetRenderer>::Retrieve(state);
       if (auto* sub_texture = renderer->sub_texture(texture, length);
           sub_texture != nullptr) {
         renderer->Draw(FVec2(x, y), angle, *sub_texture);
       } else {
         luaL_error(state, "unknown texture %s", texture);
       }
       return 0;
     }},
    {"viewport",
     [](lua_State* state) {
       auto* renderer = Registry<SpriteSheetRenderer>::Retrieve(state);
       IVec2 viewport = renderer->viewport();
       lua_pushnumber(state, viewport.x);
       lua_pushnumber(state, viewport.y);
       return 2;
     }},
    {"push",
     [](lua_State* state) {
       auto* renderer = Registry<SpriteSheetRenderer>::Retrieve(state);
       renderer->Push();
       return 0;
     }},
    {"pop",
     [](lua_State* state) {
       auto* renderer = Registry<SpriteSheetRenderer>::Retrieve(state);
       renderer->Pop();
       return 0;
     }},
    {"rotate",
     [](lua_State* state) {
       auto* renderer = Registry<SpriteSheetRenderer>::Retrieve(state);
       renderer->Rotate(luaL_checknumber(state, 1));
       return 0;
     }},
    {"scale",
     [](lua_State* state) {
       auto* renderer = Registry<SpriteSheetRenderer>::Retrieve(state);
       renderer->Scale(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       return 0;
     }},
    {"translate",
     [](lua_State* state) {
       auto* renderer = Registry<SpriteSheetRenderer>::Retrieve(state);
       renderer->Translate(luaL_checknumber(state, 1),
                           luaL_checknumber(state, 2));
       return 0;
     }},
    {nullptr, nullptr}};

static const struct luaL_Reg kConsoleLib[] = {
    {"log",
     [](lua_State* state) {
       const int num_args = lua_gettop(state);
       StringBuffer<255> buffer;
       lua_getglobal(state, "tostring");
       for (int i = 0; i < num_args; ++i) {
         // Call tostring to print the value of the argument.
         lua_pushvalue(state, -1);
         lua_pushvalue(state, i + 1);
         lua_call(state, 1, 1);
         const char* s = lua_tostring(state, -1);
         if (s == nullptr) {
           return luaL_error(state, "'tostring' did not return string");
         }
         buffer.Append(s);
         if (i + 1 < num_args) buffer.Append(" ");
         lua_pop(state, 1);
       }
       lua_pop(state, 1);
       lua_Debug ar;
       lua_getstack(state, 1, &ar);
       lua_getinfo(state, "nSl", &ar);
       int line = ar.currentline;
       const char* file = ar.source;
       lua_pop(state, 1);
       Log(file, line, buffer);
       return 0;
     }},
    {nullptr, nullptr}};

static const struct luaL_Reg kMouseLib[] = {
    {"mouse_position",
     [](lua_State* state) {
       const FVec2 pos = Mouse::GetPosition();
       lua_pushnumber(state, pos.x);
       lua_pushnumber(state, pos.y);
       return 2;
     }},
    {"is_key_down",
     [](lua_State* state) {
       size_t len;
       const char* c = luaL_checklstring(state, 1, &len);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state,
                       keyboard->IsDown(keyboard->StrToScancode(c, len)));
       return 1;
     }},
    {"is_key_released",
     [](lua_State* state) {
       size_t len;
       const char* c = luaL_checklstring(state, 1, &len);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state,
                       keyboard->IsReleased(keyboard->StrToScancode(c, len)));
       return 1;
     }},
    {"is_key_pressed",
     [](lua_State* state) {
       size_t len;
       const char* c = luaL_checklstring(state, 1, &len);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state,
                       keyboard->IsPressed(keyboard->StrToScancode(c, len)));
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
    {nullptr, nullptr}};

static const struct luaL_Reg kSoundLib[] = {
    {"play",
     [](lua_State* state) {
       const char* name = luaL_checkstring(state, 1);
       auto* sound = Registry<Sound>::Retrieve(state);
       int repeat = -1;
       const int num_args = lua_gettop(state);
       if (num_args == 2) repeat = luaL_checknumber(state, 2);
       sound->Play(name, repeat);
       return 0;
     }},
    {nullptr, nullptr}};

static const struct luaL_Reg kAssetsLib[] = {
    {"subtexture",
     [](lua_State* state) {
       size_t len;
       const char* name = luaL_checklstring(state, 1, &len);
       auto* renderer = Registry<SpriteSheetRenderer>::Retrieve(state);
       auto* subtexture = renderer->sub_texture(name, len);
       if (subtexture == nullptr) {
         luaL_error(state, "Could not find a subtexture %s", name);
       }
       luaL_newmetatable(state, "asset_subtexture_ptr");
       lua_pop(state, 1);
       lua_pushlightuserdata(state, renderer);
       luaL_setmetatable(state, "asset_subtexture_ptr");
       return 1;
     }},
    {"subtexture_info",
     [](lua_State* state) {
       const assets::Subtexture* ptr = nullptr;
       if (lua_isstring(state, 1)) {
         auto* renderer = Registry<SpriteSheetRenderer>::Retrieve(state);
         size_t len;
         const char* name = luaL_checklstring(state, 1, &len);
         ptr = renderer->sub_texture(name, len);
         if (ptr == nullptr) {
           luaL_error(state, "Could not find an image called %s", name);
         }
       } else {
         ptr = reinterpret_cast<const assets::Subtexture*>(
             luaL_checkudata(state, 1, "asset_subtexture_ptr"));
       }
       lua_newtable(state);
       lua_pushnumber(state, ptr->width());
       lua_setfield(state, -2, "width");
       lua_pushnumber(state, ptr->height());
       lua_setfield(state, -2, "height");
       return 1;
     }},
    {nullptr, nullptr}};

static const struct luaL_Reg kPhysicsLib[] = {
    {"add_box",
     [](lua_State* state) {
       const auto tx = luaL_checknumber(state, 1);
       const auto ty = luaL_checknumber(state, 2);
       const auto bx = luaL_checknumber(state, 3);
       const auto by = luaL_checknumber(state, 4);
       const auto px = luaL_checknumber(state, 5);
       const auto py = luaL_checknumber(state, 6);
       auto* physics = Registry<Physics>::Retrieve(state);
       Physics::Handle handle =
           physics->AddBox(FVec2(tx, ty), FVec2(bx, by), FVec2(px, py));
       lua_pushinteger(state, handle.id);
       return 1;
     }},
    {"get_position",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const auto handle = luaL_checkinteger(state, 1);
       const FVec2 pos =
           physics->GetPosition(Physics::Handle{.id = uint32_t(handle)});
       lua_pushnumber(state, pos.x);
       lua_pushnumber(state, pos.y);
       return 2;
     }},
    {"get_angle",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const auto handle = luaL_checkinteger(state, 1);
       const float angle =
           physics->GetAngle(Physics::Handle{.id = uint32_t(handle)});
       lua_pushnumber(state, angle);
       return 1;
     }},
    {nullptr, nullptr}};

struct LuaLine {
  char file[128];
  int line;
};

std::string_view GetLuaLine(std::string_view line, LuaLine* result) {
  enum State { PREAMBLE, FILE, POSTAMBLE, LINE, FINISH };
  State state = PREAMBLE;
  size_t pos = 0, p = 0, q = 0;
  char buf[16] = {0};
  for (; pos < line.size(); ++pos) {
    const char c = line[pos];
    switch (state) {
      case PREAMBLE:
        if (c == '"') state = FILE;
        break;
      case FILE:
        if (c == '"') {
          result->file[p] = '\0';
          state = POSTAMBLE;
        } else {
          result->file[p++] = c;
        }
        break;
      case POSTAMBLE:
        if (isdigit(c)) {
          buf[q++] = c;
          state = LINE;
        }
        break;
      case LINE:
        if (!isdigit(c)) {
          state = FINISH;
        } else {
          buf[q++] = c;
        }
        break;
      case FINISH:
        sscanf(buf, "%d", &result->line);
        line.remove_prefix(pos + 1);
        return line;
    }
  }
  return std::string_view();
}

template <typename... Ts>
void LuaCrash(lua_State* state, int idx, Ts... ts) {
  LuaLine l;
  auto message = GetLuaLine(luaL_checkstring(state, idx), &l);
  if constexpr (sizeof...(ts) > 0) {
    Crash(l.file, l.line, message, " (", std::forward<Ts>(ts)..., ")");
  } else {
    Crash(l.file, l.line, message);
  }
}

void AddLibrary(lua_State* state, const char* name, const luaL_Reg* funcs) {
  lua_getglobal(state, "G");
  lua_newtable(state);
  luaL_setfuncs(state, funcs, 0);
  lua_setfield(state, -2, name);
  lua_pop(state, 1);
}

}  // namespace

Lua::Lua(const char* script_name, Assets* assets) {
  state_ = luaL_newstate();
  lua_atpanic(state_, [](lua_State* state) {
    LuaCrash(state, 1);
    return 0;
  });
  luaL_openlibs(state_);
  lua_newtable(state_);
  lua_setglobal(state_, "G");
  Register(assets);
  AddLibrary(state_, "console", kConsoleLib);
  AddLibrary(state_, "renderer", kRendererLib);
  AddLibrary(state_, "input", kMouseLib);
  AddLibrary(state_, "sound", kSoundLib);
  AddLibrary(state_, "physics", kPhysicsLib);
  AddLibrary(state_, "assets", kAssetsLib);
  lua_pushcfunction(state_, [](lua_State* state) {
    const char* message = luaL_checkstring(state, 1);
    luaL_traceback(state, state, message, 1);
    return 1;
  });
  traceback_handler_ = lua_gettop(state_);
  for (size_t i = 0; i < assets->scripts(); ++i) {
    auto* script = assets->GetScriptByIndex(i);
    std::string_view asset_name(script->filename()->c_str());
    ConsumeSuffix(&asset_name, ".lua");
    if (asset_name != "main") {
      SetPackagePreload(asset_name);
    }
  }
  auto* main = assets->GetScript(script_name);
  CHECK(main != nullptr, "Unknown script ", script_name);
  LoadMain(*main);
}

void Lua::Start() {}

void Lua::LoadAsset(const assets::Script& asset) {
  const char* name = asset.filename()->c_str();
  if (luaL_loadbuffer(state_,
                      reinterpret_cast<const char*>(asset.contents()->Data()),
                      asset.contents()->size(), name) != 0) {
    LuaCrash(state_, -1, "while loading ", name);
  }
  if (lua_pcall(state_, 0, LUA_MULTRET, traceback_handler_) != LUA_OK) {
    LuaCrash(state_, -1, "while running ", name);
  }
}

void Lua::SetPackagePreload(std::string_view filename) {
  // We use a buffer to ensure that filename is null terminated.
  StringBuffer<127> buf(filename);
  LOG("Setting package preload for ", buf);
  lua_getglobal(state_, "package");
  lua_getfield(state_, -1, "preload");
  lua_pushcfunction(state_, &PackageLoader);
  lua_setfield(state_, -2, buf);
  lua_pop(state_, 2);
}

void Lua::LoadMain(const assets::Script& asset) {
  const char* name = asset.filename()->c_str();
  LOG("Loading ", name);
  LoadAsset(asset);
  // Check all important functions are defined.
  for (const char* fn : {"Init", "Update", "Render"}) {
    lua_getglobal(state_, fn);
    if (lua_isnil(state_, -1)) {
      DIE("Cannot run main code: ", fn, " is not defined in ", name);
    }
    lua_pop(state_, 1);
  }
}
