#pragma once
#ifndef _GAME_SOUND_H
#define _GAME_SOUND_H

#include "SDL_mixer.h"
#include "array.h"
#include "assets.h"
#include "dictionary.h"

namespace G {

class Sound {
 public:
  explicit Sound(Allocator* allocator)
      : chunks_(256, allocator), chunk_by_name_(allocator) {}
  ~Sound();

  inline static constexpr int kLoop = -1;

  void PlayMusic(std::string_view file, int times);

  void PlaySoundEffect(std::string_view file);

  // `volume` is between 0 and 1.
  void SetMusicVolume(float volume);

  // `volume` is between 0 and 1.
  void SetSoundEffectVolume(float volume);

  void LoadSound(DbAssets::Sound* sound);

  void Stop();

 private:
  FixedArray<Mix_Chunk*> chunks_;
  Dictionary<uint32_t> chunk_by_name_;
};

}  // namespace G

#endif  // _GAME_SOUND_H
