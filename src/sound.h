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
  explicit Sound(const Assets* assets, Allocator* allocator)
      : assets_(assets),
        chunks_(allocator),
        musics_(allocator),
        chunk_by_name_(allocator),
        music_by_name_(allocator) {}
  ~Sound();

  inline static constexpr int kLoop = -1;

  void PlayMusic(std::string_view file, int times);

  void PlaySoundEffect(std::string_view file);

  // `volume` is between 0 and 1.
  void SetMusicVolume(float volume);

  // `volume` is between 0 and 1.
  void SetSoundEffectVolume(float volume);

  void Stop();

 private:
  const Assets* assets_;
  FixedArray<Mix_Chunk*, 128> chunks_;
  FixedArray<Mix_Music*, 128> musics_;
  Dictionary<uint32_t> chunk_by_name_;
  Dictionary<uint32_t> music_by_name_;
};

}  // namespace G

#endif  // _GAME_SOUND_H
