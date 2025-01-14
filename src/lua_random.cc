#include "lua_random.h"

#include <random>

#include "libraries/pcg_random.h"

namespace G {
namespace {

constexpr double kRandomRange = 1LL << 32;

const struct LuaApiFunction kRandomLib[] = {
    {"from_seed",
     "Deterministically creates a random number generator from a seed",
     {{"seed", "integer with seed number for the rng"}},
     {{"rng", "random number generator"}},
     [](lua_State* state) {
       auto* handle =
           static_cast<pcg32*>(lua_newuserdata(state, sizeof(pcg64)));
       handle->seed(luaL_checkinteger(state, 1));
       luaL_getmetatable(state, "random_number_generator");
       lua_setmetatable(state, -2);
       return 1;
     }},
    {"non_deterministic",
     "Creates a random number generator from a non deterministic seed",
     {},
     {{"rng", "random number generator"}},
     [](lua_State* state) {
       pcg_extras::seed_seq_from<std::random_device> seed_source;
       auto* handle =
           static_cast<pcg32*>(lua_newuserdata(state, sizeof(pcg64)));
       handle->seed(seed_source);
       luaL_getmetatable(state, "random_number_generator");
       lua_setmetatable(state, -2);
       return 1;
     }},
    {"sample",
     "Samples a random number generator in a range. "
     "If no range is provided it uses 32 bit integers.",
     {{"rng", "rng from `from_seed` or `non_deterministic`"},
      {"start?", "start of the range to sample."},
      {"end?",
       "end of the range to sample. Must be provided if start is provided."}},
     {{"result", "an integer in the range provided"}},
     [](lua_State* state) {
       auto* handle = static_cast<pcg32*>(
           luaL_checkudata(state, 1, "random_number_generator"));
       lua_Number randnum = (*handle)();
       switch (lua_gettop(state)) {
         case 1:
           lua_pushnumber(state, randnum / kRandomRange);
           break;
         case 3: {
           const double start = luaL_checknumber(state, 2);
           const double end = luaL_checknumber(state, 3);
           lua_pushnumber(state,
                          start + (randnum / kRandomRange) * (end - start));
           break;
         }
       }
       return 1;
     }},
    {"pick",
     "Picks an element from a list using a random number generator",
     {{"rng", "rng from `from_seed` or `non_deterministic`"},
      {"list", "list to pick elements from. Must be non empty."}},
     {{"result", "an element from the list"},
      {"list", "list to pick elements from"}},
     [](lua_State* state) {
       if (lua_gettop(state) != 2) {
         LUA_ERROR(state, "Insufficient arguments");
       }
       auto* handle = static_cast<pcg32*>(
           luaL_checkudata(state, 1, "random_number_generator"));
       if (!lua_istable(state, 2)) {
         LUA_ERROR(state, "Did not pass a sequential table");
       }
       if (lua_objlen(state, 2) == 0) {
         LUA_ERROR(state, "Table cannot be empty");
       }
       const double val = (*handle)();
       const double size = lua_objlen(state, 2);
       const double pos = 1 + std::floor((val / kRandomRange) * size);
       lua_rawgeti(state, 2, static_cast<int>(pos));
       return 1;
     }}};

}  // namespace

void AddRandomLibrary(Lua* lua) {
  lua->LoadMetatable("random_number_generator", /*registers=*/nullptr,
                     /*register_count=*/0);
  lua->AddLibrary("random", kRandomLib);
}

}  // namespace G
