#include "lua_physics.h"

#include "physics.h"

namespace G {
namespace {

const struct LuaApiFunction kPhysicsLib[] = {
    {"add_box",
     "Adds a dynamic box body to the physics world",
     {{"tx", "top-left x"},
      {"ty", "top-left y"},
      {"bx", "bottom-right x"},
      {"by", "bottom-right y"},
      {"angle", "rotation in radians"},
      {"callback", "collision callback function"}},
     {{"handle", "a physics handle for the new body"}},
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
     "Adds a dynamic circle body to the physics world",
     {{"tx", "center x"},
      {"ty", "center y"},
      {"radius", "the circle radius"},
      {"callback", "collision callback function"}},
     {{"handle", "a physics handle for the new body"}},
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
     "Destroys a physics body",
     {{"handle", "the physics handle to destroy"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       physics->DestroyHandle(*handle);
       return 0;
     }},
    {"create_ground",
     "Creates a static ground body",
     {},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       physics->CreateGround();
       return 0;
     }},
    {"set_collision_callback",
     "Sets a global callback invoked when two bodies begin contact",
     {{"callback", "function called with two collision callbacks"}},
     {},
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
       auto* allocator = Registry<Lua>::Retrieve(state)->allocator();
       auto* context = allocator->BraceInit<CollisionContext>(
           state, luaL_ref(state, LUA_REGISTRYINDEX), allocator);
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
     "Returns the position of a physics body",
     {{"handle", "the physics handle"}},
     {{"x", "x position"}, {"y", "y position"}},
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
     "Returns the rotation angle of a physics body in radians",
     {{"handle", "the physics handle"}},
     {{"angle", "the angle in radians"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float angle = physics->GetAngle(*handle);
       lua_pushnumber(state, angle);
       return 1;
     }},
    {"rotate",
     "Sets the rotation angle of a physics body",
     {{"handle", "the physics handle"}, {"angle", "the angle in radians"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float angle = luaL_checknumber(state, 2);
       physics->Rotate(*handle, angle);
       return 0;
     }},
    {"apply_linear_impulse",
     "Applies a linear impulse to a physics body",
     {{"handle", "the physics handle"},
      {"x", "impulse x component"},
      {"y", "impulse y component"}},
     {},
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
     "Applies a continuous force to a physics body",
     {{"handle", "the physics handle"},
      {"x", "force x component"},
      {"y", "force y component"}},
     {},
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
     "Applies a torque to a physics body",
     {{"handle", "the physics handle"}, {"torque", "the torque value"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float x = luaL_checknumber(state, 2);
       physics->ApplyTorque(*handle, x);
       return 0;
     }}};

}  // namespace

void AddPhysicsLibrary(Lua* lua) {
  lua->LoadMetatable("physics_handle", /*registers=*/nullptr,
                     /*register_count=*/0);
  lua->AddLibrary("physics", kPhysicsLib);
}

}  // namespace G
