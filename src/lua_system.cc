#include "lua_system.h"

#include "SDL.h"

namespace G {
namespace {

const struct LuaApiFunction kSystemLib[] = {
    {"quit",
     "Quits the game",
     {},
     {},
     [](lua_State* state) {
       auto* lua = Registry<Lua>::Retrieve(state);
       lua->HandleQuit();
       lua->Stop();
       return 0;
     }},
    {"operating_system",
     "Returns the name of the operating system",
     {},
     {{"os", "the operating system name", "string"}},
     [](lua_State* state) {
       lua_pushstring(state, SDL_GetPlatform());
       return 1;
     }},
    {"cpu_count",
     "Returns the number of logical CPU cores",
     {},
     {{"count", "the number of CPUs", "integer"}},
     [](lua_State* state) {
       lua_pushinteger(state, SDL_GetCPUCount());
       return 1;
     }},
    {"set_clipboard",
     "Sets the system clipboard text",
     {{"text", "the text to copy to the clipboard", "string"}},
     {},
     [](lua_State* state) {
       const char* str = luaL_checkstring(state, 1);
       SDL_SetClipboardText(str);
       return 0;
     }},
    {"open_url",
     "Opens a URL in the default browser",
     {{"url", "the URL to open", "string"}},
     {{"error", "nil on success, error message on failure", "string"}},
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
     "Returns the command-line arguments as a table",
     {},
     {{"args", "table of argument strings", "table"}},
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
    {"get_clipboard",
     "Returns the current system clipboard text",
     {},
     {{"text", "the clipboard contents", "string"}},
     [](lua_State* state) {
       char* result = SDL_GetClipboardText();
       const size_t length = strlen(result);
       if (length == 0) {
         LUA_ERROR(state, "Failed to get the clipboard: ", SDL_GetError());
         return 0;
       }
       lua_pushlstring(state, result, length);
       return 1;
     }}};

const struct LuaApiFunction kClockLib[] = {
    {"walltime",
     "Returns the wall clock time in seconds",
     {},
     {{"time", "time in seconds", "number"}},
     [](lua_State* state) {
       lua_pushnumber(state, NowInSeconds());
       return 1;
     }},
    {"gametime",
     "Returns the elapsed game time in seconds",
     {},
     {{"time", "game time in seconds", "number"}},
     [](lua_State* state) {
       auto* lua = Registry<Lua>::Retrieve(state);
       lua_pushnumber(state, lua->time());
       return 1;
     }},
    {"sleep_ms",
     "Sleeps for the given number of milliseconds",
     {{"ms", "the number of milliseconds to sleep", "number"}},
     {},
     [](lua_State* state) {
       SDL_Delay(luaL_checknumber(state, 1));
       return 0;
     }},
    {"gamedelta",
     "Returns the time elapsed since the last frame in seconds",
     {},
     {{"dt", "delta time in seconds", "number"}},
     [](lua_State* state) {
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
