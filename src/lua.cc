#include "lua.h"

#include "SDL.h"
#include "clock.h"
#include "console.h"
#include "image.h"
#include "input.h"
#include "physics.h"
#include "renderer.h"
#include "sound.h"

namespace G {
namespace {

template <typename T>
T FromLuaTable(lua_State* state, int index) {
  T result;
  if (!lua_istable(state, index)) {
    luaL_error(state, "Not a table");
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
    luaL_error(state, "Not a table");
    return result;
  }
  for (size_t i = 0; i < T::kDimension; ++i) {
    lua_rawgeti(state, index, i + 1);
    if (!lua_istable(state, index)) {
      luaL_error(state, "Not a table");
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

int LoadLuaAsset(lua_State* state, const ScriptAsset& asset,
                 int traceback_handler = INT_MAX) {
  const char* name = asset.filename()->c_str();
  FixedStringBuffer<128> buf("@", name);
  if (luaL_loadbuffer(state,
                      reinterpret_cast<const char*>(asset.contents()->Data()),
                      asset.contents()->size(), buf.str()) != 0) {
    lua_error(state);
    return 0;
  }
  if (traceback_handler != INT_MAX) {
    if (lua_pcall(state, 0, 1, traceback_handler)) {
      lua_error(state);
    }
  } else {
    lua_call(state, 0, 1);
  }
  return 1;
}

int PackageLoader(lua_State* state) {
  const char* modname = luaL_checkstring(state, 1);
  FixedStringBuffer<127> buf(modname, ".lua");
  const auto* asset = Registry<Assets>::Retrieve(state)->GetScript(buf.piece());
  LOG("Loading package ", modname, " from file ", buf);
  if (asset == nullptr) {
    luaL_error(state, "Could not find asset %s.lua", modname);
    return 0;
  }
  return LoadLuaAsset(state, *asset);
}

std::string_view GetLuaString(lua_State* state, int index) {
  size_t len;
  const char* data = luaL_checklstring(state, index, &len);
  return std::string_view(data, len);
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
       if (auto* sprite = renderer->sprite(spritename); sprite != nullptr) {
         renderer->Draw(FVec2(x, y), angle, *sprite);
       } else {
         luaL_error(state, "unknown sprite %s", sprite);
       }
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
       renderer->DrawRect(FVec2(x1, y1), FVec2(x2, y2), angle);
       return 0;
     }},
    {"set_color",
     [](lua_State* state) {
       const float r = luaL_checknumber(state, 1);
       const float g = luaL_checknumber(state, 2);
       const float b = luaL_checknumber(state, 3);
       const float a = luaL_checknumber(state, 4);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       auto clamp = [](float f) -> uint8_t {
         return std::clamp(f, 0.0f, 255.0f);
       };
       const Color previous =
           renderer->SetColor(Color{clamp(r), clamp(g), clamp(b), clamp(a)});
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
       renderer->DrawCircle(FVec2(x, y), radius);
       return 0;
     }},
    {"draw_text",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       std::string_view text = GetLuaString(state, 1);
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       renderer->DrawText("terminus.ttf", 32, text, FVec2(x, y));
       return 0;
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
    {"take_screenshot",
     [](lua_State* state) {
       class Context {
        public:
         Context(lua_State* state) : state_(state) {
           output_file_.Set(GetLuaString(state, 1));
           renderer_ = Registry<BatchRenderer>::Retrieve(state);
           const IVec2 viewport = renderer_->GetViewport();
           width_ = viewport.x;
           height_ = viewport.y;
           buffer_ = new uint8_t[width_ * height_ * 4];
         }
         ~Context() { delete[] buffer_; }

         void RequestScreenshot() {
           renderer_->RequestScreenshot(buffer_, width_, height_, this);
         }

         void HandleScreenshot(uint8_t* pixels, size_t width, size_t height) {
           LOG("Writing screenshot");
           if (!WritePixelsToImage(output_file_.str(), pixels, width, height)) {
             luaL_error(state_, "Could not write image %s to disk",
                        output_file_.str(), state_);
           }
           delete this;
         }

        private:
         BatchRenderer* renderer_;
         lua_State* state_;
         FixedStringBuffer<127> output_file_;
         uint8_t* buffer_;
         size_t width_, height_;
       };
       auto* context = new Context(state);
       context->RequestScreenshot();
       return 0;
     }},
    {"new_shader",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       std::string_view code = GetLuaString(state, 2);
       const bool compiles = shaders->Compile(
           HasSuffix(name, ".vert") ? ShaderType::VERTEX : ShaderType::FRAGMENT,
           name, code);
       if (!compiles) {
         luaL_error(state, "Could not compile shader %s: %s", name,
                    shaders->LastError());
       }
       return 0;
     }},
    {"attach_shader",
     [](lua_State* state) {
       auto* renderer = Registry<BatchRenderer>::Retrieve(state);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       std::string_view fragment_shader = GetLuaString(state, 1);
       std::string_view program_name = fragment_shader;
       FixedStringBuffer<128> buf(fragment_shader);
       if (!ConsumeSuffix(&program_name, ".frag")) {
         luaL_error(state,
                    "Could not switch shader %s: not a fragment shader (i.e. "
                    "name does not end in .frag)",
                    buf, shaders->LastError());
         return 0;
       }
       if (!shaders->Link(program_name, "post_pass.vert", fragment_shader)) {
         luaL_error(state, "Could not switch shader %s: %s", buf,
                    shaders->LastError().data());
         return 0;
       }
       renderer->SwitchShaderProgram(program_name);
       return 0;
     }},
    {"send_uniform",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (lua_isnumber(state, 2)) {
         if (!shaders->SetUniformF(name, luaL_checknumber(state, 2))) {
           luaL_error(state, "Could not set uniform ", name, ": ",
                      shaders->LastError().data());
         }
         return 0;
       }
       if (!lua_istable(state, 2)) {
         luaL_error(state, "Not a table");
         return 0;
       }
       switch (lua_objlen(state, 2)) {
         case 2:
           if (!shaders->SetUniform(name, FromLuaTable<FVec2>(state, 2))) {
             luaL_error(state, "Could not set uniform ", name, ": ",
                        shaders->LastError().data());
           };
           break;
         case 3:
           if (!shaders->SetUniform(name, FromLuaTable<FVec3>(state, 2))) {
             luaL_error(state, "Could not set uniform ", name, ": ",
                        shaders->LastError().data());
           };
           break;
         case 4:
           if (!shaders->SetUniform(name, FromLuaTable<FVec4>(state, 2))) {
             luaL_error(state, "Could not set uniform ", name, ": ",
                        shaders->LastError().data());
           }

           break;
       }
       return 0;
     }},
    {"send_f2_uniform",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (!shaders->SetUniform(name, FromLuaTable<FVec2>(state, 2))) {
         luaL_error(state, "Could not set uniform ", name, ": ",
                    shaders->LastError().data());
       }
       return 0;
     }},
    {"send_f3_uniform",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (!shaders->SetUniform(name, FromLuaTable<FVec3>(state, 2))) {
         luaL_error(state, "Could not set uniform ", name, ": ",
                    shaders->LastError().data());
       }
       return 0;
     }},
    {"send_f4_uniform",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (!shaders->SetUniform(name, FromLuaTable<FVec4>(state, 2))) {
         luaL_error(state, "Could not set uniform ", name, ": ",
                    shaders->LastError().data());
       }
       return 0;
     }},
    {"send_f2x2_uniform",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (!shaders->SetUniform(name, FromLuaTable<FMat2x2>(state, 2))) {
         luaL_error(state, "Could not set uniform ", name, ": ",
                    shaders->LastError().data());
       }
       return 0;
     }},
    {"send_f3x3_uniform",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (!shaders->SetUniform(name, FromLuaTable<FMat3x3>(state, 2))) {
         luaL_error(state, "Could not set uniform ", name, ": ",
                    shaders->LastError().data());
       }
       return 0;
     }},
    {"send_f4x4_uniform",
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (!shaders->SetUniform(name, FromLuaTable<FMat4x4>(state, 2))) {
         luaL_error(state, "Could not set uniform ", name, ": ",
                    shaders->LastError().data());
       }
       return 0;
     }},
    {nullptr, nullptr}};

