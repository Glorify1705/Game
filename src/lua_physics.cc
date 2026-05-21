#include "lua_physics.h"

#include <cmath>
#include <string_view>

#include "physics.h"

namespace G {
namespace {

// Reads the body_type string field from a Lua table.
PhysicsBodyType LuaGetBodyType(lua_State* state, int index) {
  lua_getfield(state, index, "body_type");
  PhysicsBodyType result = PhysicsBodyType::kDynamic;
  if (lua_isstring(state, -1)) {
    std::string_view bt = lua_tostring(state, -1);
    if (bt == "static") {
      result = PhysicsBodyType::kStatic;
    } else if (bt == "kinematic") {
      result = PhysicsBodyType::kKinematic;
    }
  }
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
  opts.body_type = LuaGetBodyType(state, index);
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

JointHandle* CheckJointHandle(lua_State* state, int index) {
  return static_cast<JointHandle*>(
      luaL_checkudata(state, index, "joint_handle"));
}

void PushJointHandle(lua_State* state, JointHandle handle) {
  auto* ud =
      static_cast<JointHandle*>(lua_newuserdata(state, sizeof(JointHandle)));
  luaL_getmetatable(state, "joint_handle");
  lua_setmetatable(state, -2);
  *ud = handle;
}

const char* JointTypeName(b2Joint* j) {
  switch (j->GetType()) {
    case e_revoluteJoint:
      return "revolute";
    case e_distanceJoint:
      return "distance";
    case e_weldJoint:
      return "weld";
    case e_prismaticJoint:
      return "prismatic";
    case e_mouseJoint:
      return "mouse";
    case e_wheelJoint:
      return "wheel";
    case e_frictionJoint:
      return "friction";
    case e_motorJoint:
      return "motor";
    case e_pulleyJoint:
      return "pulley";
    case e_gearJoint:
      return "gear";
    default:
      return "unknown";
  }
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
       const FVec2 tl = CheckVec2(state, 1);
       const FVec2 br = CheckVec2(state, 3);
       const float angle = luaL_checknumber(state, 5);
       PhysicsShapeOptions opts = ReadShapeOptions(state, 7);
       auto* handle = static_cast<Physics::Handle*>(
           lua_newuserdata(state, sizeof(Physics::Handle)));
       luaL_getmetatable(state, "physics_handle");
       lua_setmetatable(state, -2);
       lua_pushvalue(state, 6);
       *handle = physics->AddBox(FVec(tl.x, tl.y), FVec(br.x, br.y), angle,
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
       const FVec2 center = CheckVec2(state, 1);
       const float radius = luaL_checknumber(state, 3);
       PhysicsShapeOptions opts = ReadShapeOptions(state, 5);
       auto* handle = static_cast<Physics::Handle*>(
           lua_newuserdata(state, sizeof(Physics::Handle)));
       luaL_getmetatable(state, "physics_handle");
       lua_setmetatable(state, -2);
       lua_pushvalue(state, 4);
       *handle = physics->AddCircle(FVec(center.x, center.y), radius,
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
    {"on_begin_contact",
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
             auto* ctx = reinterpret_cast<CollisionContext*>(userdata);
             lua_rawgeti(ctx->state, LUA_REGISTRYINDEX, ctx->func_index);
             if (lhs != 0) {
               lua_rawgeti(ctx->state, LUA_REGISTRYINDEX, lhs);
             } else {
               lua_pushnil(ctx->state);
             }
             if (rhs != 0) {
               lua_rawgeti(ctx->state, LUA_REGISTRYINDEX, rhs);
             } else {
               lua_pushnil(ctx->state);
             }
             lua_call(ctx->state, 2, 0);
           },
           context);
       return 0;
     }},
    {"on_end_contact",
     "Sets a global callback invoked when two bodies stop touching. Fires "
     "for sensor exits as well as regular contacts.",
     {{"callback", "function called with the two userdata values", "function"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       if (lua_gettop(state) != 1) {
         LUA_ERROR(state, "Must pass a function as end collision callback");
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
       physics->SetEndContactCallback(
           [](uintptr_t lhs, uintptr_t rhs, void* userdata) {
             auto* ctx = reinterpret_cast<CollisionContext*>(userdata);
             lua_rawgeti(ctx->state, LUA_REGISTRYINDEX, ctx->func_index);
             if (lhs != 0) {
               lua_rawgeti(ctx->state, LUA_REGISTRYINDEX, lhs);
             } else {
               lua_pushnil(ctx->state);
             }
             if (rhs != 0) {
               lua_rawgeti(ctx->state, LUA_REGISTRYINDEX, rhs);
             } else {
               lua_pushnil(ctx->state);
             }
             lua_call(ctx->state, 2, 0);
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
       PushVec2(state, pos);
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
       const FVec2 pos = CheckVec2(state, 2);
       physics->SetPosition(*handle, FVec(pos.x, pos.y));
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
       const FVec2 impulse = CheckVec2(state, 2);
       physics->ApplyLinearImpulse(*handle, FVec(impulse.x, impulse.y));
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
       const FVec2 force = CheckVec2(state, 2);
       physics->ApplyForce(*handle, FVec(force.x, force.y));
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
       PushVec2(state, v);
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
       const FVec2 vel = CheckVec2(state, 2);
       physics->SetLinearVelocity(*handle, FVec(vel.x, vel.y));
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
    {"set_linear_damping",
     "Sets the linear damping (drag) of a physics body",
     {{"handle", "the physics handle", "physics_handle"},
      {"damping", "damping coefficient (>= 0)", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float damping = luaL_checknumber(state, 2);
       physics->SetLinearDamping(*handle, damping);
       return 0;
     }},
    {"set_angular_damping",
     "Sets the angular damping (rotational drag) of a physics body",
     {{"handle", "the physics handle", "physics_handle"},
      {"damping", "damping coefficient (>= 0)", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float damping = luaL_checknumber(state, 2);
       physics->SetAngularDamping(*handle, damping);
       return 0;
     }},
    {"set_gravity_scale",
     "Scales the effect of world gravity on a physics body",
     {{"handle", "the physics handle", "physics_handle"},
      {"scale", "gravity multiplier (0 = none, 1 = normal)", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const float scale = luaL_checknumber(state, 2);
       physics->SetGravityScale(*handle, scale);
       return 0;
     }},
    {"set_bullet",
     "Enables continuous collision detection (CCD) for a fast-moving body",
     {{"handle", "the physics handle", "physics_handle"},
      {"bullet", "true to enable CCD", "boolean"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const bool bullet = lua_toboolean(state, 2);
       physics->SetBullet(*handle, bullet);
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
     }},
    {"set_gravity",
     "Sets the world gravity vector in pixels/s^2",
     {{"gx", "gravity x component", "number"},
      {"gy", "gravity y component", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const FVec2 gravity = CheckVec2(state, 1);
       physics->SetWorldGravity(FVec(gravity.x, gravity.y));
       return 0;
     }},
    {"gravity",
     "Returns the world gravity vector in pixels/s^2",
     {},
     {{"gx", "gravity x component", "number"},
      {"gy", "gravity y component", "number"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       FVec2 g = physics->GetWorldGravity();
       PushVec2(state, g);
       return 2;
     }},
    {"set_iterations",
     "Sets the solver iteration counts per time step",
     {{"velocity", "velocity solver iterations (default 6)", "number"},
      {"position", "position solver iterations (default 2)", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const int vel = luaL_checkinteger(state, 1);
       const int pos = luaL_checkinteger(state, 2);
       physics->SetIterations(vel, pos);
       return 0;
     }},
    {"pixels_per_meter",
     "Returns the pixels-per-meter scale factor",
     {},
     {{"ppm", "the scale factor", "number"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       lua_pushnumber(state, physics->GetPixelsPerMeter());
       return 1;
     }},
    {"raycast",
     "Casts a ray and returns the closest hit, or nil if nothing was hit",
     {{"x1", "ray start x", "number"},
      {"y1", "ray start y", "number"},
      {"x2", "ray end x", "number"},
      {"y2", "ray end y", "number"},
      {"mask", "collision mask filter (default 0xFFFF)", "number"}},
     {{"hit", "table with handle, x, y, nx, ny, fraction fields, or nil",
       "table|nil"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const FVec2 from = CheckVec2(state, 1);
       const FVec2 to = CheckVec2(state, 3);
       uint16_t mask = 0xFFFF;
       if (lua_gettop(state) >= 5 && lua_isnumber(state, 5)) {
         mask = static_cast<uint16_t>(lua_tointeger(state, 5));
       }
       Physics::RaycastHit hit;
       if (physics->Raycast(FVec(from.x, from.y), FVec(to.x, to.y), mask,
                            &hit)) {
         lua_createtable(state, 0, 6);
         auto* handle = static_cast<Physics::Handle*>(
             lua_newuserdata(state, sizeof(Physics::Handle)));
         luaL_getmetatable(state, "physics_handle");
         lua_setmetatable(state, -2);
         *handle = hit.handle;
         lua_setfield(state, -2, "handle");
         lua_pushnumber(state, hit.point.x);
         lua_setfield(state, -2, "x");
         lua_pushnumber(state, hit.point.y);
         lua_setfield(state, -2, "y");
         lua_pushnumber(state, hit.normal.x);
         lua_setfield(state, -2, "nx");
         lua_pushnumber(state, hit.normal.y);
         lua_setfield(state, -2, "ny");
         lua_pushnumber(state, hit.fraction);
         lua_setfield(state, -2, "fraction");
         return 1;
       }
       lua_pushnil(state);
       return 1;
     }},
    {"raycast_all",
     "Casts a ray and returns all hits sorted by distance",
     {{"x1", "ray start x", "number"},
      {"y1", "ray start y", "number"},
      {"x2", "ray end x", "number"},
      {"y2", "ray end y", "number"},
      {"mask", "collision mask filter (default 0xFFFF)", "number"}},
     {{"hits", "array of hit tables", "table"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       const FVec2 from = CheckVec2(state, 1);
       const FVec2 to = CheckVec2(state, 3);
       uint16_t mask = 0xFFFF;
       if (lua_gettop(state) >= 5 && lua_isnumber(state, 5)) {
         mask = static_cast<uint16_t>(lua_tointeger(state, 5));
       }
       constexpr int kMaxHits = 32;
       Physics::RaycastHit hits[kMaxHits];
       int count = physics->RaycastAll(FVec(from.x, from.y), FVec(to.x, to.y),
                                       mask, hits, kMaxHits);
       lua_createtable(state, count, 0);
       for (int i = 0; i < count; i++) {
         lua_createtable(state, 0, 6);
         auto* handle = static_cast<Physics::Handle*>(
             lua_newuserdata(state, sizeof(Physics::Handle)));
         luaL_getmetatable(state, "physics_handle");
         lua_setmetatable(state, -2);
         *handle = hits[i].handle;
         lua_setfield(state, -2, "handle");
         lua_pushnumber(state, hits[i].point.x);
         lua_setfield(state, -2, "x");
         lua_pushnumber(state, hits[i].point.y);
         lua_setfield(state, -2, "y");
         lua_pushnumber(state, hits[i].normal.x);
         lua_setfield(state, -2, "nx");
         lua_pushnumber(state, hits[i].normal.y);
         lua_setfield(state, -2, "ny");
         lua_pushnumber(state, hits[i].fraction);
         lua_setfield(state, -2, "fraction");
         lua_rawseti(state, -2, i + 1);
       }
       return 1;
     }},
    {"create_revolute_joint",
     "Creates a revolute (hinge) joint between two bodies",
     {{"body_a", "first body", "physics_handle"},
      {"body_b", "second body", "physics_handle"},
      {"anchor_x", "world-space anchor x (pixels)", "number"},
      {"anchor_y", "world-space anchor y (pixels)", "number"},
      {"options?",
       "optional: enable_limit, lower_angle, upper_angle, enable_motor, "
       "motor_speed, max_motor_torque, collide_connected",
       "table"}},
     {{"joint", "a joint handle", "joint_handle"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* a = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       auto* b = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 2, "physics_handle"));
       FVec2 anchor = CheckVec2(state, 3);
       bool enable_limit = false;
       float lower_angle = 0, upper_angle = 0;
       bool enable_motor = false;
       float motor_speed = 0, max_motor_torque = 0;
       bool collide_connected = false;
       if (lua_istable(state, 5)) {
         enable_limit = LuaGetBoolField(state, 5, "enable_limit", false);
         lower_angle = LuaGetNumberField(state, 5, "lower_angle", 0);
         upper_angle = LuaGetNumberField(state, 5, "upper_angle", 0);
         enable_motor = LuaGetBoolField(state, 5, "enable_motor", false);
         motor_speed = LuaGetNumberField(state, 5, "motor_speed", 0);
         max_motor_torque = LuaGetNumberField(state, 5, "max_motor_torque", 0);
         collide_connected =
             LuaGetBoolField(state, 5, "collide_connected", false);
       }
       PushJointHandle(state,
                       physics->CreateRevoluteJoint(
                           *a, *b, FVec(anchor.x, anchor.y), enable_limit,
                           lower_angle, upper_angle, enable_motor, motor_speed,
                           max_motor_torque, collide_connected));
       return 1;
     }},
    {"create_distance_joint",
     "Creates a distance (spring) joint between two bodies",
     {{"body_a", "first body", "physics_handle"},
      {"body_b", "second body", "physics_handle"},
      {"ax1", "anchor A x (pixels)", "number"},
      {"ay1", "anchor A y (pixels)", "number"},
      {"ax2", "anchor B x (pixels)", "number"},
      {"ay2", "anchor B y (pixels)", "number"},
      {"options?",
       "optional: length, frequency, damping_ratio, collide_connected",
       "table"}},
     {{"joint", "a joint handle", "joint_handle"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* a = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       auto* b = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 2, "physics_handle"));
       FVec2 anchor_a = CheckVec2(state, 3);
       FVec2 anchor_b = CheckVec2(state, 5);
       float length = -1, frequency = 0, damping_ratio = 0;
       bool collide_connected = false;
       if (lua_istable(state, 7)) {
         length = LuaGetNumberField(state, 7, "length", -1);
         frequency = LuaGetNumberField(state, 7, "frequency", 0);
         damping_ratio = LuaGetNumberField(state, 7, "damping_ratio", 0);
         collide_connected =
             LuaGetBoolField(state, 7, "collide_connected", false);
       }
       PushJointHandle(state, physics->CreateDistanceJoint(
                                  *a, *b, FVec(anchor_a.x, anchor_a.y),
                                  FVec(anchor_b.x, anchor_b.y), length,
                                  frequency, damping_ratio, collide_connected));
       return 1;
     }},
    {"create_weld_joint",
     "Creates a weld (rigid) joint between two bodies",
     {{"body_a", "first body", "physics_handle"},
      {"body_b", "second body", "physics_handle"},
      {"anchor_x", "world-space anchor x (pixels)", "number"},
      {"anchor_y", "world-space anchor y (pixels)", "number"},
      {"options?", "optional: frequency, damping_ratio, collide_connected",
       "table"}},
     {{"joint", "a joint handle", "joint_handle"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* a = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       auto* b = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 2, "physics_handle"));
       FVec2 anchor = CheckVec2(state, 3);
       float frequency = 0, damping_ratio = 0;
       bool collide_connected = false;
       if (lua_istable(state, 5)) {
         frequency = LuaGetNumberField(state, 5, "frequency", 0);
         damping_ratio = LuaGetNumberField(state, 5, "damping_ratio", 0);
         collide_connected =
             LuaGetBoolField(state, 5, "collide_connected", false);
       }
       PushJointHandle(state, physics->CreateWeldJoint(
                                  *a, *b, FVec(anchor.x, anchor.y), frequency,
                                  damping_ratio, collide_connected));
       return 1;
     }},
    {"create_prismatic_joint",
     "Creates a prismatic (slider) joint between two bodies",
     {{"body_a", "first body", "physics_handle"},
      {"body_b", "second body", "physics_handle"},
      {"anchor_x", "world-space anchor x (pixels)", "number"},
      {"anchor_y", "world-space anchor y (pixels)", "number"},
      {"axis_x", "slide axis x component", "number"},
      {"axis_y", "slide axis y component", "number"},
      {"options?",
       "optional: enable_limit, lower_translation, upper_translation, "
       "enable_motor, motor_speed, max_motor_force, collide_connected",
       "table"}},
     {{"joint", "a joint handle", "joint_handle"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* a = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       auto* b = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 2, "physics_handle"));
       FVec2 anchor = CheckVec2(state, 3);
       FVec2 axis = CheckVec2(state, 5);
       bool enable_limit = false;
       float lower = 0, upper = 0;
       bool enable_motor = false;
       float motor_speed = 0, max_motor_force = 0;
       bool collide_connected = false;
       if (lua_istable(state, 7)) {
         enable_limit = LuaGetBoolField(state, 7, "enable_limit", false);
         lower = LuaGetNumberField(state, 7, "lower_translation", 0);
         upper = LuaGetNumberField(state, 7, "upper_translation", 0);
         enable_motor = LuaGetBoolField(state, 7, "enable_motor", false);
         motor_speed = LuaGetNumberField(state, 7, "motor_speed", 0);
         max_motor_force = LuaGetNumberField(state, 7, "max_motor_force", 0);
         collide_connected =
             LuaGetBoolField(state, 7, "collide_connected", false);
       }
       PushJointHandle(
           state, physics->CreatePrismaticJoint(
                      *a, *b, FVec(anchor.x, anchor.y), FVec(axis.x, axis.y),
                      enable_limit, lower, upper, enable_motor, motor_speed,
                      max_motor_force, collide_connected));
       return 1;
     }},
    {"create_mouse_joint",
     "Creates a mouse (drag) joint that pulls a body toward a target point",
     {{"body", "the body to drag", "physics_handle"},
      {"target_x", "initial target x (pixels)", "number"},
      {"target_y", "initial target y (pixels)", "number"},
      {"options?", "optional: max_force, frequency, damping_ratio", "table"}},
     {{"joint", "a joint handle", "joint_handle"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* body = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       FVec2 target = CheckVec2(state, 2);
       float max_force = 1000, frequency = 5.0f, damping_ratio = 0.7f;
       if (lua_istable(state, 4)) {
         max_force = LuaGetNumberField(state, 4, "max_force", max_force);
         frequency = LuaGetNumberField(state, 4, "frequency", frequency);
         damping_ratio =
             LuaGetNumberField(state, 4, "damping_ratio", damping_ratio);
       }
       PushJointHandle(state, physics->CreateLuaMouseJoint(
                                  *body, FVec(target.x, target.y), max_force,
                                  frequency, damping_ratio));
       return 1;
     }},
    {"create_wheel_joint",
     "Creates a wheel (vehicle suspension) joint between two bodies",
     {{"body_a", "chassis body", "physics_handle"},
      {"body_b", "wheel body", "physics_handle"},
      {"anchor_x", "world-space anchor x (pixels)", "number"},
      {"anchor_y", "world-space anchor y (pixels)", "number"},
      {"axis_x", "suspension axis x component", "number"},
      {"axis_y", "suspension axis y component", "number"},
      {"options?",
       "optional: enable_motor, motor_speed, max_motor_torque, frequency, "
       "damping_ratio, collide_connected",
       "table"}},
     {{"joint", "a joint handle", "joint_handle"}},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* a = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       auto* b = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 2, "physics_handle"));
       FVec2 anchor = CheckVec2(state, 3);
       FVec2 axis = CheckVec2(state, 5);
       bool enable_motor = false;
       float motor_speed = 0, max_motor_torque = 0;
       float frequency = 2.0f, damping_ratio = 0.7f;
       bool collide_connected = false;
       if (lua_istable(state, 7)) {
         enable_motor = LuaGetBoolField(state, 7, "enable_motor", false);
         motor_speed = LuaGetNumberField(state, 7, "motor_speed", 0);
         max_motor_torque = LuaGetNumberField(state, 7, "max_motor_torque", 0);
         frequency = LuaGetNumberField(state, 7, "frequency", frequency);
         damping_ratio =
             LuaGetNumberField(state, 7, "damping_ratio", damping_ratio);
         collide_connected =
             LuaGetBoolField(state, 7, "collide_connected", false);
       }
       PushJointHandle(
           state, physics->CreateWheelJoint(
                      *a, *b, FVec(anchor.x, anchor.y), FVec(axis.x, axis.y),
                      enable_motor, motor_speed, max_motor_torque, frequency,
                      damping_ratio, collide_connected));
       return 1;
     }},
    {"set_angle",
     "Sets the absolute rotation angle of a physics body",
     {{"handle", "the physics handle", "physics_handle"},
      {"angle", "angle in radians (0=right, increases counter-clockwise)",
       "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       physics->SetAngle(*handle, luaL_checknumber(state, 2));
       return 0;
     }},
    {"move_toward",
     "Sets velocity to move a body toward a target point at a given speed",
     {{"handle", "the physics handle", "physics_handle"},
      {"target_x", "target x position in pixels", "number"},
      {"target_y", "target y position in pixels", "number"},
      {"speed", "movement speed in pixels/second", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const FVec2 target = CheckVec2(state, 2);
       const float speed = luaL_checknumber(state, 4);
       const FVec2 pos = physics->GetPosition(*handle);
       const float dx = target.x - pos.x;
       const float dy = target.y - pos.y;
       const float dist = std::sqrt(dx * dx + dy * dy);
       if (dist < 0.001f) {
         physics->SetLinearVelocity(*handle, FVec(0, 0));
       } else {
         const float scale = speed / dist;
         physics->SetLinearVelocity(*handle, FVec(dx * scale, dy * scale));
       }
       return 0;
     }},
    {"look_at",
     "Sets a body's angle to face toward a target point",
     {{"handle", "the physics handle", "physics_handle"},
      {"target_x", "target x position in pixels", "number"},
      {"target_y", "target y position in pixels", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const FVec2 target = CheckVec2(state, 2);
       const FVec2 pos = physics->GetPosition(*handle);
       physics->SetAngle(*handle,
                         std::atan2(target.y - pos.y, target.x - pos.x));
       return 0;
     }},
    {"apply_force_world",
     "Applies a continuous force in world coordinates (not body-local)",
     {{"handle", "the physics handle", "physics_handle"},
      {"x", "force x component (world space)", "number"},
      {"y", "force y component (world space)", "number"}},
     {},
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* handle = static_cast<Physics::Handle*>(
           luaL_checkudata(state, 1, "physics_handle"));
       const FVec2 force = CheckVec2(state, 2);
       physics->ApplyForceWorld(*handle, FVec(force.x, force.y));
       return 0;
     }},
};

constexpr luaL_Reg kJointMethods[] = {
    {"destroy",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->DestroyJoint(*h);
       return 0;
     }},
    {"is_valid",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       lua_pushboolean(state, physics->ResolveJoint(*h) != nullptr);
       return 1;
     }},
    {"get_type",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       b2Joint* j = physics->ResolveJoint(*h);
       if (j == nullptr) return luaL_error(state, "invalid joint handle");
       lua_pushstring(state, JointTypeName(j));
       return 1;
     }},
    {"get_joint_angle",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       lua_pushnumber(state, physics->GetJointAngle(*h));
       return 1;
     }},
    {"get_joint_speed",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       lua_pushnumber(state, physics->GetJointSpeed(*h));
       return 1;
     }},
    {"get_joint_translation",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       lua_pushnumber(state, physics->GetJointTranslation(*h));
       return 1;
     }},
    {"get_current_length",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       lua_pushnumber(state, physics->GetCurrentLength(*h));
       return 1;
     }},
    {"set_motor_speed",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->SetMotorSpeed(*h, luaL_checknumber(state, 2));
       return 0;
     }},
    {"enable_motor",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->EnableMotor(*h, lua_toboolean(state, 2));
       return 0;
     }},
    {"enable_limit",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->EnableLimit(*h, lua_toboolean(state, 2));
       return 0;
     }},
    {"set_limits",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       float lower = luaL_checknumber(state, 2);
       float upper = luaL_checknumber(state, 3);
       physics->SetJointLimits(*h, lower, upper);
       return 0;
     }},
    {"set_max_motor_torque",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->SetMaxMotorTorque(*h, luaL_checknumber(state, 2));
       return 0;
     }},
    {"set_max_motor_force",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->SetMaxMotorForce(*h, luaL_checknumber(state, 2));
       return 0;
     }},
    {"set_length",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->SetLength(*h, luaL_checknumber(state, 2));
       return 0;
     }},
    {"set_target",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       FVec2 target = CheckVec2(state, 2);
       physics->SetTarget(*h, FVec(target.x, target.y));
       return 0;
     }},
    {"set_max_force",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->SetMaxForce(*h, luaL_checknumber(state, 2));
       return 0;
     }},
    {"set_frequency",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->SetJointFrequency(*h, luaL_checknumber(state, 2));
       return 0;
     }},
    {"set_damping_ratio",
     [](lua_State* state) {
       auto* physics = Registry<Physics>::Retrieve(state);
       auto* h = CheckJointHandle(state, 1);
       physics->SetJointDampingRatio(*h, luaL_checknumber(state, 2));
       return 0;
     }},
};

