#pragma once
#ifndef _GAME_CONFIG_H
#define _GAME_CONFIG_H

#include <stdint.h>

#include "allocators.h"
#include "error.h"
#include "libraries/sqlite3.h"

namespace G {

struct GameConfig {
  size_t window_width = 1440;
  size_t window_height = 1024;
  size_t msaa_samples = 16;
  int vsync_mode = 1;
  char window_title[512] = {0};
  bool borderless = false;
  bool resizable = true;
  bool centered = true;
  bool fullscreen = false;
  bool enable_opengl_debug = true;
  bool enable_joystick = false;
  bool enable_debug_rendering = true;
  bool nearest_filter = false;  // Use GL_NEAREST for pixel art.
  char org_name[512] = {0};
  char app_name[512] = {0};
  struct Version {
    int major = 0;
    int minor = 1;
  };
  Version version;
};

void LoadConfig(std::string_view json_configuration, GameConfig* config,
                Allocator* allocator);

void LoadConfigFromDatabase(sqlite3* db, GameConfig* config,
                            Allocator* allocator);

// Reads conf.json from disk and parses it into config.
ErrorOr<void> LoadConfigFromFile(const char* path, GameConfig* config,
                                 Allocator* allocator);

}  // namespace G

#endif  // _GAME_CONFIG_H