const struct luaL_Reg kSystemLib[] = {
    {"operating_system",
     [](lua_State* state) {
       lua_pushstring(state, SDL_GetPlatform());
       return 1;
     }},
    {"set_clipboard",
     [](lua_State* state) {
       const char* str = luaL_checkstring(state, 1);
       SDL_SetClipboardText(str);
       return 0;
     }},
    {"get_clipboard",
     [](lua_State* state) {
       char* result = SDL_GetClipboardText();
       const size_t length = strlen(result);
       if (length == 0) {
         return luaL_error(state, "Failed to get the clipboard: %s",
                           SDL_GetError());
       }
       lua_pushlstring(state, result, length);
       return 1;
     }},
    {nullptr, nullptr}};

int LuaLogPrint(lua_State* state) {
  const int num_args = lua_gettop(state);
  FixedStringBuffer<kMaxLogLineLength> buffer;
  lua_getglobal(state, "tostring");
  for (int i = 0; i < num_args; ++i) {
    // Call tostring to print the value of the argument.
    lua_pushvalue(state, -1);
    lua_pushvalue(state, i + 1);
    lua_call(state, 1, 1);
    size_t length;
    const char* s = lua_tolstring(state, -1, &length);
    if (s == nullptr) {
      return luaL_error(state, "'tostring' did not return string");
    }
    buffer.Append(std::string_view(s, length));
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
  Log(file, line, buffer);
  return 0;
}

const struct luaL_Reg kConsoleLib[] = {
    {"log", LuaLogPrint},
    {"watch",
     [](lua_State* state) {
       auto* console = Registry<DebugConsole>::Retrieve(state);
       lua_getglobal(state, "tostring");
       const std::string_view key = GetLuaString(state, 1);
       lua_pushvalue(state, -1);
       lua_pushvalue(state, 2);
       lua_call(state, 1, 1);
       size_t length;
       const char* s = lua_tolstring(state, -1, &length);
       if (s == nullptr) {
         return luaL_error(state, "'tostring' did not return string");
       }
       console->AddWatcher(key, std::string_view(s, length));
       return 0;
     }},
    {nullptr, nullptr}};

const struct luaL_Reg kMouseLib[] = {
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
       lua_pushboolean(state, keyboard->IsDown(keyboard->StrToScancode(c)));
       return 1;
     }},
    {"is_key_released",
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsReleased(keyboard->StrToScancode(c)));
       return 1;
     }},
    {"is_key_pressed",
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsPressed(keyboard->StrToScancode(c)));
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
    {"get_controller_axis",
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushnumber(
           state, controllers->AxisPositions(controllers->StrToAxisOrTrigger(c),
                                             controllers->active_controller()));
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

const struct luaL_Reg kSoundLib[] = {
    {"play_music",
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* sound = Registry<Sound>::Retrieve(state);
       int repeat = Sound::kLoop;
       const int num_args = lua_gettop(state);
       if (num_args == 2) repeat = luaL_checknumber(state, 2);
       sound->PlayMusic(name.data(), repeat);
       return 0;
     }},
    {"play_sfx",
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->PlaySoundEffect(name.data());
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
         luaL_error(state, "Invalid volume %f must be in [0, 1)", volume);
         return 0;
       }
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->SetMusicVolume(volume);
       return 0;
     }},
    {"set_sfx_volume",
     [](lua_State* state) {
       const float volume = luaL_checknumber(state, 1);
       if (volume < 0 || volume > 1) {
         luaL_error(state, "Invalid volume %f must be in [0, 1)", volume);
         return 0;
       }
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->SetSoundEffectVolume(volume);
       return 0;
     }},
    {nullptr, nullptr}};

