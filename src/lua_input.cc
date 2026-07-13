#include "lua_input.h"

#include "actions.h"
#include "input.h"

namespace G {
namespace {

// Maps a mouse button argument to a Mouse button index. Accepts a number
// directly or a string name: "left" (0), "middle"/"mid" (1), "right" (2).
int CheckMouseButton(lua_State* state, int index) {
  if (lua_isnumber(state, index)) return lua_tointeger(state, index);
  std::string_view name = GetLuaString(state, index);
  if (name == "left") return 0;
  if (name == "middle" || name == "mid") return 1;
  if (name == "right") return 2;
  return luaL_error(state, "unknown mouse button: %s", name.data());
}

const struct LuaApiFunction kInputLib[] = {
    {"mouse_position",
     "Returns the current mouse position",
     {},
     {{"x", "x coordinate", "number"}, {"y", "y coordinate", "number"}},
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const FVec2 pos = mouse->GetPosition();
       lua_pushnumber(state, pos.x);
       lua_pushnumber(state, pos.y);
       return 2;
     }},
    {"is_key_down",
     "Returns true if the key is currently held down",
     {{"key", "the key name", "string"}},
     {{"down", "whether the key is down", "boolean"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsDown(keyboard->MapKey(c)));
       return 1;
     }},
    {"is_key_released",
     "Returns true if the key was released this frame",
     {{"key", "the key name", "string"}},
     {{"released", "whether the key was released", "boolean"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsReleased(keyboard->MapKey(c)));
       return 1;
     }},
    {"is_key_pressed",
     "Returns true if the key was pressed this frame",
     {{"key", "the key name", "string"}},
     {{"pressed", "whether the key was pressed", "boolean"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* keyboard = Registry<Keyboard>::Retrieve(state);
       lua_pushboolean(state, keyboard->IsPressed(keyboard->MapKey(c)));
       return 1;
     }},
    {"mouse_wheel",
     "Returns the mouse wheel scroll delta",
     {},
     {{"x", "horizontal scroll", "number"}, {"y", "vertical scroll", "number"}},
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       const FVec2 wheel = mouse->GetWheel();
       lua_pushnumber(state, wheel.x);
       lua_pushnumber(state, wheel.y);
       return 2;
     }},
    {"is_mouse_pressed",
     "Returns true if the mouse button was pressed this frame",
     {{"button",
       "Button number or name (\"left\", \"middle\"/\"mid\", \"right\")",
       "number|string"}},
     {{"pressed", "whether the button was pressed", "boolean"}},
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       lua_pushboolean(state, mouse->IsPressed(CheckMouseButton(state, 1)));
       return 1;
     }},
    {"is_mouse_released",
     "Returns true if the mouse button was released this frame",
     {{"button",
       "Button number or name (\"left\", \"middle\"/\"mid\", \"right\")",
       "number|string"}},
     {{"released", "whether the button was released", "boolean"}},
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       lua_pushboolean(state, mouse->IsReleased(CheckMouseButton(state, 1)));
       return 1;
     }},
    {"is_mouse_down",
     "Returns true if the mouse button is currently held down",
     {{"button",
       "Button number or name (\"left\", \"middle\"/\"mid\", \"right\")",
       "number|string"}},
     {{"down", "whether the button is down", "boolean"}},
     [](lua_State* state) {
       auto* mouse = Registry<Mouse>::Retrieve(state);
       lua_pushboolean(state, mouse->IsDown(CheckMouseButton(state, 1)));
       return 1;
     }},
    {"is_controller_button_pressed",
     "Returns true if the controller button was pressed this frame",
     {{"button", "the controller button name", "string"}},
     {{"pressed", "whether the button was pressed", "boolean"}},
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
     {{"button", "the controller button name", "string"}},
     {{"down", "whether the button is down", "boolean"}},
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
     {{"button", "the controller button name", "string"}},
     {{"released", "whether the button was released", "boolean"}},
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
     {{"axis", "the axis or trigger name", "string"}},
     {{"position", "the axis position", "number"}},
     [](lua_State* state) {
       std::string_view c = GetLuaString(state, 1);
       auto* controllers = Registry<Controllers>::Retrieve(state);
       lua_pushnumber(
           state, controllers->AxisPositions(controllers->StrToAxisOrTrigger(c),
                                             controllers->active_controller()));
       return 1;
     }},
    {"bind",
     "Binds an action name to one or more input sources, replacing any "
     "previous bindings. Sources: \"key:<name>\", "
     "\"mouse:<left|middle|right|0|1|2>\", \"gamepad:<button>\", and "
     "\"touch\" (any finger)",
     {{"action", "the action name", "string"},
      {"bindings", "array of binding strings", "string[]"}},
     {},
     [](lua_State* state) {
       std::string_view action = GetLuaString(state, 1);
       luaL_checktype(state, 2, LUA_TTABLE);
       std::string_view bindings[Actions::kMaxBindingsPerAction];
       const size_t count = lua_objlen(state, 2);
       if (count == 0) {
         return luaL_error(state, "action '%s' needs at least one binding",
                           action.data());
       }
       if (count > Actions::kMaxBindingsPerAction) {
         return luaL_error(state, "too many bindings for action '%s' (max %d)",
                           action.data(),
                           static_cast<int>(Actions::kMaxBindingsPerAction));
       }
       for (size_t i = 0; i < count; ++i) {
         lua_rawgeti(state, 2, static_cast<int>(i + 1));
         size_t len;
         const char* str = luaL_checklstring(state, -1, &len);
         bindings[i] = std::string_view(str, len);
         lua_pop(state, 1);
       }
       auto* actions = Registry<Actions>::Retrieve(state);
       auto result = actions->Bind(
           action, Slice<const std::string_view>(bindings, count));
       if (result.is_error()) {
         return luaL_error(state, "bind '%s': %s", action.data(),
                           result.error().message().data());
       }
       return 0;
     }},
    {"get_bindings",
     "Returns the binding strings for an action, exactly as passed to "
     "bind; an empty table if the action is unbound",
     {{"action", "the action name", "string"}},
     {{"bindings", "array of binding strings", "table"}},
     [](lua_State* state) {
       std::string_view action = GetLuaString(state, 1);
       auto* actions = Registry<Actions>::Retrieve(state);
       std::string_view out[Actions::kMaxBindingsPerAction];
       const size_t count =
           actions->GetBindings(action, out, Actions::kMaxBindingsPerAction);
       lua_createtable(state, static_cast<int>(count), 0);
       for (size_t i = 0; i < count; ++i) {
         lua_pushlstring(state, out[i].data(), out[i].size());
         lua_rawseti(state, -2, static_cast<int>(i + 1));
       }
       return 1;
     }},
    {"is_action_down",
     "Returns true while any binding of the action is down. The action "
     "must have been bound",
     {{"action", "the action name", "string"}},
     {{"down", "whether the action is down", "boolean"}},
     [](lua_State* state) {
       std::string_view action = GetLuaString(state, 1);
       auto* actions = Registry<Actions>::Retrieve(state);
       if (!actions->Has(action)) {
         return luaL_error(state, "unknown action '%s'", action.data());
       }
       lua_pushboolean(state, actions->IsDown(action));
       return 1;
     }},
    {"is_action_pressed",
     "Returns true the frame the action transitions to down. The action "
     "must have been bound",
     {{"action", "the action name", "string"}},
     {{"pressed", "whether the action was pressed this frame", "boolean"}},
     [](lua_State* state) {
       std::string_view action = GetLuaString(state, 1);
       auto* actions = Registry<Actions>::Retrieve(state);
       if (!actions->Has(action)) {
         return luaL_error(state, "unknown action '%s'", action.data());
       }
       lua_pushboolean(state, actions->IsPressed(action));
       return 1;
     }},
    {"is_action_released",
     "Returns true the frame the action transitions to up. The action "
     "must have been bound",
     {{"action", "the action name", "string"}},
     {{"released", "whether the action was released this frame", "boolean"}},
     [](lua_State* state) {
       std::string_view action = GetLuaString(state, 1);
       auto* actions = Registry<Actions>::Retrieve(state);
       if (!actions->Has(action)) {
         return luaL_error(state, "unknown action '%s'", action.data());
       }
       lua_pushboolean(state, actions->IsReleased(action));
       return 1;
     }},
    {"action_time",
     "Returns the seconds the action has been continuously held, or 0 "
     "when it is not down. The action must have been bound",
     {{"action", "the action name", "string"}},
     {{"seconds", "hold duration in seconds", "number"}},
     [](lua_State* state) {
       std::string_view action = GetLuaString(state, 1);
       auto* actions = Registry<Actions>::Retrieve(state);
       if (!actions->Has(action)) {
         return luaL_error(state, "unknown action '%s'", action.data());
       }
       lua_pushnumber(state, actions->DownTime(action));
       return 1;
     }},
    {"touch_count",
     "Returns the number of fingers currently touching the screen",
     {},
     {{"count", "the number of active touches", "number"}},
     [](lua_State* state) {
       auto* touch = Registry<Touch>::Retrieve(state);
       lua_pushinteger(state, static_cast<lua_Integer>(touch->DownCount()));
       return 1;
     }},
    {"touches",
     "Returns the active touches as an array of {id, x, y, pressure} "
     "tables, with positions in viewport coordinates",
     {},
     {{"touches", "array of active touches", "table"}},
     [](lua_State* state) {
       auto* touch = Registry<Touch>::Retrieve(state);
       Touch::Finger fingers[Touch::kMaxFingers];
       const size_t count = touch->GetFingers(fingers, Touch::kMaxFingers);
       lua_createtable(state, static_cast<int>(count), 0);
       for (size_t i = 0; i < count; ++i) {
         lua_createtable(state, 0, 4);
         // Finger ids fit comfortably in a double's 53-bit mantissa.
         lua_pushnumber(state, static_cast<lua_Number>(fingers[i].id));
         lua_setfield(state, -2, "id");
         lua_pushnumber(state, fingers[i].position.x);
         lua_setfield(state, -2, "x");
         lua_pushnumber(state, fingers[i].position.y);
         lua_setfield(state, -2, "y");
         lua_pushnumber(state, fingers[i].pressure);
         lua_setfield(state, -2, "pressure");
         lua_rawseti(state, -2, static_cast<int>(i + 1));
       }
       return 1;
     }},
    {"is_touch_down",
     "Returns true while any finger is touching the screen",
     {},
     {{"down", "whether any finger is down", "boolean"}},
     [](lua_State* state) {
       lua_pushboolean(state, Registry<Touch>::Retrieve(state)->AnyDown());
       return 1;
     }},
    {"is_touch_pressed",
     "Returns true if any finger began touching this frame",
     {},
     {{"pressed", "whether any finger began touching", "boolean"}},
     [](lua_State* state) {
       lua_pushboolean(state, Registry<Touch>::Retrieve(state)->AnyPressed());
       return 1;
     }},
    {"is_touch_released",
     "Returns true if any finger stopped touching this frame",
     {},
     {{"released", "whether any finger stopped touching", "boolean"}},
     [](lua_State* state) {
       lua_pushboolean(state, Registry<Touch>::Retrieve(state)->AnyReleased());
       return 1;
     }}};

}  // namespace

void AddInputLibrary(Lua* lua) { lua->AddLibrary("input", kInputLib); }

LuaLibraryDef GetInputLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"input", kInputLib, std::size(kInputLib)},
  };
  return {kLibs, std::size(kLibs), nullptr, 0};
}

}  // namespace G
