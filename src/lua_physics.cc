#include "lua_physics.h"

#include "physics.h"

namespace G {
namespace {

// Reads a numeric field from a Lua table, returning the default if absent.
float LuaGetNumberField(lua_State* state, int index, const char* field,
                        float fallback) {
  lua_getfield(state, index, field);
  float result = lua_isnumber(state, -1) ? lua_tonumber(state, -1) : fallback;
  lua_pop(state, 1);
  return result;
}

// Reads a boolean field from a Lua table, returning the default if absent.
bool LuaGetBoolField(lua_State* state, int index, const char* field,
                     bool fallback) {
  lua_getfield(state, index, field);
  bool result = lua_isboolean(state, -1) ? lua_toboolean(state, -1) : fallback;
  lua_pop(state, 1);
  return result;
}

// Reads shape options from a Lua table at the given stack index.
// Supports both raw numeric category/mask and string-based names:
//   { category = "player", collides_with = {"meteor", "powerup"} }
PhysicsShapeOptions ReadShapeOptions(lua_State* state, int index) {
  PhysicsShapeOptions opts;
  if (!lua_istable(state, index)) return opts;
  auto* physics = Registry<Physics>::Retrieve(state);
  opts.density = LuaGetNumberField(state, index, "density", opts.density);
  opts.friction = LuaGetNumberField(state, index, "friction", opts.friction);
  opts.restitution =
      LuaGetNumberField(state, index, "restitution", opts.restitution);
  opts.sensor = LuaGetBoolField(state, index, "sensor", opts.sensor);
  lua_getfield(state, index, "category");
  if (lua_isnumber(state, -1)) {
    opts.category = (uint16_t)lua_tointeger(state, -1);
  } else if (lua_isstring(state, -1)) {
    opts.category = physics->ResolveCategory(lua_tostring(state, -1));
  }
  lua_pop(state, 1);
  lua_getfield(state, index, "mask");
  if (lua_isnumber(state, -1)) {
    opts.mask = (uint16_t)lua_tointeger(state, -1);
  }
  lua_pop(state, 1);
  lua_getfield(state, index, "collides_with");
  if (lua_istable(state, -1)) {
    uint16_t mask = 0;
    int len = lua_objlen(state, -1);
    for (int i = 1; i <= len; i++) {
      lua_rawgeti(state, -1, i);
      if (lua_isstring(state, -1)) {
        mask |= physics->ResolveCategory(lua_tostring(state, -1));
      }
      lua_pop(state, 1);
    }
    opts.mask = mask;
  }
  lua_pop(state, 1);
  return opts;
}

const struct LuaApiFunction kPhysicsLib[] = {
    {"add_box",
     "Adds a dynamic box body to the physics world",
     {{"tx", "top-left x", "number"},
      {"ty", "top-left y", "number"},
      {"bx", "bottom-right x", "number"},
      {"by", "bottom-right y", "number"},
      {"angle", "rotation in radians", "number"},
      {"callback", "collision callback function", "function"},
      {"options",
       "optional table: density, friction, restitution, sensor, "
       "category, mask",
       "table"}},
     {{"handle", "a physics handle for the new body", "physics_handle"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const float tx = luaL_checknumber(state, 1);
       const float ty = luaL_checknumber(state, 2);
       const float bx = luaL_checknumber(state, 3);
       const float by = luaL_checknumber(state, 4);
       const float angle = luaL_checknumber(state, 5);
       PhysicsShapeOptions opts = ReadShapeOptions(state, 7);
       auto* handle = static_cast<Physics::Handle*>(
           lua_newuserdata(state, sizeof(Physics::Handle)));
       luaL_getmetatable(state, "physics_handle");
       lua_setmetatable(state, -2);
       lua_pushvalue(state, 6);
       *handle = physics->AddBox(FVec(tx, ty), FVec(bx, by), angle,
                                 luaL_ref(state, LUA_REGISTRYINDEX), opts);
       return 1;
     }},
    {"add_circle",
     "Adds a dynamic circle body to the physics world",
     {{"tx", "center x", "number"},
      {"ty", "center y", "number"},
      {"radius", "the circle radius", "number"},
      {"callback", "collision callback function", "function"},
      {"options",
       "optional table: density, friction, restitution, sensor, "
       "category, mask",
       "table"}},
     {{"handle", "a physics handle for the new body", "physics_handle"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const float tx = luaL_checknumber(state, 1);
       const float ty = luaL_checknumber(state, 2);
       const float radius = luaL_checknumber(state, 3);
       PhysicsShapeOptions opts = ReadShapeOptions(state, 5);
       auto* handle = static_cast<Physics::Handle*>(
           lua_newuserdata(state, sizeof(Physics::Handle)));
       luaL_getmetatable(state, "physics_handle");
       lua_setmetatable(state, -2);
       lua_pushvalue(state, 4);
       *handle = physics->AddCircle(FVec(tx, ty), radius,
                                    luaL_ref(state, LUA_REGISTRYINDEX), opts);
       return 1;
     }},
    {"destroy_handle",
     "Destroys a physics body",
     {{"handle", "the physics handle to destroy", "physics_handle"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       physics->DestroyHandle(*handle);
       return 0;
     }},
    {"set_collision_categories",
     "Registers named collision categories for string-based filtering",
     {{"categories", "array of category name strings (max 16)", "table"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       luaL_checktype(state, 1, LUA_TTABLE);
       int len = lua_objlen(state, 1);
       if (len > 16) {
         LUA_ERROR(state, "Max 16 collision categories");
         return 0;
       }
       std::string_view names[16];
       for (int i = 1; i <= len; i++) {
         lua_rawgeti(state, 1, i);
         names[i - 1] = luaL_checkstring(state, -1);
         lua_pop(state, 1);
       }
       physics->SetCollisionCategories(Slice<std::string_view>(names, len));
       return 0;
     }},
    {"create_ground",
     "Creates a static ground body",
     {{"walls", "if true, add edge walls around the screen (default true)",
       "boolean"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       bool walls = true;
       if (lua_gettop(state) >= 1 && lua_isboolean(state, 1)) {
         walls = lua_toboolean(state, 1);
       }
       physics->CreateGround(walls);
       return 0;
     }},
    {"set_collision_callback",
     "Sets a global callback invoked when two bodies begin contact",
     {{"callback", "function called with two collision callbacks", "function"}},
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
     {{"handle", "the physics handle", "physics_handle"}},
     {{"x", "x position", "number"}, {"y", "y position", "number"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const FVec2 pos = physics->GetPosition(*handle);
       lua_pushnumber(state, pos.x);
       lua_pushnumber(state, pos.y);
       return 2;
     }},
    {"set_position",
     "Teleports a physics body to a new position",
     {{"handle", "the physics handle", "physics_handle"},
      {"x", "new x position", "number"},
      {"y", "new y position", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       physics->SetPosition(*handle, FVec(x, y));
       return 0;
     }},
    {"angle",
     "Returns the rotation angle of a physics body in radians",
     {{"handle", "the physics handle", "physics_handle"}},
     {{"angle", "the angle in radians", "number"}},
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
     {{"handle", "the physics handle", "physics_handle"},
      {"angle", "the angle in radians", "number"}},
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
     {{"handle", "the physics handle", "physics_handle"},
      {"x", "impulse x component", "number"},
      {"y", "impulse y component", "number"}},
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
     {{"handle", "the physics handle", "physics_handle"},
      {"x", "force x component", "number"},
      {"y", "force y component", "number"}},
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
     {{"handle", "the physics handle", "physics_handle"},
      {"torque", "the torque value", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float x = luaL_checknumber(state, 2);
       physics->ApplyTorque(*handle, x);
       return 0;
     }},
    {"linear_velocity",
     "Returns the linear velocity of a physics body in pixels/s",
     {{"handle", "the physics handle", "physics_handle"}},
     {{"vx", "x velocity", "number"}, {"vy", "y velocity", "number"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const FVec2 v = physics->GetLinearVelocity(*handle);
       lua_pushnumber(state, v.x);
       lua_pushnumber(state, v.y);
       return 2;
     }},
    {"set_linear_velocity",
     "Sets the linear velocity of a physics body in pixels/s",
     {{"handle", "the physics handle", "physics_handle"},
      {"vx", "x velocity", "number"},
      {"vy", "y velocity", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float vx = luaL_checknumber(state, 2);
       const float vy = luaL_checknumber(state, 3);
       physics->SetLinearVelocity(*handle, FVec(vx, vy));
       return 0;
     }},
    {"angular_velocity",
     "Returns the angular velocity of a physics body in rad/s",
     {{"handle", "the physics handle", "physics_handle"}},
     {{"omega", "angular velocity", "number"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       lua_pushnumber(state, physics->GetAngularVelocity(*handle));
       return 1;
     }},
    {"set_angular_velocity",
     "Sets the angular velocity of a physics body in rad/s",
     {{"handle", "the physics handle", "physics_handle"},
      {"omega", "angular velocity", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float omega = luaL_checknumber(state, 2);
       physics->SetAngularVelocity(*handle, omega);
       return 0;
     }},
    {"set_fixed_rotation",
     "Prevents or allows a physics body from rotating",
     {{"handle", "the physics handle", "physics_handle"},
      {"fixed", "true to lock rotation", "boolean"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const bool fixed = lua_toboolean(state, 2);
       physics->SetFixedRotation(*handle, fixed);
       return 0;
     }},
    {"get_fixed_rotation",
     "Returns whether a physics body has fixed rotation",
     {{"handle", "the physics handle", "physics_handle"}},
     {{"fixed", "true if rotation is locked", "boolean"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       lua_pushboolean(state, physics->GetFixedRotation(*handle));
       return 1;
     }}};

}  // namespace

void AddPhysicsLibrary(Lua* lua) {
  lua->LoadMetatable("physics_handle", /*registers=*/nullptr,
                     /*register_count=*/0);
  lua->AddLibrary("physics", kPhysicsLib);
  lua->RegisterUserdataType({"physics_handle", "physics_handle",
                             "An opaque handle to a physics body"});
}

}  // namespace G
