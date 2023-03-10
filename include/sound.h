#pragma once
#ifndef _GAME_SOUND_H
#define _GAME_SOUND_H

#include "assets.h"
#include "sound.h"

namespace G {

class Sound {
 public:
  explicit Sound(const Assets* assets) : assets_(assets) {}
  ~Sound() { Stop(); }

  inline static constexpr int kLoop = -1;

  void Play(const char* file, int repeat);

  // `volume` is between 0 and 1.
  void SetVolume(float volume);

  void Stop();

 private:
  const Assets* assets_;
};

}  // namespace G

#endif  // _GAME_SOUND_H