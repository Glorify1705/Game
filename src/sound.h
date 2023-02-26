#pragma once
#ifndef _GAME_SOUND_H
#define _GAME_SOUND_H

#include "assets.h"
#include "sound.h"

class Sound {
 public:
  explicit Sound(const Assets* assets) : assets_(assets) {}

  void Play(const char* file, int repeat);

  void Stop();

 private:
  const Assets* assets_;
};

#endif  // _GAME_SOUND_H