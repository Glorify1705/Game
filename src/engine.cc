#include "engine.h"

#include <SDL3/SDL.h>

#include "clock.h"
#include "lua_assets.h"
#include "lua_bytebuffer.h"
#include "lua_camera.h"
#include "lua_collision.h"
#include "lua_data.h"
#include "lua_filesystem.h"
#include "lua_graphics.h"
#include "lua_input.h"
#include "lua_json.h"
#include "lua_log.h"
#include "lua_math.h"
#include "lua_network.h"
#include "lua_particles.h"
#include "lua_physics.h"
#include "lua_random.h"
#include "lua_save.h"
#include "lua_scene.h"
#include "lua_sound.h"
#include "lua_system.h"
#include "lua_test.h"
#include "lua_tilemap.h"
#include "lua_timer.h"
#include "memory_budgets.h"
#include "platform.h"
#include "sdl_init.h"
#include "units.h"
#include "vec.h"

namespace G {

Engine::Engine(Slice<const char*> args, sqlite3* db_, DbAssets* db_assets,
               const GameConfig& config_, size_t audio_channels,
               size_t audio_buffer_samples, SDL_Window* sdl_window,
               Allocator* allocator)
    : console(allocator),
      db(db_),
      assets(db_assets),
      config(config_),
      filesystem(allocator),
      save(allocator),
      window(sdl_window),
      shaders(allocator),
      batch_renderer(GetWindowViewport(sdl_window), &shaders, allocator),
      keyboard(allocator),
      controllers(allocator),
      sound(audio_channels, audio_buffer_samples, allocator),
      renderer(*db_assets, &batch_renderer, db, allocator),
      lua_allocator(allocator->Alloc(kLuaArenaSize, kMaxAlign), kLuaArenaSize),
      lua(args, db, db_assets, &lua_allocator),
      physics(FVec(config.window_width, config.window_height),
              Physics::kPixelsPerMeter, allocator),
      network(allocator),
      frame_allocator(allocator, kFrameArenaSize),
      pool(allocator, PoolThreadCount()),
      allocator_(allocator) {
  if (config.nearest_filter) {
    batch_renderer.SetDefaultFilter(GL_NEAREST, GL_NEAREST);
  }
}

void Engine::Initialize() {
  TIMER();
  filesystem.Initialize(config);
  // Open the persistent save database.
  char save_dir[1024];
  GetUserSaveDir(config.app_name, save_dir, sizeof(save_dir));
  auto save_result = save.Open(save_dir);
  if (save_result.is_error()) {
    ELOG("Failed to open save database at ", save_dir);
  }
  lua.LoadLibraries();
  lua.Register(&shaders);
  lua.Register(&batch_renderer);
  lua.Register(&renderer);
  lua.Register(window);
  lua.Register(&keyboard);
  lua.Register(&mouse);
  lua.Register(&controllers);
  lua.Register(&touch);
  lua.Register(&shaders);
  lua.Register(&sound);
  lua.Register(&filesystem);
  lua.Register(&physics);
  lua.Register(&network);
  lua.Register(&console);
  lua.Register(&camera);
  lua.Register(assets);
  lua.Register(&timers);
  lua.Register(&frame_allocator);
  lua.Register(&save);
  AddByteBufferLibrary(&lua);
  AddCameraLibrary(&lua);
  AddFilesystemLibrary(&lua);
  AddGraphicsLibrary(&lua);
  AddInputLibrary(&lua);
  AddLogLibrary(&lua);
  AddMathLibrary(&lua);
  AddPhysicsLibrary(&lua);
  AddRandomLibrary(&lua);
  AddSoundLibrary(&lua);
  AddSystemLibrary(&lua);
  AddAssetsLibrary(&lua);
  AddCollisionLibrary(&lua);
  AddDataLibrary(&lua, assets);
  AddNetworkLibrary(&lua);
  AddJsonLibrary(&lua);
  AddTestLibrary(&lua);
  AddTimerLibrary(&lua);
  AddSceneLibrary(&lua);
  AddParticlesLibrary(&lua);
  AddSaveLibrary(&lua);
  AddTilemapLibrary(&lua);
  lua.BuildCompilationCache();
  // Register asset loaders.
  assets->RegisterShaderLoad(
      [](DbAssets::Shader* shader, void* ud) -> ErrorOr<void> {
        auto* self = static_cast<Engine*>(ud);
        auto result = self->shaders.Load(*shader);
        if (result.is_error()) {
          self->lua.SetError(result.error().file(), result.error().line(),
                             result.error().message());
          return result.release_error();
        }
        return {};
      },
      this);
  assets->RegisterScriptLoad(
      [](DbAssets::Script* script, void* ud) -> ErrorOr<void> {
        auto* self = static_cast<Engine*>(ud);
        self->lua.LoadScript(*script);
        return {};
      },
      this);
  assets->RegisterImageLoad(
      [](DbAssets::Image* image, void* ud) -> ErrorOr<void> {
        auto* self = static_cast<Engine*>(ud);
        self->renderer.LoadTexture(*image);
        return {};
      },
      this);
  assets->RegisterSpritesheetLoad(
      [](DbAssets::Spritesheet* spritesheet, void* ud) -> ErrorOr<void> {
        auto* self = static_cast<Engine*>(ud);
        return self->renderer.LoadSpritesheet(*spritesheet);
      },
      this);
  assets->RegisterSpriteLoad(
      [](DbAssets::Sprite* sprite, void* ud) -> ErrorOr<void> {
        auto* self = static_cast<Engine*>(ud);
        return self->renderer.LoadSprite(*sprite);
      },
      this);
  assets->RegisterSoundLoad(
      [](DbAssets::Sound* asset, void* ud) -> ErrorOr<void> {
        auto* self = static_cast<Engine*>(ud);
        self->sound.LoadSound(*asset);
        return {};
      },
      this);
  assets->RegisterFontLoad(
      [](DbAssets::Font* font, void* ud) -> ErrorOr<void> {
        auto* self = static_cast<Engine*>(ud);
        self->renderer.LoadFont(*font);
        return {};
      },
      this);
  assets->RegisterProtoLoad(
      [](DbAssets::ProtoDescriptor* desc, void* ud) -> ErrorOr<void> {
        auto* self = static_cast<Engine*>(ud);
        if (!self->lua.LoadProtoDescriptor(
                ByteSlice(desc->contents, desc->size))) {
          return Error::Message("Failed to load proto descriptor");
        }
        LOG("Loaded proto schema ", desc->name);
        return {};
      },
      this);
  assets->Load();
  auto* controller_db = assets->LookupTextFile("gamecontrollerdb");
  if (controller_db) {
    controllers.Initialize(
        ByteSlice(controller_db->contents, controller_db->size));
  } else {
    controllers.Initialize();
  }
  lua.LoadMain();
  lua.FlushCompilationCache();
}

void Engine::StartFrame() {
  frame_allocator.Reset();
  mouse.InitForFrame();
  keyboard.InitForFrame();
  controllers.InitForFrame();
  touch.InitForFrame();
}

void Engine::ForwardEventToLua(const SDL_Event& event) {
  if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
    if (event.type == SDL_EVENT_KEY_DOWN) {
      lua.HandleKeypressed(event.key.scancode);
    }
    if (event.type == SDL_EVENT_KEY_UP) {
      lua.HandleKeyreleased(event.key.scancode);
    }
  }
  if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
       event.type == SDL_EVENT_MOUSE_MOTION)) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      if (event.button.button == SDL_BUTTON_LEFT) {
        lua.HandleMousePressed(0);
      } else if (event.button.button == SDL_BUTTON_MIDDLE) {
        lua.HandleMousePressed(1);
      } else if (event.button.button == SDL_BUTTON_RIGHT) {
        lua.HandleMousePressed(2);
      }
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      if (event.button.button == SDL_BUTTON_LEFT) {
        lua.HandleMouseReleased(0);
      } else if (event.button.button == SDL_BUTTON_MIDDLE) {
        lua.HandleMouseReleased(1);
      } else if (event.button.button == SDL_BUTTON_RIGHT) {
        lua.HandleMouseReleased(2);
      }
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
      lua.HandleMouseMoved(FVec2(event.motion.x, event.motion.y),
                           FVec2(event.motion.xrel, event.motion.yrel));
    }
  }
  if (event.type == SDL_EVENT_TEXT_INPUT) {
    lua.HandleTextInput(event.text.text);
  }
}

void Engine::Reload(const HotReloadChanges& changes) {
  timers.Clear();
  if (changes.has_audio_changes || changes.has_script_changes) {
    sound.StopAll();
  }
  physics.Clear();
  assets->Load();
}

void Engine::HandleEvent(const SDL_Event& event) {
  if (event.type == SDL_EVENT_WINDOW_RESIZED) {
    if (config.resizable) {
      IVec2 new_viewport(event.window.data1, event.window.data2);
      batch_renderer.SetViewport(new_viewport);
      physics.UpdateDimensions(new_viewport);
    }
  }
  if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
    keyboard.PushEvent(event);
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
      event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
      event.type == SDL_EVENT_MOUSE_MOTION ||
      event.type == SDL_EVENT_MOUSE_WHEEL) {
    mouse.PushEvent(event);
  }
  if (event.type == SDL_EVENT_FINGER_DOWN ||
      event.type == SDL_EVENT_FINGER_MOTION ||
      event.type == SDL_EVENT_FINGER_UP ||
      event.type == SDL_EVENT_FINGER_CANCELED) {
    touch.PushEvent(event);
  }
  controllers.PushEvent(event);
  ForwardEventToLua(event);
}

}  // namespace G
