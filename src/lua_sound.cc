#include "lua_sound.h"

#include "sound.h"

namespace G {
namespace {

const struct LuaApiFunction kSoundLib[] = {
    {"play_music",
     "Plays an audio asset using the SDL mixer music channel.",
     {{"name", "name of the sound asset to play."},
      {"repeat?",
       "a number indicating how many times to repeat the music. -1 means loop "
       "forever."}},
     {{}},
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* sound = Registry<Sound>::Retrieve(state);
       int repeat = Sound::kLoop;
       const int num_args = lua_gettop(state);
       if (num_args == 2) repeat = luaL_checknumber(state, 2);
       if (!sound->PlayMusic(name, repeat)) {
         LUA_ERROR(state, "Could not find music asset ", name);
       }
       return 0;
     }},
    {"play_sfx",
     "Plays an audio asset using the SDL mixer music SFX.",
     {{"name", "name of the sound asset to play."}},
     {{}},
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->PlaySoundEffect(name);
       if (!sound->PlaySoundEffect(name)) {
         LUA_ERROR(state, "Could not find music asset ", name);
       }
       return 0;
     }},
    {"stop",
     "Stops sound",
     {{}},
     {{}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->Stop();
       return 0;
     }},
    {"set_music_volume",
     "Sets the volume of the music channel",
     {{"volume", "Volume as a float between 0 and 1"}},
     {{}},
     [](lua_State* state) {
       const float volume = luaL_checknumber(state, 1);
       if (volume < 0 || volume > 1) {
         LUA_ERROR(state, "Invalid volume ", volume, " must be in [0, 1)",
                   volume);
         return 0;
       }
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->SetMusicVolume(volume);
       return 0;
     }},
    {"set_sfx_volume",
     "Sets the volume of the sound effects channel",
     {{"volume", "Volume as a float between 0 and 1"}},
     {{}},
     [](lua_State* state) {
       const float volume = luaL_checknumber(state, 1);
       if (volume < 0 || volume > 1) {
         LUA_ERROR(state, "Invalid volume ", volume, " must be in [0, 1)");
         return 0;
       }
       auto* sound = Registry<Sound>::Retrieve(state);
       sound->SetSoundEffectVolume(volume);
       return 0;
     }}};

}

void AddSoundLibrary(Lua* lua) { lua->AddLibrary("sound", kSoundLib); }

}  // namespace G
