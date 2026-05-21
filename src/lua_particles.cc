#include "lua_particles.h"

#include <cmath>

#include "particles.h"
#include "renderer.h"  // BatchRenderer, BlendMode

namespace G {
namespace {

Emitter* CheckEmitter(lua_State* state, int index) {
  return static_cast<Emitter*>(
      luaL_checkudata(state, index, "particle_emitter"));
}

// Helper to read a PropertyRamp from a Lua value at the given index.
// - number: constant
// - table with 2 elements: random range {min, max}
// - table with 3+ elements: ramp stops
PropertyRamp ReadPropertyRamp(lua_State* state, int index) {
  if (lua_isnumber(state, index)) {
    return PropertyRamp::Constant(lua_tonumber(state, index));
  }
  if (!lua_istable(state, index)) return PropertyRamp::Constant(0);

  int len = lua_objlen(state, index);
  if (len == 2) {
    lua_rawgeti(state, index, 1);
    float lo = lua_tonumber(state, -1);
    lua_pop(state, 1);
    lua_rawgeti(state, index, 2);
    float hi = lua_tonumber(state, -1);
    lua_pop(state, 1);
    return PropertyRamp::Range(lo, hi);
  }
  float stops[kMaxRampStops];
  int count = len < kMaxRampStops ? len : kMaxRampStops;
  for (int i = 0; i < count; ++i) {
    lua_rawgeti(state, index, i + 1);
    stops[i] = lua_tonumber(state, -1);
    lua_pop(state, 1);
  }
  return PropertyRamp::Stops(stops, count);
}

// Reads a ColorRamp from a Lua value: table of {r, g, b, a} tables.
ColorRamp ReadColorRamp(lua_State* state, int index) {
  if (!lua_istable(state, index)) return ColorRamp::Constant(Color::White());

  // Check if it's a single color {r, g, b, a} or array of colors.
  lua_rawgeti(state, index, 1);
  bool is_array_of_colors = lua_istable(state, -1);
  lua_pop(state, 1);

  if (!is_array_of_colors) {
    // Single color table {r, g, b, a}.
    Color c = Color::White();
    lua_rawgeti(state, index, 1);
    c.r = static_cast<uint8_t>(lua_tonumber(state, -1) * 255);
    lua_pop(state, 1);
    lua_rawgeti(state, index, 2);
    c.g = static_cast<uint8_t>(lua_tonumber(state, -1) * 255);
    lua_pop(state, 1);
    lua_rawgeti(state, index, 3);
    c.b = static_cast<uint8_t>(lua_tonumber(state, -1) * 255);
    lua_pop(state, 1);
    lua_rawgeti(state, index, 4);
    c.a = lua_isnumber(state, -1)
              ? static_cast<uint8_t>(lua_tonumber(state, -1) * 255)
              : 255;
    lua_pop(state, 1);
    return ColorRamp::Constant(c);
  }

  ColorRamp ramp;
  int len = lua_objlen(state, index);
  ramp.num_stops = len < kMaxRampStops ? len : kMaxRampStops;
  for (int i = 0; i < ramp.num_stops; ++i) {
    lua_rawgeti(state, index, i + 1);
    int ci = lua_gettop(state);
    lua_rawgeti(state, ci, 1);
    ramp.stops[i].r = static_cast<uint8_t>(lua_tonumber(state, -1) * 255);
    lua_pop(state, 1);
    lua_rawgeti(state, ci, 2);
    ramp.stops[i].g = static_cast<uint8_t>(lua_tonumber(state, -1) * 255);
    lua_pop(state, 1);
    lua_rawgeti(state, ci, 3);
    ramp.stops[i].b = static_cast<uint8_t>(lua_tonumber(state, -1) * 255);
    lua_pop(state, 1);
    lua_rawgeti(state, ci, 4);
    ramp.stops[i].a = lua_isnumber(state, -1)
                          ? static_cast<uint8_t>(lua_tonumber(state, -1) * 255)
                          : 255;
    lua_pop(state, 1);
    lua_pop(state, 1);  // pop the inner table
  }
  return ramp;
}

// Helper to read a named table field as a PropertyRamp.
PropertyRamp ReadRampField(lua_State* state, int table_index, const char* name,
                           PropertyRamp default_val) {
  lua_getfield(state, table_index, name);
  PropertyRamp result =
      lua_isnil(state, -1) ? default_val : ReadPropertyRamp(state, -1);
  lua_pop(state, 1);
  return result;
}

// Helper to read an integer field from a table.
int ReadIntField(lua_State* state, int table_index, const char* name,
                 int default_val) {
  lua_getfield(state, table_index, name);
  int result = lua_isnumber(state, -1) ? lua_tointeger(state, -1) : default_val;
  lua_pop(state, 1);
  return result;
}

int PushNewEmitter(lua_State* state) {
  luaL_checktype(state, 1, LUA_TTABLE);
  auto* allocator = Registry<Lua>::Retrieve(state)->allocator();

  EmitterDef def;
  int t = 1;

  def.emission_rate = LuaGetNumberField(state, t, "emission_rate", 0);
  def.max_particles = ReadIntField(state, t, "max_particles", 1000);
  def.initial_speed =
      ReadRampField(state, t, "speed", PropertyRamp::Constant(100));
  def.initial_size = ReadRampField(state, t, "size", PropertyRamp::Constant(4));
  def.initial_spin = ReadRampField(state, t, "spin", PropertyRamp::Constant(0));

  // Lifetime: number or {min, max}.
  lua_getfield(state, t, "lifetime");
  if (lua_isnumber(state, -1)) {
    def.lifetime_min = lua_tonumber(state, -1);
    def.lifetime_max = def.lifetime_min;
  } else if (lua_istable(state, -1)) {
    lua_rawgeti(state, -1, 1);
    def.lifetime_min = lua_tonumber(state, -1);
    lua_pop(state, 1);
    lua_rawgeti(state, -1, 2);
    def.lifetime_max = lua_tonumber(state, -1);
    lua_pop(state, 1);
  }
  lua_pop(state, 1);

  def.direction = LuaGetNumberField(state, t, "direction", 0);
  def.spread = LuaGetNumberField(state, t, "spread", static_cast<float>(M_PI));

  // Emission shape.
  lua_getfield(state, t, "shape");
  if (lua_isstring(state, -1)) {
    std::string_view shape = lua_tostring(state, -1);
    if (shape == "circle")
      def.shape = EmissionShape::kCircle;
    else if (shape == "rect")
      def.shape = EmissionShape::kRect;
  }
  lua_pop(state, 1);
  def.shape_width = LuaGetNumberField(state, t, "shape_width", 0);
  def.shape_height = LuaGetNumberField(state, t, "shape_height", 0);

  // Over-lifetime ramps.
  def.size_over_life =
      ReadRampField(state, t, "size_over_life", PropertyRamp::Constant(1.0f));
  def.speed_over_life =
      ReadRampField(state, t, "speed_over_life", PropertyRamp::Constant(1.0f));
  def.spin_over_life =
      ReadRampField(state, t, "spin_over_life", PropertyRamp::Constant(1.0f));

  // Color over lifetime.
  lua_getfield(state, t, "color_over_life");
  if (!lua_isnil(state, -1)) {
    def.color_over_life = ReadColorRamp(state, -1);
  }
  lua_pop(state, 1);

  // Gravity: {x, y}.
  lua_getfield(state, t, "gravity");
  if (lua_istable(state, -1)) {
    lua_rawgeti(state, -1, 1);
    def.gravity_x = lua_tonumber(state, -1);
    lua_pop(state, 1);
    lua_rawgeti(state, -1, 2);
    def.gravity_y = lua_tonumber(state, -1);
    lua_pop(state, 1);
  }
  lua_pop(state, 1);

  def.damping = LuaGetNumberField(state, t, "damping", 1.0f);

  // Blend mode.
  lua_getfield(state, t, "blend_mode");
  if (lua_isstring(state, -1)) {
    std::string_view mode = lua_tostring(state, -1);
    if (mode == "add" || mode == "additive")
      def.blend_mode = BLEND_ADD;
    else if (mode == "alpha")
      def.blend_mode = BLEND_ALPHA;
    else if (mode == "multiply")
      def.blend_mode = BLEND_MULTIPLY;
    else if (mode == "replace")
      def.blend_mode = BLEND_REPLACE;
  }
  lua_pop(state, 1);

  // Create the emitter as userdata.
  auto* emitter =
      static_cast<Emitter*>(lua_newuserdata(state, sizeof(Emitter)));
  new (emitter) Emitter();
  emitter->Init(def, allocator);

  luaL_getmetatable(state, "particle_emitter");
  lua_setmetatable(state, -2);
  return 1;
}

int EmitterSetPosition(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  e->x = luaL_checknumber(state, 2);
  e->y = luaL_checknumber(state, 3);
  return 0;
}

int EmitterGetPosition(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  lua_pushnumber(state, e->x);
  lua_pushnumber(state, e->y);
  return 2;
}

int EmitterStart(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  e->active = true;
  return 0;
}

int EmitterStop(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  e->active = false;
  return 0;
}

int EmitterUpdate(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  float dt = luaL_checknumber(state, 2);
  e->Update(dt);
  return 0;
}

int EmitterBurst(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  int count = luaL_checkinteger(state, 2);
  if (lua_isnumber(state, 3) && lua_isnumber(state, 4)) {
    float bx = lua_tonumber(state, 3);
    float by = lua_tonumber(state, 4);
    e->BurstAt(count, bx, by);
  } else {
    e->Burst(count);
  }
  return 0;
}

int EmitterDraw(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  auto* br = Registry<BatchRenderer>::Retrieve(state);
  auto* frame_alloc = Registry<ArenaAllocator>::Retrieve(state);

  const ParticlePool& p = e->pool;
  if (p.count == 0) return 0;

  // Allocate instance data from the frame allocator (valid until frame end).
  auto* instances = static_cast<ParticleInstanceData*>(frame_alloc->Alloc(
      p.count * sizeof(ParticleInstanceData), alignof(ParticleInstanceData)));
  CHECK(instances != nullptr, "Failed to allocate particle instance data");

  // Build instance data from SoA particle arrays.
  for (uint32_t i = 0; i < p.count; ++i) {
    instances[i] = {p.x[i], p.y[i], p.size[i], p.angle[i], p.color[i]};
  }

  // Push a single instanced draw command.
  br->DrawParticles(instances, p.count, br->noop_texture(), e->def.blend_mode);
  return 0;
}

int EmitterParticleCount(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  lua_pushinteger(state, e->ParticleCount());
  return 1;
}

int EmitterIsActive(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  lua_pushboolean(state, e->IsActive());
  return 1;
}

int EmitterSetEmissionRate(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  e->def.emission_rate = luaL_checknumber(state, 2);
  return 0;
}

int EmitterSetDirection(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  e->def.direction = luaL_checknumber(state, 2);
  return 0;
}

int EmitterSetSpread(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  e->def.spread = luaL_checknumber(state, 2);
  return 0;
}

int EmitterSetGravity(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  e->def.gravity_x = luaL_checknumber(state, 2);
  e->def.gravity_y = luaL_checknumber(state, 3);
  return 0;
}

int EmitterGc(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  e->Destroy();
  return 0;
}

int EmitterToString(lua_State* state) {
  auto* e = CheckEmitter(state, 1);
  lua_pushfstring(state, "particle_emitter(%d/%d particles)",
                  e->ParticleCount(), e->pool.max_particles);
  return 1;
}

constexpr luaL_Reg kEmitterMethods[] = {
    {"set_position", EmitterSetPosition},
    {"get_position", EmitterGetPosition},
    {"start", EmitterStart},
    {"stop", EmitterStop},
    {"update", EmitterUpdate},
    {"burst", EmitterBurst},
    {"draw", EmitterDraw},
    {"particle_count", EmitterParticleCount},
    {"is_active", EmitterIsActive},
    {"set_emission_rate", EmitterSetEmissionRate},
    {"set_direction", EmitterSetDirection},
    {"set_spread", EmitterSetSpread},
    {"set_gravity", EmitterSetGravity},
    {"__gc", EmitterGc},
    {"__tostring", EmitterToString},
};

const struct LuaApiFunction kParticlesLib[] = {
    {"new_emitter",
     "Creates a new particle emitter from a definition table",
     {{"def", "Emitter definition table", "table"}},
     {{"emitter", "The particle emitter", "particle_emitter"}},
     PushNewEmitter},
};

const LuaUserdataMethod kEmitterMethodDefs[] = {
    {"set_position",
     "Sets the emitter position",
     {{"x", "X position", "number"}, {"y", "Y position", "number"}},
     {}},
    {"get_position",
     "Gets the emitter position",
     {},
     {{"x", "X position", "number"}, {"y", "Y position", "number"}}},
    {"start", "Starts continuous particle emission", {}, {}},
    {"stop", "Stops continuous particle emission", {}, {}},
    {"update",
     "Advances the particle simulation by dt seconds",
     {{"dt", "Time step in seconds", "number"}},
     {}},
    {"burst",
     "Spawns particles immediately",
     {{"count", "Number of particles", "integer"},
      {"x", "Optional x position", "number?"},
      {"y", "Optional y position", "number?"}},
     {}},
    {"draw", "Draws all live particles", {}, {}},
    {"particle_count",
     "Returns the number of live particles",
     {},
     {{"count", "Live particle count", "integer"}}},
    {"is_active",
     "Returns whether the emitter is actively spawning",
     {},
     {{"active", "True if active", "boolean"}}},
    {"set_emission_rate",
     "Changes the emission rate",
     {{"rate", "Particles per second", "number"}},
     {}},
    {"set_direction",
     "Changes the emission direction",
     {{"angle", "Direction in radians", "number"}},
     {}},
    {"set_spread",
     "Changes the emission spread angle",
     {{"spread", "Half-angle in radians", "number"}},
     {}},
    {"set_gravity",
     "Changes the gravity force",
     {{"gx", "Gravity X", "number"}, {"gy", "Gravity Y", "number"}},
     {}},
};

}  // namespace

void AddParticlesLibrary(Lua* lua) {
  LOAD_METATABLE(lua, "particle_emitter", kEmitterMethods);
  lua->AddLibrary("particles", kParticlesLib);
}

LuaLibraryDef GetParticlesLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"particles", kParticlesLib, std::size(kParticlesLib)},
  };
  static const LuaUserdataType kTypes[] = {
      {"particle_emitter", "particle_emitter",
       "A particle emitter that manages a pool of particles", nullptr, 0,
       kEmitterMethodDefs, std::size(kEmitterMethodDefs), nullptr, 0},
  };
  return {kLibs, std::size(kLibs), kTypes, std::size(kTypes)};
}

}  // namespace G
