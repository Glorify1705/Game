#include <algorithm>

#include "SDL_mixer.h"
#include "assets.h"
#include "sound.h"

namespace G {

void Sound::PlayMusic(std::string_view file, int times) {
  uint32_t handle;
  if (!music_by_name_.Lookup(file, &handle)) {
    handle = musics_.size();
    auto* music = assets_->GetSound(file);
    CHECK(music != nullptr, "Could not find sound ", file);
    SDL_RWops* rwops = SDL_RWFromMem(
        const_cast<void*>(
            reinterpret_cast<const void*>(music->contents()->Data())),
        music->contents()->size());
    auto* mixed = Mix_LoadMUS_RW(rwops, /*freesrc=*/true);
    music_by_name_.Insert(file, handle);
    musics_.Push(mixed);
  }
  auto* mixed = musics_[handle];
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
  uint32_t handle;
  if (!chunk_by_name_.Lookup(file, &handle)) {
    handle = chunks_.size();
    auto* sound = assets_->GetSound(file);
    CHECK(sound != nullptr, "Could not find sound ", file);
    SDL_RWops* rwops = SDL_RWFromMem(
        const_cast<void*>(
            reinterpret_cast<const void*>(sound->contents()->Data())),
        sound->contents()->size());
    auto* chunk = Mix_LoadWAV_RW(rwops, /*freesrc=*/true);
    music_by_name_.Insert(file, handle);
    chunks_.Push(chunk);
  }
  auto* chunk = chunks_[handle];
  DCHECK(chunk != nullptr);
  Mix_PlayChannel(-1, chunk, 0);
}

void Sound::SetMusicVolume(float volume) {
  DCHECK(volume >= 0 && volume <= 1, "Invalid volume setting");
  Mix_VolumeMusic(std::clamp(static_cast<int>(volume * 128), 0, 128));
}

void Sound::SetSoundEffectVolume(float volume) {
  DCHECK(volume >= 0 && volume <= 1, "Invalid volume setting");
  Mix_Volume(-1, std::clamp(static_cast<int>(volume * 128), 0, 128));
}

void Sound::Stop() {
  if (Mix_PlayingMusic()) Mix_HaltMusic();
  Mix_HaltGroup(-1);
}

}  // namespace G
