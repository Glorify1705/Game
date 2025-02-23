#include "lua_sound.h"

#include "sound.h"

namespace G {
namespace {

const struct LuaApiFunction kSoundLib[] = {
    {"add_source",
     "Adds an audio source from an asset name",
     {{"name", "name of the asset to play."}},
     {{"source", "a handle for the source"}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       const int result = sound->AddSource(name);
       if (result < 0) {
         LUA_ERROR(state, "Could not find sound ", name);
       }
       lua_pushnumber(state, result);
       return 1;
     }},
    {"play_source",
     "Plays an audio asset on the music channel.",
     {{"name", "name of the sound asset to play."},
      {"repeat?",
       "a number indicating how many times to repeat the music. -1 means loop "
       "forever."}},
     {{}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const int source = luaL_checkinteger(state, 1);
       if (!sound->StartChannel(source)) {
         LUA_ERROR(state, "Could not play source ", source);
       }
       return 0;
     }},
    {"set_volume",
     "Plays an audio asset as a special effect.",
     {{"name", "name of the sound asset to play."}},
     {{}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const int source = luaL_checkinteger(state, 1);
       const double gain = luaL_checknumber(state, 2);
       if (gain < 0) {
         LUA_ERROR(state, "Invalid gain setting ", gain, " - must be positive");
       }
       if (!sound->SetGain(source, gain)) {
         LUA_ERROR(state, "Could not play source ", source);
       }
       return 0;
     }},
    {"stop_source",
     "Stops sound source and rewinds it",
     {{}},
     {{}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const int source = luaL_checkinteger(state, 1);
       if (!sound->Stop(source)) {
         LUA_ERROR(state, "Could not play source ", source);
       }
       return 0;
     }}};

}  // namespace

void AddSoundLibrary(Lua* lua) { lua->AddLibrary("sound", kSoundLib); }

}  // namespace G
