#pragma once
#ifndef _GAME_CONFIG_H
#define _GAME_CONFIG_H

#include <cstdint>

#include "assets.h"

namespace G {

enum GameModules : uint32_t { VIDEO = 0, SOUND = 1, JOYSTICK = 2 };

struct GameConfig {
  size_t window_width = 1440;
  size_t window_height = 1024;
  size_t msaa_samples = 16;
  int vsync_mode = 1;
  char window_title[512] = {0};
  bool borderless = false;
  bool resizable = true;
  bool centered = true;
  bool enable_opengl_debug = true;
  bool enable_joystick = false;
  bool enable_debug_rendering = true;
  char org_name[512];
  char app_name[512];
  struct Version {
    int major;
    int minor;
  };
  Version version;
};

void LoadConfig(const DbAssets& assets, GameConfig* config,
                Allocator* allocator);

}  // namespace G

#endif  // _GAME_CONFIG_H
