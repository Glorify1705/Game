#include "lua_test.h"

#include <cmath>

#include "clock.h"
#include "input.h"
#include "lua.h"

namespace G {
namespace {

const struct LuaApiFunction kTestLib[] = {
    {"key_down",
     "Injects a key-down event for the named key.",
     {{"key", "the key name", "string"}},
     {},
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       Keyboard::PressConditions pc = keyboard->MapKey(name);
       keyboard->InjectKeyDown(pc.code);
       return 0;
     }},
    {"key_up",
     "Injects a key-up event for the named key.",
     {{"key", "the key name", "string"}},
     {},
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       Keyboard::PressConditions pc = keyboard->MapKey(name);
       keyboard->InjectKeyUp(pc.code);
       return 0;
     }},
    {"mouse_down",
     "Injects a mouse-button-down event. Buttons: 0=left, 1=middle, 2=right.",
     {{"button", "mouse button index", "number"}},
     {},
     [](lua_State* state) {
       int button = luaL_checknumber(state, 1);
       auto* mouse = Registry<Mouse>::Retrieve(state);
       mouse->InjectButtonDown(button);
       return 0;
     }},
    {"mouse_up",
     "Injects a mouse-button-up event.",
     {{"button", "mouse button index", "number"}},
     {},
     [](lua_State* state) {
       int button = luaL_checknumber(state, 1);
       auto* mouse = Registry<Mouse>::Retrieve(state);
       mouse->InjectButtonUp(button);
       return 0;
     }},
    {"mouse_move",
     "Sets the synthetic mouse position.",
     {{"x", "x coordinate", "number"}, {"y", "y coordinate", "number"}},
     {},
     [](lua_State* state) {
       float x = luaL_checknumber(state, 1);
       float y = luaL_checknumber(state, 2);
       auto* mouse = Registry<Mouse>::Retrieve(state);
       mouse->SetTestPosition(x, y);
       return 0;
     }},
    {"mouse_wheel",
     "Adds to the synthetic mouse wheel delta for this frame.",
     {{"dx", "horizontal scroll", "number"},
      {"dy", "vertical scroll", "number"}},
     {},
     [](lua_State* state) {
       float dx = luaL_checknumber(state, 1);
       float dy = luaL_checknumber(state, 2);
       auto* mouse = Registry<Mouse>::Retrieve(state);
       mouse->InjectWheel(dx, dy);
       return 0;
     }},
    {"controller_down",
     "Injects a controller button-down event by name.",
     {{"button", "the controller button name", "string"}},
     {},
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       controllers->InjectButtonDown(controllers->StrToButton(name));
       return 0;
     }},
    {"controller_up",
     "Injects a controller button-up event by name.",
     {{"button", "the controller button name", "string"}},
     {},
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       controllers->InjectButtonUp(controllers->StrToButton(name));
       return 0;
     }},
    {"controller_axis",
     "Sets a synthetic controller axis or trigger value.",
     {{"axis", "the axis or trigger name", "string"},
      {"value", "the axis position", "number"}},
     {},
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       int value = luaL_checknumber(state, 2);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       controllers->InjectAxis(controllers->StrToAxisOrTrigger(name), value);
       return 0;
     }},
    {"wait_frames",
     "Yields the test coroutine for the given number of frames.",
     {{"n", "number of frames to wait", "number"}},
     {},
     [](lua_State* state) {
       int n = luaL_optnumber(state, 1, 1);
       if (n < 1) n = 1;
       lua_pushnumber(state, n);
       return lua_yield(state, 1);
     }},
    {"wait_seconds",
     "Yields the test coroutine until the given number of seconds has "
     "elapsed (rounded up to whole frames).",
     {{"seconds", "duration to wait", "number"}},
     {},
     [](lua_State* state) {
       double seconds = luaL_checknumber(state, 1);
       int n = static_cast<int>(std::ceil(seconds / TimeStepInSeconds()));
       if (n < 1) n = 1;
       lua_pushnumber(state, n);
       return lua_yield(state, 1);
     }},
    {"is_active",
     "Returns true if the engine is running under --test (a test coroutine "
     "is driving input).",
     {},
     {{"active", "whether test mode is active", "boolean"}},
     [](lua_State* state) {
       lua_pushboolean(state,
                       Registry<Lua>::Retrieve(state)->TestCoroutineActive());
       return 1;
     }},
    {"assert_true",
     "Errors out the test coroutine if the condition is false.",
     {{"cond", "condition", "boolean"},
      {"msg?", "optional failure message", "string"}},
     {},
     [](lua_State* state) {
       if (lua_toboolean(state, 1)) return 0;
       const char* msg = luaL_optstring(state, 2, "test assertion failed");
       return luaL_error(state, "%s", msg);
     }},
};

}  // namespace

void AddTestLibrary(Lua* lua) { lua->AddLibrary("test", kTestLib); }

LuaLibraryDef GetTestLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"test", kTestLib, std::size(kTestLib)},
  };
  return {kLibs, std::size(kLibs), nullptr, 0};
}

}  // namespace G