const struct luaL_Reg kAssetsLib[] = {
    {"sprite",
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       auto* subtexture = renderer->sprite(name);
       if (subtexture == nullptr) {
         luaL_error(state, "Could not find a subtexture %s", name);
       }
       lua_pushlightuserdata(state, renderer);
       lua_getfield(state, LUA_REGISTRYINDEX, "asset_sprite_ptr");
       lua_setmetatable(state, -2);
       return 1;
     }},
    {"sprite_info",
     [](lua_State* state) {
       const SpriteAsset* ptr = nullptr;
       if (lua_isstring(state, 1)) {
         auto* renderer = Registry<Renderer>::Retrieve(state);
         std::string_view name = GetLuaString(state, 1);
         ptr = renderer->sprite(name);
         if (ptr == nullptr) {
           luaL_error(state, "Could not find an image called %s", name);
         }
       } else {
         ptr = reinterpret_cast<const SpriteAsset*>(
             luaL_checkudata(state, 1, "asset_sprite_ptr"));
       }
       lua_newtable(state);
       lua_pushnumber(state, ptr->width());
       lua_setfield(state, -2, "width");
       lua_pushnumber(state, ptr->height());
       lua_setfield(state, -2, "height");
       return 1;
     }},
    {nullptr, nullptr}};

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
    {"destroy_handle",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       physics->DestroyHandle(*handle);
       return 0;
     }},
    {"set_collision_callback",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       if (lua_gettop(state) != 1) {
         luaL_error(state, "Must pass a function as collision callback");
         return 0;
       }
       struct CollisionContext {
         lua_State* state;
         int func_index;
       };
       lua_pushvalue(state, 1);
       auto* context =
           new CollisionContext{state, luaL_ref(state, LUA_REGISTRYINDEX)};
       physics->SetBeginContactCallback(
           [](uintptr_t lhs, uintptr_t rhs, void* userdata) {
             auto context = reinterpret_cast<CollisionContext*>(userdata);
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
    {"rotate",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float angle = luaL_checknumber(state, 2);
       physics->Rotate(*handle, angle);
       return 0;
     }},
    {nullptr, nullptr}};

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
    {nullptr, nullptr}};

