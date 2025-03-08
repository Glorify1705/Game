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
       Sound::Source source;
       if (!sound->AddSource(name, &source)) {
         LUA_ERROR(state, "Could not find sound ", name);
       }
       lua_pushnumber(state, source.AsNum());
       return 1;
     }},
    {"play_source",
     "Plays an audio asset on the music channel.",
     {{"name", "name of the sound asset to play."}},
     {{}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = Sound::Source::FromNum(luaL_checkinteger(state, 1));
       if (!sound->StartChannel(source)) {
         LUA_ERROR(state, "Could not play source");
       }
       return 0;
     }},
    {"play",
     "Loads and immediately plays an audio asset on the music channel.",
     {{"name", "name of the sound asset to play."}},
     {{}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       Sound::Source source;
       if (!sound->AddSource(name, &source)) {
         LUA_ERROR(state, "Could not find sound ", name);
       }
       if (!sound->StartChannel(source)) {
         LUA_ERROR(state, "Could not play source");
       }
       return 0;
     }},
    {"set_volume",
     "Plays an audio asset as a special effect.",
     {{"name", "name of the sound asset to play."},
      {"gain", "the gain for the channel, must be a number between 0 and 1"}},
     {{}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = Sound::Source::FromNum(luaL_checkinteger(state, 1));
       const double gain = luaL_checknumber(state, 2);
       if (gain < 0) {
         LUA_ERROR(state, "Invalid gain setting ", gain, " - must be positive");
       }
       if (!sound->SetSourceGain(source, gain)) {
         LUA_ERROR(state, "Could not set volume for source");
       }
       return 0;
     }},
    {"set_global_volume",
     "Sets the global volume",
     {{"gain", "name of the sound asset to play."}},
     {{}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const double gain = luaL_checknumber(state, 1);
       if (gain < 0) {
         LUA_ERROR(state, "Invalid gain setting ", gain, " - must be positive");
       }
       if (gain > 1) {
         LUA_ERROR(state, "Invalid gain setting ", gain,
                   " - must be less than 1");
       }
       sound->SetGlobalGain(gain);
       return 0;
     }},
    {"stop_source",
     "Stops sound source and rewinds it",
     {{}},
     {{}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = Sound::Source::FromNum(luaL_checkinteger(state, 1));
       if (!sound->Stop(source)) {
         LUA_ERROR(state, "Could not stop source");
       }
       return 0;
     }}};

}  // namespace

void AddSoundLibrary(Lua* lua) { lua->AddLibrary("sound", kSoundLib); }

}  // namespace G
