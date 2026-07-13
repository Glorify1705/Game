#pragma once
#ifndef _GAME_ENGINE_H
#define _GAME_ENGINE_H

#include <SDL3/SDL.h>

#include "allocators.h"
#include "assets.h"
#include "camera.h"
#include "config.h"
#include "console.h"
#include "executor.h"
#include "filesystem.h"
#include "hot_reload.h"
#include "input.h"
#include "lua.h"
#include "mimalloc_allocator.h"
#include "network.h"
#include "physics.h"
#include "renderer.h"
#include "save.h"
#include "shaders.h"
#include "sound.h"
#include "timer.h"

namespace G {

// Owns all engine subsystems as direct members. The constructor sets up
// each subsystem in dependency order; Initialize() wires them together
// (Lua bindings, asset loaders, controller DB).
//
// Engine does not own the hot-reload watcher — that is managed by the
// caller (RunGame) so the main loop controls the lifecycle explicitly.
struct Engine {
  Engine(Slice<const char*> args, sqlite3* db_, DbAssets* db_assets,
         const GameConfig& config_, size_t audio_channels,
         size_t audio_buffer_samples, SDL_Window* sdl_window,
         Allocator* allocator);

  ~Engine() = default;

  // Registers Lua bindings and asset loaders, loads assets.
  void Initialize();

  // Resets per-frame state (frame allocator, input devices).
  void StartFrame();

  // Forwards an SDL event to input subsystems and Lua callbacks.
  void HandleEvent(const SDL_Event& event);

  // Resets subsystem state and reloads assets after a hot-reload.
  void Reload(const HotReloadChanges& changes);

  // Subsystems, in initialization order.
  DebugConsole console;
  sqlite3* db;
  DbAssets* assets;
  GameConfig config;
  Filesystem filesystem;
  Save save;
  SDL_Window* window;
  Shaders shaders;
  BatchRenderer batch_renderer;
  Keyboard keyboard;
  Mouse mouse;
  Controllers controllers;
  Touch touch;
  Sound sound;
  Renderer renderer;
  Camera camera;
  MimallocAllocator lua_allocator;
  Lua lua;
  TimerSystem timers;
  Physics physics;
  Network network;
  ArenaAllocator frame_allocator;
  ThreadPoolExecutor pool;
  Allocator* allocator_;

 private:
  // Dispatches SDL input events to the corresponding Lua handlers.
  void ForwardEventToLua(const SDL_Event& event);
};

}  // namespace G

#endif  // _GAME_ENGINE_H
