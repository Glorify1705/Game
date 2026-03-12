#include "lua_input.h"

#include "input.h"

namespace G {
namespace {

const struct LuaApiFunction kInputLib[] = {
    {"mouse_position",
     "Returns the current mouse position",
     {},
     {{"x", "x coordinate"}, {"y", "y coordinate"}},
     [](lua_State* state) {
       const FVec2 pos = Mouse::GetPosition();
       lua_pushnumber(state, pos.x);
       lua_pushnumber(state, pos.y);
       return 2;
     }},
    {"is_key_down",
     "Returns true if the key is currently held down",
     {{"key", "the key name"}},
     {{"down", "whether the key is down"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsDown(keyboard->MapKey(c)));
       return 1;
     }},
    {"is_key_released",
     "Returns true if the key was released this frame",
     {{"key", "the key name"}},
     {{"released", "whether the key was released"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsReleased(keyboard->MapKey(c)));
       return 1;
     }},
    {"is_key_pressed",
     "Returns true if the key was pressed this frame",
     {{"key", "the key name"}},
     {{"pressed", "whether the key was pressed"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsPressed(keyboard->MapKey(c)));
       return 1;
     }},
    {"mouse_wheel",
     "Returns the mouse wheel scroll delta",
     {},
     {{"x", "horizontal scroll"}, {"y", "vertical scroll"}},
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const FVec2 wheel = mouse->GetWheel();
       lua_pushnumber(state, wheel.x);
       lua_pushnumber(state, wheel.y);
       return 2;
     }},
    {"is_mouse_pressed",
     "Returns true if the mouse button was pressed this frame",
     {{"button", "the mouse button number"}},
     {{"pressed", "whether the button was pressed"}},
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const auto button = luaL_checknumber(state, 1);
       lua_pushboolean(state, mouse->IsPressed(button));
       return 1;
     }},
    {"is_mouse_released",
     "Returns true if the mouse button was released this frame",
     {{"button", "the mouse button number"}},
     {{"released", "whether the button was released"}},
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const auto button = luaL_checknumber(state, 1);
       lua_pushboolean(state, mouse->IsReleased(button));
       return 1;
     }},
    {"is_mouse_down",
     "Returns true if the mouse button is currently held down",
     {{"button", "the mouse button number"}},
     {{"down", "whether the button is down"}},
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const auto button = luaL_checknumber(state, 1);
       lua_pushboolean(state, mouse->IsDown(button));
       return 1;
     }},
    {"is_controller_button_pressed",
     "Returns true if the controller button was pressed this frame",
     {{"button", "the controller button name"}},
     {{"pressed", "whether the button was pressed"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushboolean(
           state, controllers->IsPressed(controllers->StrToButton(c),
                                         controllers->active_controller()));
       return 1;
     }},
    {"is_controller_button_down",
     "Returns true if the controller button is currently held down",
     {{"button", "the controller button name"}},
     {{"down", "whether the button is down"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushboolean(state,
                       controllers->IsDown(controllers->StrToButton(c),
                                           controllers->active_controller()));
       return 1;
     }},
    {"is_controller_button_released",
     "Returns true if the controller button was released this frame",
     {{"button", "the controller button name"}},
     {{"released", "whether the button was released"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushboolean(
           state, controllers->IsReleased(controllers->StrToButton(c),
                                          controllers->active_controller()));
       return 1;
     }},
    {"get_controller_axis",
     "Returns the current position of a controller axis or trigger",
     {{"axis", "the axis or trigger name"}},
     {{"position", "the axis position"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushnumber(
           state, controllers->AxisPositions(controllers->StrToAxisOrTrigger(c),
                                             controllers->active_controller()));
       return 1;
     }}};

}  // namespace

void AddInputLibrary(Lua* lua) { lua->AddLibrary("input", kInputLib); }

}  // namespace G
