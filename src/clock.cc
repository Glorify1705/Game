#include "clock.h"

#include <cmath>
#include <iostream>

#include "SDL.h"
#include "clock.h"
double NowInMillis() {
  return ((SDL_GetPerformanceCounter() * 1.0) / SDL_GetPerformanceFrequency()) *
         1000.0;
}