const LuaUserdataMethod kJointMethodDefs[] = {
    {"destroy", "Destroys this joint", {}, {}},
    {"is_valid",
     "Returns true if this joint handle is still valid",
     {},
     {{"valid", "whether the joint exists", "boolean"}}},
    {"get_type",
     "Returns the joint type name",
     {},
     {{"type", "joint type string", "string"}}},
    {"get_joint_angle",
     "Returns the revolute joint angle in radians",
     {},
     {{"angle", "angle in radians", "number"}}},
    {"get_joint_speed",
     "Returns the joint speed (revolute: rad/s, prismatic: pixels/s)",
     {},
     {{"speed", "joint speed", "number"}}},
    {"get_joint_translation",
     "Returns the prismatic joint translation in pixels",
     {},
     {{"translation", "translation in pixels", "number"}}},
    {"get_current_length",
     "Returns the current distance joint length in pixels",
     {},
     {{"length", "current length in pixels", "number"}}},
    {"set_motor_speed",
     "Sets the motor speed",
     {{"speed", "motor speed", "number"}},
     {}},
    {"enable_motor",
     "Enables or disables the joint motor",
     {{"enabled", "whether to enable", "boolean"}},
     {}},
    {"enable_limit",
     "Enables or disables joint limits",
     {{"enabled", "whether to enable", "boolean"}},
     {}},
    {"set_limits",
     "Sets joint limits (revolute: radians, prismatic: pixels)",
     {{"lower", "lower limit", "number"}, {"upper", "upper limit", "number"}},
     {}},
    {"set_max_motor_torque",
     "Sets max motor torque (revolute, wheel)",
     {{"torque", "max torque", "number"}},
     {}},
    {"set_max_motor_force",
     "Sets max motor force (prismatic)",
     {{"force", "max force", "number"}},
     {}},
    {"set_length",
     "Sets the rest length (distance joint, pixels)",
     {{"length", "rest length in pixels", "number"}},
     {}},
    {"set_target",
     "Sets the mouse joint target position",
     {{"x", "target x (pixels)", "number"},
      {"y", "target y (pixels)", "number"}},
     {}},
    {"set_max_force",
     "Sets the max force (mouse joint)",
     {{"force", "max force", "number"}},
     {}},
    {"set_frequency",
     "Sets the spring frequency in Hz",
     {{"hz", "frequency in Hz", "number"}},
     {}},
    {"set_damping_ratio",
     "Sets the damping ratio (0-1)",
     {{"ratio", "damping ratio", "number"}},
     {}},
};

}  // namespace

void AddPhysicsLibrary(Lua* lua) {
  lua->LoadMetatable("physics_handle", /*registers=*/nullptr,
                     /*register_count=*/0);
  LOAD_METATABLE(lua, "joint_handle", kJointMethods);
  lua->AddLibrary("physics", kPhysicsLib);
}

LuaLibraryDef GetPhysicsLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"physics", kPhysicsLib, std::size(kPhysicsLib)},
  };
  static const LuaUserdataType kTypes[] = {
      {"physics_handle", "physics_handle",
       "An opaque handle to a physics body"},
      {"joint_handle", "joint_handle", "An opaque handle to a physics joint",
       nullptr, 0, kJointMethodDefs, std::size(kJointMethodDefs)},
  };
  return {kLibs, std::size(kLibs), kTypes, std::size(kTypes)};
}

}  // namespace G
