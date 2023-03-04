#include "sound.h"

#include "SDL_mixer.h"
#include "assets.h"

namespace G {

void Sound::Play(const char* file, int times) {
  auto* music = assets_->GetSound(file);
  CHECK(music != nullptr);
  SDL_RWops* rwops = SDL_RWFromMem(
      const_cast<void*>(
          reinterpret_cast<const void*>(music->contents()->Data())),
      music->contents()->size());
  auto* mixed = Mix_LoadMUS_RW(rwops, /*freesrc=*/true);
  Mix_PlayMusic(mixed, times);
}

void Sound::Stop() { Mix_HaltMusic(); }

}  // namespace G