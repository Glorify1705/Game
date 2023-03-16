#include <algorithm>

#include "SDL_mixer.h"
#include "assets.h"
#include "sound.h"

namespace G {

void Sound::PlayMusic(std::string_view file, int times) {
  Mix_Music* mixed = nullptr;
  if (!music_by_name_.Lookup(file, &mixed)) {
    auto* music = assets_->GetSound(file);
    CHECK(music != nullptr, "Could not find sound ", file);
    SDL_RWops* rwops = SDL_RWFromMem(
        const_cast<void*>(
            reinterpret_cast<const void*>(music->contents()->Data())),
        music->contents()->size());
    mixed = Mix_LoadMUS_RW(rwops, /*freesrc=*/true);
    musics_.Push(mixed);
    music_by_name_.Insert(file, mixed);
  }
  DCHECK(mixed != nullptr);
  if (!Mix_PlayingMusic()) {
    DCHECK(Mix_PlayMusic(mixed, times) == 0, "Could not play sound ", file,
           ": ", SDL_GetError());
  }
}

Sound::~Sound() {
  Stop();
  for (auto* music : musics_) Mix_FreeMusic(music);
  for (auto* chunk : chunks_) Mix_FreeChunk(chunk);
}

void Sound::PlaySoundEffect(std::string_view file) {
  Mix_Chunk* chunk = nullptr;
  if (!chunk_by_name_.Lookup(file, &chunk)) {
    auto* sound = assets_->GetSound(file);
    CHECK(sound != nullptr, "Could not find sound ", file);
    SDL_RWops* rwops = SDL_RWFromMem(
        const_cast<void*>(
            reinterpret_cast<const void*>(sound->contents()->Data())),
        sound->contents()->size());
    chunk = Mix_LoadWAV_RW(rwops, /*freesrc=*/true);
    chunks_.Push(chunk);
    chunk_by_name_.Insert(file, chunk);
  }
  DCHECK(chunk != nullptr);
  Mix_PlayChannel(-1, chunk, 0);
}

void Sound::SetMusicVolume(float volume) {
  DCHECK(volume >= 0 && volume <= 1, "Invalid volume setting");
  Mix_VolumeMusic(std::clamp(static_cast<int>(volume * 128), 0, 128));
}

void Sound::SetSoundEffectVolume(float volume) {
  DCHECK(volume >= 0 && volume <= 1, "Invalid volume setting");
  Mix_MasterVolume(std::clamp(static_cast<int>(volume * 128), 0, 128));
}

void Sound::Stop() {
  if (Mix_PlayingMusic()) Mix_HaltMusic();
  Mix_HaltGroup(-1);
}

}  // namespace G