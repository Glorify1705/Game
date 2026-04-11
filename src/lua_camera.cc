#include "lua_camera.h"

#include "camera.h"
#include "input.h"
#include "renderer.h"

namespace G {
namespace {

const struct LuaApiFunction kCameraLib[] = {
    {"set",
     "Sets the camera position in world coordinates.",
     {{"x", "x world coordinate", "number"},
      {"y", "y world coordinate", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->SetPosition(luaL_checknumber(state, 1),
                           luaL_checknumber(state, 2));
       return 0;
     }},
    {"get",
     "Returns the current camera position in world coordinates.",
     {},
     {{"x", "x world coordinate", "number"},
      {"y", "y world coordinate", "number"}},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       FVec2 pos = camera->GetPosition();
       lua_pushnumber(state, pos.x);
       lua_pushnumber(state, pos.y);
       return 2;
     }},
    {"move",
     "Moves the camera by a delta in world coordinates.",
     {{"dx", "x offset", "number"}, {"dy", "y offset", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->Move(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       return 0;
     }},
    {"set_zoom",
     "Sets the camera zoom level. Values > 1 zoom in, < 1 zoom out.",
     {{"zoom", "zoom factor", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->SetZoom(luaL_checknumber(state, 1));
       return 0;
     }},
    {"get_zoom",
     "Returns the current zoom level.",
     {},
     {{"zoom", "zoom factor", "number"}},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       lua_pushnumber(state, camera->GetZoom());
       return 1;
     }},
    {"set_rotation",
     "Sets the camera rotation in radians.",
     {{"angle", "rotation angle in radians", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->SetRotation(luaL_checknumber(state, 1));
       return 0;
     }},
    {"get_rotation",
     "Returns the current camera rotation in radians.",
     {},
     {{"angle", "rotation angle in radians", "number"}},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       lua_pushnumber(state, camera->GetRotation());
       return 1;
     }},
    {"follow",
     "Sets the position the camera should follow. Call each frame with the "
     "target's position.",
     {{"x", "target x world coordinate", "number"},
      {"y", "target y world coordinate", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->Follow(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       return 0;
     }},
    {"set_lerp",
     "Sets the smoothing factor for following. 0 = no movement, 1 = instant. "
     "Values around 0.05-0.15 give a smooth feel.",
     {{"lx", "horizontal smoothing factor (0-1)", "number"},
      {"ly", "vertical smoothing factor (0-1)", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->SetLerp(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       return 0;
     }},
    {"unfollow",
     "Stops following the target. Camera stays at its current position.",
     {},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->Unfollow();
       return 0;
     }},
    {"set_deadzone",
     "Sets a deadzone rectangle as a fraction of the viewport (0-1). "
     "The target can move within this zone without the camera following.",
     {{"half_w", "half-width as fraction of viewport width (0-1)", "number"},
      {"half_h", "half-height as fraction of viewport height (0-1)", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->SetDeadzone(luaL_checknumber(state, 1),
                           luaL_checknumber(state, 2));
       return 0;
     }},
    {"clear_deadzone",
     "Removes the deadzone so the camera tracks the target directly.",
     {},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->ClearDeadzone();
       return 0;
     }},
    {"set_bounds",
     "Sets world bounds that the camera viewport cannot exceed.",
     {{"x", "left edge of bounds", "number"},
      {"y", "top edge of bounds", "number"},
      {"w", "width of bounds", "number"},
      {"h", "height of bounds", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->SetBounds(luaL_checknumber(state, 1), luaL_checknumber(state, 2),
                         luaL_checknumber(state, 3),
                         luaL_checknumber(state, 4));
       return 0;
     }},
    {"clear_bounds",
     "Removes world bounds so the camera can scroll freely.",
     {},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       camera->ClearBounds();
       return 0;
     }},
    {"shake",
     "Starts a screen shake effect with the given intensity and duration.",
     {{"intensity", "shake amplitude in pixels", "number"},
      {"duration", "shake duration in seconds", "number"},
      {"frequency?", "oscillation frequency (default 8)", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       float intensity = luaL_checknumber(state, 1);
       float duration = luaL_checknumber(state, 2);
       float frequency = luaL_optnumber(state, 3, 8.0);
       camera->Shake(intensity, duration, frequency);
       return 0;
     }},
    {"to_world",
     "Converts screen coordinates to world coordinates.",
     {{"screen_x", "x position on screen", "number"},
      {"screen_y", "y position on screen", "number"}},
     {{"world_x", "x position in world", "number"},
      {"world_y", "y position in world", "number"}},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       FVec2 screen(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       IVec2 vp = renderer->viewport();
       FVec2 world = camera->ToWorld(screen, FVec2(vp.x, vp.y));
       lua_pushnumber(state, world.x);
       lua_pushnumber(state, world.y);
       return 2;
     }},
    {"to_screen",
     "Converts world coordinates to screen coordinates.",
     {{"world_x", "x position in world", "number"},
      {"world_y", "y position in world", "number"}},
     {{"screen_x", "x position on screen", "number"},
      {"screen_y", "y position on screen", "number"}},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       FVec2 world(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       IVec2 vp = renderer->viewport();
       FVec2 screen = camera->ToScreen(world, FVec2(vp.x, vp.y));
       lua_pushnumber(state, screen.x);
       lua_pushnumber(state, screen.y);
       return 2;
     }},
    {"mouse_world",
     "Returns the mouse position in world coordinates.",
     {},
     {{"world_x", "x position in world", "number"},
      {"world_y", "y position in world", "number"}},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       auto* mouse_input = Registry<Mouse>::Retrieve(state);
       FVec2 mouse = mouse_input->GetPosition();
       IVec2 vp = renderer->viewport();
       FVec2 world = camera->ToWorld(mouse, FVec2(vp.x, vp.y));
       lua_pushnumber(state, world.x);
       lua_pushnumber(state, world.y);
       return 2;
     }},
    {"attach",
     "Pushes the camera transform onto the render stack. Everything drawn "
     "after this will be in camera space. Optional parallax factors (0-1) "
     "make layers scroll slower for depth effect.",
     {{"parallax_x?", "horizontal parallax factor (default 1.0)", "number"},
      {"parallax_y?", "vertical parallax factor (default 1.0)", "number"}},
     {},
     [](lua_State* state) {
       auto* camera = Registry<Camera>::Retrieve(state);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       float px = luaL_optnumber(state, 1, 1.0);
       float py = luaL_optnumber(state, 2, 1.0);
       IVec2 vp = renderer->viewport();
       FMat4x4 view = camera->GetViewMatrix(FVec2(vp.x, vp.y), FVec2(px, py));
       renderer->Push();
       renderer->ApplyTransform(view);
       return 0;
     }},
    {"detach",
     "Pops the camera transform from the render stack. Drawing returns to "
     "screen space.",
     {},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Pop();
       return 0;
     }},
};

}  // namespace

void AddCameraLibrary(Lua* lua) { lua->AddLibrary("camera", kCameraLib); }

LuaLibraryDef GetCameraLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"camera", kCameraLib, std::size(kCameraLib)},
  };
  return {kLibs, std::size(kLibs), nullptr, 0};
}

}  // namespace G
