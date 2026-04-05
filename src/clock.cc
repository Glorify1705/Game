#include "clock.h"

#include <SDL3/SDL.h>

namespace G {

Time Now() { return Clock::now(); }

double NowInSeconds() {
  return (SDL_GetPerformanceCounter() * 1.0) / SDL_GetPerformanceFrequency();
}

}  // namespace G
