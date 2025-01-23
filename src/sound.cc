#include "sound.h"

#include <algorithm>

#include "SDL_mixer.h"
#include "assets.h"

namespace G {

void Sound::PlayMusic(std::string_view file, int times) {
  uint32_t handle;
  if (!chunk_by_name_.Lookup(file, &handle)) {
    LOG("No sound called ", file);
    return;
  }
  auto* mixed = chunks_[handle];
  if (!Mix_PlayingMusic()) {
    DCHECK(Mix_PlayChannel(handle, mixed, times) == 0, "Could not play sound ",
           file, ": ", SDL_GetError());
  }
}

Sound::~Sound() {
  Stop();
  for (auto* chunk : chunks_) Mix_FreeChunk(chunk);
}

void Sound::PlaySoundEffect(std::string_view file) {
  uint32_t handle;
  if (!chunk_by_name_.Lookup(file, &handle)) {
    LOG("No sound called ", file);
    return;
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

void Sound::LoadSound(DbAssets::Sound* sound) {
  (void)sound;
  return;
  uint32_t handle = chunks_.size();
  SDL_RWops* rwops = SDL_RWFromMem(
      const_cast<void*>(reinterpret_cast<const void*>(sound->contents)),
      sound->size);
  auto* chunk = Mix_LoadWAV_RW(rwops, /*freesrc=*/true);
  chunk_by_name_.Insert(sound->name, handle);
  chunks_.Push(chunk);
}

}  // namespace G
