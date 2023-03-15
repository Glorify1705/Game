#include <cmath>
#include <iostream>

#include "SDL.h"
#include "clock.h"

namespace G {

double NowInSeconds() {
  return (SDL_GetPerformanceCounter() * 1.0) / SDL_GetPerformanceFrequency();
}

}  // namespace G