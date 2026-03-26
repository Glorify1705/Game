#pragma once
#ifndef _GAME_THREAD_H
#define _GAME_THREAD_H

#include <SDL3/SDL_mutex.h>

namespace G {

struct LockMutex {
  explicit LockMutex(SDL_Mutex* mutex) : mu(mutex) { SDL_LockMutex(mu); }

  ~LockMutex() {
    if (mu) SDL_UnlockMutex(mu);
  }

  SDL_Mutex* Release() {
    SDL_Mutex* result = mu;
    SDL_UnlockMutex(mu);
    mu = nullptr;
    return result;
  }

  SDL_Mutex* mu;
};

}  // namespace G

#endif  // _GAME_THREAD_H
