#pragma once
#ifndef _GAME_THREAD_H
#define _GAME_THREAD_H

#include "SDL_thread.h"

namespace G {

struct LockMutex {
  explicit LockMutex(SDL_mutex* mutex) : mu(mutex) { SDL_LockMutex(mu); }

  ~LockMutex() {
    if (mu) SDL_UnlockMutex(mu);
  }

  SDL_mutex* Release() {
    SDL_mutex* result = mu;
    SDL_UnlockMutex(mu);
    mu = nullptr;
    return result;
  }

  SDL_mutex* mu;
};

}  // namespace G

#endif  // _GAME_THREAD_H