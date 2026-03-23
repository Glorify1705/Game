#include "lua_sound.h"

#include "sound.h"

namespace G {
namespace {

const struct LuaApiFunction kSoundLib[] = {
    {"add_source",
     "Adds an audio source from an asset name",
     {{"name", "name of the asset to play.", "string"}},
     {{"source", "a handle for the source", "integer"}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       Sound::Source source;
       if (!sound->AddSource(name, &source)) {
         LUA_ERROR(state, "Could not add sound source ", name);
       }
       lua_pushnumber(state, source);
       return 1;
     }},
    {"play_source",
     "Plays an audio asset on the music channel.",
     {{"source", "source id of the sound to play", "integer"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = luaL_checkinteger(state, 1);
       if (!sound->StartChannel(source)) {
         LUA_ERROR(state, "Could not play source");
       }
       return 0;
     }},
    {"play",
     "Loads and immediately plays an audio asset on the music channel.",
     {{"name", "name of the sound asset to play.", "string"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       Sound::Source source;
       if (!sound->AddSource(name, &source, Sound::Ownership::kAutoFree)) {
         LUA_ERROR(state, "Could not add sound source ", name);
       }
       if (!sound->StartChannel(source)) {
         LUA_ERROR(state, "Could not play source");
       }
       return 0;
     }},
    {"set_volume",
     "Sets the volume a source",
     {{"source", "source id of the asset to modify", "integer"},
      {"gain", "the gain for the channel, must be a number between 0 and 1",
       "number"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = luaL_checkinteger(state, 1);
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
     {{"gain", "the global gain between 0 and 1", "number"}},
     {},
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
     {{"source", "source id to stop", "integer"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = luaL_checkinteger(state, 1);
       if (!sound->Stop(source)) {
         LUA_ERROR(state, "Could not stop source");
       }
       return 0;
     }},
    {"add_effect",
     "Adds a sound effect (fully decoded upfront for low-latency playback).",
     {{"name", "name of the sound asset.", "string"}},
     {{"source", "a handle for the source", "integer"}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       Sound::Source source;
       if (!sound->AddEffect(name, &source)) {
         LUA_ERROR(state, "Could not add sound effect ", name);
       }
       lua_pushnumber(state, source);
       return 1;
     }},
    {"play_effect",
     "Loads, decodes, and immediately plays a sound effect.",
     {{"name", "name of the sound asset.", "string"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       Sound::Source source;
       if (!sound->AddEffect(name, &source)) {
         LUA_ERROR(state, "Could not add sound effect ", name);
       }
       if (!sound->StartChannel(source)) {
         LUA_ERROR(state, "Could not play effect");
       }
       return 0;
     }},
    {"set_loop",
     "Enables or disables looping for a source.",
     {{"source", "source id to modify", "integer"},
      {"loop", "true to enable looping, false to disable", "boolean"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = luaL_checkinteger(state, 1);
       const bool loop = lua_toboolean(state, 2);
       if (!sound->SetLoop(source, loop)) {
         LUA_ERROR(state, "Could not set loop for source");
       }
       return 0;
     }},
    {"pause",
     "Pauses a source without rewinding it.",
     {{"source", "source id to pause", "integer"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = luaL_checkinteger(state, 1);
       if (!sound->Pause(source)) {
         LUA_ERROR(state, "Could not pause source");
       }
       return 0;
     }},
    {"resume",
     "Resumes a paused source from where it stopped.",
     {{"source", "source id to resume", "integer"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = luaL_checkinteger(state, 1);
       if (!sound->Resume(source)) {
         LUA_ERROR(state, "Could not resume source");
       }
       return 0;
     }},
    {"is_playing",
     "Returns whether a source is currently playing.",
     {{"source", "source id to query", "integer"}},
     {{"playing", "true if the source is playing", "boolean"}},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = luaL_checkinteger(state, 1);
       lua_pushboolean(state, sound->IsPlaying(source));
       return 1;
     }},
    {"set_pitch",
     "Sets the playback pitch/speed for a source.",
     {{"source", "source id to modify", "integer"},
      {"pitch", "pitch multiplier (0.25 to 4.0, 1.0 = normal)", "number"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = luaL_checkinteger(state, 1);
       const float pitch = luaL_checknumber(state, 2);
       if (!sound->SetPitch(source, pitch)) {
         LUA_ERROR(state, "Could not set pitch for source");
       }
       return 0;
     }},
    {"set_pan",
     "Sets the stereo panning for a source.",
     {{"source", "source id to modify", "integer"},
      {"pan", "pan position (-1.0 = left, 0.0 = center, 1.0 = right)",
       "number"}},
     {},
     [](lua_State* state) {
       auto* sound = Registry<Sound>::Retrieve(state);
       const auto source = luaL_checkinteger(state, 1);
       const float pan = luaL_checknumber(state, 2);
       if (!sound->SetPan(source, pan)) {
         LUA_ERROR(state, "Could not set pan for source");
       }
       return 0;
     }}};

}  // namespace

void AddSoundLibrary(Lua* lua) { lua->AddLibrary("sound", kSoundLib); }

}  // namespace G
