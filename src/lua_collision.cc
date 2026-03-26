#include "lua_collision.h"

#include "collision.h"
#include "collision_world.h"

namespace G {

// --- Handle helpers ---

static void PushHandle(lua_State* state, ColliderHandle h) {
  auto* ud = static_cast<ColliderHandle*>(
      lua_newuserdata(state, sizeof(ColliderHandle)));
  *ud = h;
  luaL_getmetatable(state, "collision_handle");
  lua_setmetatable(state, -2);
}

static ColliderHandle CheckHandle(lua_State* state, int index) {
  return *static_cast<ColliderHandle*>(
      luaL_checkudata(state, index, "collision_handle"));
}

static CollisionWorld* CheckWorld(lua_State* state, int index) {
  return static_cast<CollisionWorld*>(
      luaL_checkudata(state, index, "collision_world"));
}

static CollisionShape* CheckShape(lua_State* state, int index) {
  return static_cast<CollisionShape*>(
      luaL_checkudata(state, index, "collision_shape"));
}

// --- Shape constructors ---

static int collision_circle(lua_State* state) {
  float radius = luaL_checknumber(state, 1);
  auto* shape = static_cast<CollisionShape*>(
      lua_newuserdata(state, sizeof(CollisionShape)));
  *shape = MakeCircle(radius);
  luaL_getmetatable(state, "collision_shape");
  lua_setmetatable(state, -2);
  return 1;
}

static int collision_aabb(lua_State* state) {
  float w = luaL_checknumber(state, 1);
  float h = luaL_checknumber(state, 2);
  auto* shape = static_cast<CollisionShape*>(
      lua_newuserdata(state, sizeof(CollisionShape)));
  *shape = MakeAABB(w, h);
  luaL_getmetatable(state, "collision_shape");
  lua_setmetatable(state, -2);
  return 1;
}

// --- Pure geometry test (no world needed) ---

static int collision_test(lua_State* state) {
  auto* shape_a = CheckShape(state, 1);
  float ax = luaL_checknumber(state, 2);
  float ay = luaL_checknumber(state, 3);
  auto* shape_b = CheckShape(state, 4);
  float bx = luaL_checknumber(state, 5);
  float by = luaL_checknumber(state, 6);

  CollisionResult cr =
      TestShapes(*shape_a, FVec(ax, ay), *shape_b, FVec(bx, by));
  lua_pushboolean(state, cr.hit);
  if (cr.hit) {
    lua_pushnumber(state, cr.normal.x);
    lua_pushnumber(state, cr.normal.y);
    lua_pushnumber(state, cr.depth);
    return 4;
  }
  return 1;
}

// --- World creation ---

static int collision_new_world(lua_State* state) {
  float cell_size = luaL_optnumber(state, 1, 64.0);
  auto* allocator = Registry<Lua>::Retrieve(state)->allocator();

  auto* world = static_cast<CollisionWorld*>(
      lua_newuserdata(state, sizeof(CollisionWorld)));
  new (world) CollisionWorld(cell_size, allocator);

  luaL_getmetatable(state, "collision_world");
  lua_setmetatable(state, -2);
  return 1;
}

// --- World methods ---

static int collision_world_add(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  auto* shape = CheckShape(state, 2);
  float x = luaL_checknumber(state, 3);
  float y = luaL_checknumber(state, 4);

  CollisionFilter filter = {};
  bool is_trigger = false;
  uintptr_t userdata_ref = static_cast<uintptr_t>(LUA_NOREF);

  if (lua_istable(state, 5)) {
    lua_getfield(state, 5, "category");
    if (!lua_isnil(state, -1)) filter.category = lua_tointeger(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, 5, "mask");
    if (!lua_isnil(state, -1)) filter.mask = lua_tointeger(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, 5, "trigger");
    if (!lua_isnil(state, -1)) is_trigger = lua_toboolean(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, 5, "userdata");
    if (!lua_isnil(state, -1)) {
      lua_pushvalue(state, -1);
      userdata_ref = luaL_ref(state, LUA_REGISTRYINDEX);
    }
    lua_pop(state, 1);
  }

  ColliderHandle handle =
      world->Add(*shape, FVec(x, y), filter, is_trigger, userdata_ref);
  PushHandle(state, handle);
  return 1;
}

static int collision_world_remove(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  ColliderHandle handle = CheckHandle(state, 2);

  if (!world->IsValid(handle)) {
    LUA_ERROR(state, "Invalid collision handle");
  }

  // Unref the Lua userdata if it was stored.
  uintptr_t ref = world->GetUserdata(handle);
  int lua_ref = static_cast<int>(ref);
  if (lua_ref != LUA_NOREF && lua_ref != LUA_REFNIL) {
    luaL_unref(state, LUA_REGISTRYINDEX, lua_ref);
  }

  world->Remove(handle);
  return 0;
}

static int collision_world_set_position(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  ColliderHandle handle = CheckHandle(state, 2);
  float x = luaL_checknumber(state, 3);
  float y = luaL_checknumber(state, 4);
  if (!world->IsValid(handle)) {
    LUA_ERROR(state, "Invalid collision handle");
  }
  world->SetPosition(handle, FVec(x, y));
  return 0;
}

static int collision_world_get_position(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  ColliderHandle handle = CheckHandle(state, 2);
  if (!world->IsValid(handle)) {
    LUA_ERROR(state, "Invalid collision handle");
  }
  FVec2 pos = world->GetPosition(handle);
  lua_pushnumber(state, pos.x);
  lua_pushnumber(state, pos.y);
  return 2;
}

static int collision_world_set_shape(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  ColliderHandle handle = CheckHandle(state, 2);
  auto* shape = CheckShape(state, 3);
  if (!world->IsValid(handle)) {
    LUA_ERROR(state, "Invalid collision handle");
  }
  world->SetShape(handle, *shape);
  return 0;
}

static int collision_world_set_filter(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  ColliderHandle handle = CheckHandle(state, 2);
  uint16_t category = luaL_checkinteger(state, 3);
  uint16_t mask = luaL_checkinteger(state, 4);
  if (!world->IsValid(handle)) {
    LUA_ERROR(state, "Invalid collision handle");
  }
  world->SetFilter(handle, {category, mask});
  return 0;
}

static int collision_world_get_userdata(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  ColliderHandle handle = CheckHandle(state, 2);
  if (!world->IsValid(handle)) {
    LUA_ERROR(state, "Invalid collision handle");
  }
  int ref = static_cast<int>(world->GetUserdata(handle));
  if (ref == LUA_NOREF || ref == LUA_REFNIL) {
    lua_pushnil(state);
  } else {
    lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
  }
  return 1;
}

// --- Movement ---

static void PushContacts(lua_State* state,
                         const CollisionWorld::Contact* contacts,
                         uint32_t count) {
  lua_createtable(state, count, 0);
  for (uint32_t i = 0; i < count; ++i) {
    lua_createtable(state, 0, 5);

    PushHandle(state, contacts[i].other);
    lua_setfield(state, -2, "other");

    lua_pushnumber(state, contacts[i].normal.x);
    lua_setfield(state, -2, "nx");
    lua_pushnumber(state, contacts[i].normal.y);
    lua_setfield(state, -2, "ny");
    lua_pushnumber(state, contacts[i].depth);
    lua_setfield(state, -2, "depth");
    lua_pushnumber(state, contacts[i].point.x);
    lua_setfield(state, -2, "tx");
    lua_pushnumber(state, contacts[i].point.y);
    lua_setfield(state, -2, "ty");

    lua_rawseti(state, -2, i + 1);
  }
}

static int collision_world_move_and_slide(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  ColliderHandle handle = CheckHandle(state, 2);
  float vx = luaL_checknumber(state, 3);
  float vy = luaL_checknumber(state, 4);
  if (!world->IsValid(handle)) {
    LUA_ERROR(state, "Invalid collision handle");
  }

  CollisionWorld::MoveResult result = world->MoveAndSlide(handle, FVec(vx, vy));
  lua_pushnumber(state, result.position.x);
  lua_pushnumber(state, result.position.y);
  PushContacts(state, result.contacts, result.contact_count);
  return 3;
}

static int collision_world_move_and_collide(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  ColliderHandle handle = CheckHandle(state, 2);
  float vx = luaL_checknumber(state, 3);
  float vy = luaL_checknumber(state, 4);
  if (!world->IsValid(handle)) {
    LUA_ERROR(state, "Invalid collision handle");
  }

  CollisionWorld::MoveResult result =
      world->MoveAndCollide(handle, FVec(vx, vy));
  lua_pushnumber(state, result.position.x);
  lua_pushnumber(state, result.position.y);
  if (result.contact_count > 0) {
    lua_createtable(state, 0, 5);
    PushHandle(state, result.contacts[0].other);
    lua_setfield(state, -2, "other");
    lua_pushnumber(state, result.contacts[0].normal.x);
    lua_setfield(state, -2, "nx");
    lua_pushnumber(state, result.contacts[0].normal.y);
    lua_setfield(state, -2, "ny");
    lua_pushnumber(state, result.contacts[0].depth);
    lua_setfield(state, -2, "depth");
    lua_pushnumber(state, result.contacts[0].point.x);
    lua_setfield(state, -2, "tx");
    lua_pushnumber(state, result.contacts[0].point.y);
    lua_setfield(state, -2, "ty");
  } else {
    lua_pushnil(state);
  }
  return 3;
}

// --- Overlap queries ---

static int collision_world_get_overlaps(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  ColliderHandle handle = CheckHandle(state, 2);
  if (!world->IsValid(handle)) {
    LUA_ERROR(state, "Invalid collision handle");
  }

  CollisionWorld::OverlapResult results[64];
  uint32_t count = world->GetOverlaps(handle, results, 64);

  lua_createtable(state, count, 0);
  for (uint32_t i = 0; i < count; ++i) {
    lua_createtable(state, 0, 4);
    PushHandle(state, results[i].handle);
    lua_setfield(state, -2, "other");
    lua_pushnumber(state, results[i].normal.x);
    lua_setfield(state, -2, "nx");
    lua_pushnumber(state, results[i].normal.y);
    lua_setfield(state, -2, "ny");
    lua_pushnumber(state, results[i].depth);
    lua_setfield(state, -2, "depth");
    lua_rawseti(state, -2, i + 1);
  }
  return 1;
}

// --- Spatial queries ---

static int collision_world_raycast(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  float ox = luaL_checknumber(state, 2);
  float oy = luaL_checknumber(state, 3);
  float dx = luaL_checknumber(state, 4);
  float dy = luaL_checknumber(state, 5);
  float max_dist = luaL_checknumber(state, 6);
  uint16_t mask = luaL_optinteger(state, 7, 0xFFFF);

  CollisionWorld::RaycastHit hit;
  if (world->Raycast(FVec(ox, oy), FVec(dx, dy), max_dist, mask, &hit)) {
    lua_createtable(state, 0, 6);
    PushHandle(state, hit.handle);
    lua_setfield(state, -2, "handle");
    lua_pushnumber(state, hit.point.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, hit.point.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, hit.normal.x);
    lua_setfield(state, -2, "nx");
    lua_pushnumber(state, hit.normal.y);
    lua_setfield(state, -2, "ny");
    lua_pushnumber(state, hit.t);
    lua_setfield(state, -2, "t");
  } else {
    lua_pushnil(state);
  }
  return 1;
}

static int collision_world_raycast_all(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  float ox = luaL_checknumber(state, 2);
  float oy = luaL_checknumber(state, 3);
  float dx = luaL_checknumber(state, 4);
  float dy = luaL_checknumber(state, 5);
  float max_dist = luaL_checknumber(state, 6);
  uint16_t mask = luaL_optinteger(state, 7, 0xFFFF);

  CollisionWorld::RaycastHit hits[64];
  uint32_t count =
      world->RaycastAll(FVec(ox, oy), FVec(dx, dy), max_dist, mask, hits, 64);

  lua_createtable(state, count, 0);
  for (uint32_t i = 0; i < count; ++i) {
    lua_createtable(state, 0, 6);
    PushHandle(state, hits[i].handle);
    lua_setfield(state, -2, "handle");
    lua_pushnumber(state, hits[i].point.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, hits[i].point.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, hits[i].normal.x);
    lua_setfield(state, -2, "nx");
    lua_pushnumber(state, hits[i].normal.y);
    lua_setfield(state, -2, "ny");
    lua_pushnumber(state, hits[i].t);
    lua_setfield(state, -2, "t");
    lua_rawseti(state, -2, i + 1);
  }
  return 1;
}

static int collision_world_query_point(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  float x = luaL_checknumber(state, 2);
  float y = luaL_checknumber(state, 3);
  uint16_t mask = luaL_optinteger(state, 4, 0xFFFF);

  ColliderHandle results[64];
  uint32_t count = world->QueryPoint(FVec(x, y), mask, results, 64);

  lua_createtable(state, count, 0);
  for (uint32_t i = 0; i < count; ++i) {
    PushHandle(state, results[i]);
    lua_rawseti(state, -2, i + 1);
  }
  return 1;
}

static int collision_world_query_rect(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  float x1 = luaL_checknumber(state, 2);
  float y1 = luaL_checknumber(state, 3);
  float x2 = luaL_checknumber(state, 4);
  float y2 = luaL_checknumber(state, 5);
  uint16_t mask = luaL_optinteger(state, 6, 0xFFFF);

  ColliderHandle results[64];
  uint32_t count =
      world->QueryRect(FVec(x1, y1), FVec(x2, y2), mask, results, 64);

  lua_createtable(state, count, 0);
  for (uint32_t i = 0; i < count; ++i) {
    PushHandle(state, results[i]);
    lua_rawseti(state, -2, i + 1);
  }
  return 1;
}

static int collision_world_query_circle(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  float cx = luaL_checknumber(state, 2);
  float cy = luaL_checknumber(state, 3);
  float radius = luaL_checknumber(state, 4);
  uint16_t mask = luaL_optinteger(state, 5, 0xFFFF);

  ColliderHandle results[64];
  uint32_t count = world->QueryCircle(FVec(cx, cy), radius, mask, results, 64);

  lua_createtable(state, count, 0);
  for (uint32_t i = 0; i < count; ++i) {
    PushHandle(state, results[i]);
    lua_rawseti(state, -2, i + 1);
  }
  return 1;
}

// --- Trigger callbacks ---

static int collision_world_on_trigger_enter(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  luaL_checktype(state, 2, LUA_TFUNCTION);
  if (world->trigger_enter_ref != LUA_NOREF) {
    luaL_unref(state, LUA_REGISTRYINDEX, world->trigger_enter_ref);
  }
  lua_pushvalue(state, 2);
  world->trigger_enter_ref = luaL_ref(state, LUA_REGISTRYINDEX);
  return 0;
}

static int collision_world_on_trigger_exit(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  luaL_checktype(state, 2, LUA_TFUNCTION);
  if (world->trigger_exit_ref != LUA_NOREF) {
    luaL_unref(state, LUA_REGISTRYINDEX, world->trigger_exit_ref);
  }
  lua_pushvalue(state, 2);
  world->trigger_exit_ref = luaL_ref(state, LUA_REGISTRYINDEX);
  return 0;
}

// --- Update (calls C++ Update and fires Lua trigger callbacks) ---

static int collision_world_update(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  world->Update();

  // Fire trigger enter callbacks.
  if (world->trigger_enter_ref != LUA_NOREF && world->new_trigger_count() > 0) {
    for (uint32_t i = 0; i < world->new_trigger_count(); ++i) {
      const auto& pair = world->new_trigger_pairs()[i];
      lua_rawgeti(state, LUA_REGISTRYINDEX, world->trigger_enter_ref);
      PushHandle(state, world->HandleFor(pair.a));
      PushHandle(state, world->HandleFor(pair.b));
      lua_call(state, 2, 0);
    }
  }

  // Fire trigger exit callbacks.
  if (world->trigger_exit_ref != LUA_NOREF && world->lost_trigger_count() > 0) {
    for (uint32_t i = 0; i < world->lost_trigger_count(); ++i) {
      const auto& pair = world->lost_trigger_pairs()[i];
      lua_rawgeti(state, LUA_REGISTRYINDEX, world->trigger_exit_ref);
      PushHandle(state, world->HandleFor(pair.a));
      PushHandle(state, world->HandleFor(pair.b));
      lua_call(state, 2, 0);
    }
  }

  return 0;
}

// --- __gc (first usage of __gc in this codebase) ---

static int collision_world_gc(lua_State* state) {
  auto* world = CheckWorld(state, 1);

  // Unref all active collider userdata values.
  for (uint32_t i = 0; i < world->collider_capacity(); ++i) {
    if (world->IsActiveSlot(i)) {
      int ref = static_cast<int>(world->GetSlotUserdata(i));
      if (ref != LUA_NOREF && ref != LUA_REFNIL) {
        luaL_unref(state, LUA_REGISTRYINDEX, ref);
      }
    }
  }

  // Unref trigger callbacks.
  if (world->trigger_enter_ref != LUA_NOREF) {
    luaL_unref(state, LUA_REGISTRYINDEX, world->trigger_enter_ref);
  }
  if (world->trigger_exit_ref != LUA_NOREF) {
    luaL_unref(state, LUA_REGISTRYINDEX, world->trigger_exit_ref);
  }

  // Call destructor to free internal allocations (collider array, spatial hash
  // arrays, trigger pair arrays). All go back to mimalloc.
  world->~CollisionWorld();
  return 0;
}

static int collision_world_tostring(lua_State* state) {
  auto* world = CheckWorld(state, 1);
  lua_pushfstring(state, "collision_world(%d colliders)",
                  world->active_count());
  return 1;
}

// --- Handle methods ---

static int collision_handle_eq(lua_State* state) {
  ColliderHandle a = CheckHandle(state, 1);
  ColliderHandle b = CheckHandle(state, 2);
  lua_pushboolean(state, a == b);
  return 1;
}

static int collision_handle_tostring(lua_State* state) {
  ColliderHandle h = CheckHandle(state, 1);
  lua_pushfstring(state, "collision_handle(%d:%d)", h.index, h.generation);
  return 1;
}

// --- Shape methods ---

static int collision_shape_tostring(lua_State* state) {
  auto* shape = CheckShape(state, 1);
  switch (shape->type) {
    case CollisionShapeType::kCircle:
      lua_pushfstring(state, "circle(r=%f)", shape->circle.radius);
      break;
    case CollisionShapeType::kAABB:
      lua_pushfstring(state, "aabb(%fx%f)", shape->aabb.half_w * 2.0f,
                      shape->aabb.half_h * 2.0f);
      break;
  }
  return 1;
}

// --- Metatable registrations ---

static const luaL_Reg kCollisionWorldMethods[] = {
    {"add", collision_world_add},
    {"remove", collision_world_remove},
    {"set_position", collision_world_set_position},
    {"get_position", collision_world_get_position},
    {"set_shape", collision_world_set_shape},
    {"set_filter", collision_world_set_filter},
    {"get_userdata", collision_world_get_userdata},
    {"move_and_slide", collision_world_move_and_slide},
    {"move_and_collide", collision_world_move_and_collide},
    {"get_overlaps", collision_world_get_overlaps},
    {"raycast", collision_world_raycast},
    {"raycast_all", collision_world_raycast_all},
    {"query_point", collision_world_query_point},
    {"query_rect", collision_world_query_rect},
    {"query_circle", collision_world_query_circle},
    {"on_trigger_enter", collision_world_on_trigger_enter},
    {"on_trigger_exit", collision_world_on_trigger_exit},
    {"update", collision_world_update},
    {"__gc", collision_world_gc},
    {"__tostring", collision_world_tostring},
};

static const luaL_Reg kCollisionHandleMethods[] = {
    {"__eq", collision_handle_eq},
    {"__tostring", collision_handle_tostring},
};

static const luaL_Reg kCollisionShapeMethods[] = {
    {"__tostring", collision_shape_tostring},
};

// --- Library functions (G.collision namespace) ---

const struct LuaApiFunction kCollisionLib[] = {
    {"new_world",
     "Creates a new collision world",
     {{"cell_size", "Spatial hash cell size in pixels (default 64)",
       "number?"}},
     {{"world", "The collision world", "collision_world"}},
     collision_new_world},
    {"circle",
     "Creates a circle collision shape",
     {{"radius", "Circle radius in pixels", "number"}},
     {{"shape", "The collision shape", "collision_shape"}},
     collision_circle},
    {"aabb",
     "Creates an axis-aligned bounding box collision shape",
     {{"width", "Box width in pixels", "number"},
      {"height", "Box height in pixels", "number"}},
     {{"shape", "The collision shape", "collision_shape"}},
     collision_aabb},
    {"test",
     "Tests two shapes for collision without needing a world",
     {{"shape_a", "First shape", "collision_shape"},
      {"ax", "First shape x position", "number"},
      {"ay", "First shape y position", "number"},
      {"shape_b", "Second shape", "collision_shape"},
      {"bx", "Second shape x position", "number"},
      {"by", "Second shape y position", "number"}},
     {{"hit", "Whether the shapes overlap", "boolean"},
      {"nx", "Collision normal x (if hit)", "number?"},
      {"ny", "Collision normal y (if hit)", "number?"},
      {"depth", "Penetration depth (if hit)", "number?"}},
     collision_test},
};

// --- Registration ---

static const LuaUserdataMethod kWorldMethods[] = {
    {"add",
     "Adds a collider to the world",
     {{"shape", "Collision shape", "collision_shape"},
      {"x", "X position", "number"},
      {"y", "Y position", "number"},
      {"opts", "Options table: category, mask, trigger, userdata", "table?"}},
     {{"handle", "Handle to the new collider", "collision_handle"}}},
    {"remove",
     "Removes a collider from the world",
     {{"handle", "Handle to remove", "collision_handle"}},
     {}},
    {"set_position",
     "Sets the position of a collider",
     {{"handle", "Collider handle", "collision_handle"},
      {"x", "X position", "number"},
      {"y", "Y position", "number"}},
     {}},
    {"get_position",
     "Gets the position of a collider",
     {{"handle", "Collider handle", "collision_handle"}},
     {{"x", "X position", "number"}, {"y", "Y position", "number"}}},
    {"set_shape",
     "Sets the shape of a collider",
     {{"handle", "Collider handle", "collision_handle"},
      {"shape", "New shape", "collision_shape"}},
     {}},
    {"set_filter",
     "Sets collision filter for a collider",
     {{"handle", "Collider handle", "collision_handle"},
      {"category", "Category bitmask", "integer"},
      {"mask", "Detection mask", "integer"}},
     {}},
    {"get_userdata",
     "Gets the userdata associated with a collider",
     {{"handle", "Collider handle", "collision_handle"}},
     {{"userdata", "The stored userdata value", "any"}}},
    {"move_and_slide",
     "Moves a collider with sliding collision resolution",
     {{"handle", "Collider handle", "collision_handle"},
      {"vx", "X velocity", "number"},
      {"vy", "Y velocity", "number"}},
     {{"x", "Final x position", "number"},
      {"y", "Final y position", "number"},
      {"contacts", "Array of contact info", "table"}}},
    {"move_and_collide",
     "Moves a collider until first collision",
     {{"handle", "Collider handle", "collision_handle"},
      {"vx", "X velocity", "number"},
      {"vy", "Y velocity", "number"}},
     {{"x", "Final x position", "number"},
      {"y", "Final y position", "number"},
      {"contact", "Contact info or nil", "table?"}}},
    {"get_overlaps",
     "Gets all shapes overlapping this collider",
     {{"handle", "Collider handle", "collision_handle"}},
     {{"overlaps", "Array of overlap info", "table"}}},
    {"raycast",
     "Casts a ray and returns the closest hit",
     {{"ox", "Origin x", "number"},
      {"oy", "Origin y", "number"},
      {"dx", "Direction x", "number"},
      {"dy", "Direction y", "number"},
      {"max_dist", "Maximum distance", "number"},
      {"mask", "Filter mask (default 0xFFFF)", "integer?"}},
     {{"hit", "Hit info or nil", "table?"}}},
    {"raycast_all",
     "Casts a ray and returns all hits sorted by distance",
     {{"ox", "Origin x", "number"},
      {"oy", "Origin y", "number"},
      {"dx", "Direction x", "number"},
      {"dy", "Direction y", "number"},
      {"max_dist", "Maximum distance", "number"},
      {"mask", "Filter mask (default 0xFFFF)", "integer?"}},
     {{"hits", "Array of hit info sorted by t", "table"}}},
    {"query_point",
     "Finds all colliders containing a point",
     {{"x", "Point x", "number"},
      {"y", "Point y", "number"},
      {"mask", "Filter mask (default 0xFFFF)", "integer?"}},
     {{"handles", "Array of collider handles", "table"}}},
    {"query_rect",
     "Finds all colliders overlapping a rectangle",
     {{"x1", "Min x", "number"},
      {"y1", "Min y", "number"},
      {"x2", "Max x", "number"},
      {"y2", "Max y", "number"},
      {"mask", "Filter mask (default 0xFFFF)", "integer?"}},
     {{"handles", "Array of collider handles", "table"}}},
    {"query_circle",
     "Finds all colliders overlapping a circle",
     {{"cx", "Center x", "number"},
      {"cy", "Center y", "number"},
      {"radius", "Circle radius", "number"},
      {"mask", "Filter mask (default 0xFFFF)", "integer?"}},
     {{"handles", "Array of collider handles", "table"}}},
    {"on_trigger_enter",
     "Sets callback for trigger enter events",
     {{"fn", "Callback function(handle_a, handle_b)", "function"}},
     {}},
    {"on_trigger_exit",
     "Sets callback for trigger exit events",
     {{"fn", "Callback function(handle_a, handle_b)", "function"}},
     {}},
    {"update",
     "Updates the collision world (rebuilds broad phase, fires triggers)",
     {},
     {}},
};

void AddCollisionLibrary(Lua* lua) {
  lua->LoadMetatable("collision_world", kCollisionWorldMethods);
  lua->LoadMetatable("collision_handle", kCollisionHandleMethods);
  lua->LoadMetatable("collision_shape", kCollisionShapeMethods);
  lua->AddLibrary("collision", kCollisionLib);
  lua->RegisterUserdataType({"collision_world", "collision_world",
                             "A collision detection world with spatial hashing",
                             nullptr, 0, kWorldMethods,
                             std::size(kWorldMethods), nullptr, 0});
  lua->RegisterUserdataType(
      {"collision_handle", "collision_handle",
       "An opaque handle to a collider in a collision world"});
  lua->RegisterUserdataType({"collision_shape", "collision_shape",
                             "A collision shape (circle or AABB)"});
}

}  // namespace G
