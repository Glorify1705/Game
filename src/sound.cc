#include <algorithm>

#include "SDL_mixer.h"
#include "assets.h"
#include "sound.h"

namespace G {

void Sound::Play(const char* file, int times) {
  auto* music = assets_->GetSound(file);
  CHECK(music != nullptr, "Could not find sound ", file);
  SDL_RWops* rwops = SDL_RWFromMem(
      const_cast<void*>(
          reinterpret_cast<const void*>(music->contents()->Data())),
      music->contents()->size());
  auto* mixed = Mix_LoadMUS_RW(rwops, /*freesrc=*/true);
  if (!Mix_PlayingMusic()) {
    CHECK(Mix_PlayMusic(mixed, times) == 0,
          "Could not play sound: ", SDL_GetError());
  }
}

void Sound::SetVolume(float volume) {
  DCHECK(volume >= 0 && volume <= 1, "Invalid volume setting");
  Mix_VolumeMusic(std::clamp(static_cast<int>(volume * 128), 0, 128));
}

void Sound::Stop() { Mix_HaltMusic(); }

}  // namespace G