const struct luaL_Reg kClockLib[] = {{"now",
                                      [](lua_State* state) {
                                        lua_pushnumber(state, NowInSeconds());
                                        return 1;
                                      }},
                                     {nullptr, nullptr}};

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

template <typename... Ts>
void LuaCrash(lua_State* state, int idx, Ts... ts) {
  std::string_view message = GetLuaString(state, idx);
  LuaError e = ParseLuaError(message);
  if constexpr (sizeof...(ts) > 0) {
    Crash(e.filename, e.line, e.message, " (", std::forward<Ts>(ts)..., ")");
  } else {
    Crash(e.filename, e.line, e.message);
  }
}

void AddLibrary(lua_State* state, const char* name, const luaL_Reg* funcs) {
  lua_getglobal(state, "G");
  lua_newtable(state);
  for (auto* func = funcs; func->name != nullptr; func++) {
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

Lua::Lua(std::string_view script_name, Assets* assets, Allocator* allocator)
    : allocator_(allocator) {
  state_ = luaL_newstate();
  lua_setallocf(state_, &Lua::LuaAlloc, this);
  lua_atpanic(state_, [](lua_State* state) {
    LuaCrash(state, 1);
    return 0;
  });
  Register(this);
  luaL_newmetatable(state_, "physics_handle");
  luaL_newmetatable(state_, "asset_subtexture_ptr");
  lua_pop(state_, 2);
  luaL_openlibs(state_);
  lua_newtable(state_);
  lua_pushcfunction(state_, [](lua_State* state) {
    auto* lua = Registry<Lua>::Retrieve(state);
    lua->Stop();
    return 0;
  });
  lua_setfield(state_, -2, "quit");
  lua_setglobal(state_, "G");
  Register(assets);
  AddLibrary(state_, "console", kConsoleLib);
  AddLibrary(state_, "graphics", kGraphicsLib);
  AddLibrary(state_, "input", kMouseLib);
  AddLibrary(state_, "sound", kSoundLib);
  AddLibrary(state_, "physics", kPhysicsLib);
  AddLibrary(state_, "assets", kAssetsLib);
  AddLibrary(state_, "clock", kClockLib);
  AddLibrary(state_, "system", kSystemLib);
  AddLibrary(state_, "window", kWindowLib);
  lua_pushcfunction(state_, Traceback);
  traceback_handler_ = lua_gettop(state_);
  // Set print as G.console.log for consistency.
  lua_pushcfunction(state_, LuaLogPrint);
  lua_setglobal(state_, "print");
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

void Lua::SetPackagePreload(std::string_view filename) {
  // We use a buffer to ensure that filename is null terminated.
  FixedStringBuffer<127> buf(filename);
  lua_getglobal(state_, "package");
  lua_getfield(state_, -1, "preload");
  lua_pushcfunction(state_, &PackageLoader);
  lua_setfield(state_, -2, buf);
  lua_pop(state_, 2);
}

void Lua::LoadMain(const ScriptAsset& asset) {
  const char* name = asset.filename()->c_str();
  LoadLuaAsset(state_, asset, traceback_handler_);
  // Check all important functions are defined.
  for (const char* fn : {"init", "update", "draw"}) {
    lua_getfield(state_, -1, fn);
    if (lua_isnil(state_, -1)) {
      DIE("Cannot run main code: ", fn, " is not defined in ", name);
    }
    lua_pop(state_, 1);
  }
  lua_setglobal(state_, "Game");
}

void Lua::Init() {
  lua_getglobal(state_, "Game");
  lua_getfield(state_, -1, "init");
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, LUA_MULTRET, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::Update(float t, float dt) {
  lua_getglobal(state_, "Game");
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
  lua_getglobal(state_, "Game");
  lua_getfield(state_, -1, "draw");
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

}  // namespace G