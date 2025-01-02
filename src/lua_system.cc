#include "lua_system.h"

#include "SDL.h"

namespace G {
namespace {

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

}  // namespace

void AddSystemLibrary(Lua* lua) {
  lua->AddLibrary("system", kSystemLib);
  lua->AddLibrary("clock", kClockLib);
}

}  // namespace G
