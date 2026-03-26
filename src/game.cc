#include "game.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstring>
#include <string_view>

#include "allocators.h"
#include "assets.h"
#include "cli.h"
#include "clock.h"
#include "config.h"
#include "console.h"
#include "filesystem.h"
#include "input.h"
#include "libraries/glad.h"
#include "logging.h"
#include "lua.h"
#include "lua_assets.h"
#include "lua_bytebuffer.h"
#include "lua_collision.h"
#include "lua_filesystem.h"
#include "lua_graphics.h"
#include "lua_input.h"
#include "lua_math.h"
#include "lua_physics.h"
#include "lua_random.h"
#include "lua_sound.h"
#include "lua_system.h"
#include "mimalloc_allocator.h"
#include "packer.h"
#include "physics.h"
#include "renderer.h"
#include "shaders.h"
#include "sound.h"
#include "sqlite3.h"
#include "stats.h"
#include "stringlib.h"
#include "thread_pool.h"
#include "units.h"
#include "vec.h"
#include "version.h"

#ifndef _WIN32
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace G {

constexpr size_t kEngineMemory = Gigabytes(4);
constexpr size_t kHotReloadMemory = Megabytes(128);

#ifndef _INTERNAL_GAME_TRAP
#if __has_builtin(__builtin_debugtrap)
#define _INTERNAL_GAME_TRAP __builtin_debugtrap
#elif __has_builtin(__builtin_trap)
#define _INTERNAL_GAME_TRAP __builtin_trap
#elif _MSC_VER
#define _INTERNAL_GAME_TRAP __debugbreak
#else
#define _INTERNAL_GAME_TRAP std::abort
#endif
#endif

void SdlCrash(const char* message) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unrecoverable error", message,
                           /*window=*/nullptr);
  _INTERNAL_GAME_TRAP();
}

void LogToSDL(LogLevel level, const char* message) {
  switch (level) {
    case LogLevel::kFatal:
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kInfo:
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
  }
}

void GLAPIENTRY OpenglMessageCallback(GLenum /*source*/, GLenum type,
                                      GLuint /*id*/, GLenum severity,
                                      GLsizei /*length*/, const GLchar* message,
                                      const void* user_param) {
  auto l = static_cast<const OpenGLSourceLine*>(user_param);
  if (type == GL_DEBUG_TYPE_ERROR) {
    Log(l->file, l->line, "GL ERROR ", " type = ", type,
        " severity = ", severity, " message = ", message,
        ". Context = ", l->buffer);
  }
}

IVec2 GetWindowViewport(SDL_Window* window) {
  IVec2 result;
  SDL_GetWindowSizeInPixels(window, &result.x, &result.y);
  return result;
}

// TODO: port this to windows.
#ifdef _WIN32
struct Filewatcher {};
#else
class Filewatcher {
 public:
  explicit Filewatcher(sqlite3* /*db*/) {
    file_descriptor_ = inotify_init1(IN_NONBLOCK);
    CHECK(file_descriptor_ >= 0, "Failed to start inotify: ", strerror(errno));
  }

  ~Filewatcher() {
    for (size_t i = 0; i < watches_count_; ++i) {
      inotify_rm_watch(file_descriptor_, watches_[i]);
    }
    if (file_descriptor_ >= 0) close(file_descriptor_);
    file_descriptor_ = -1;
  }

  void Watch(const char* directory) {
    int watch = inotify_add_watch(file_descriptor_, directory,
                                  IN_MODIFY | IN_CREATE | IN_DELETE);
    CHECK(watch >= 0, "Could not add watch for ", directory, ": ",
          strerror(errno));
    watches_[watches_count_++] = watch;
  }

  void CheckForEvents() {
    ssize_t length = read(file_descriptor_, events_, sizeof(events_));
    if (length <= 0) {
      if (errno == EAGAIN) return;
      DIE("Failed to read file watching events: ", strerror(errno));
    }
    for (char* ptr = events_; ptr < events_ + length;) {
      const auto* event = reinterpret_cast<const inotify_event*>(ptr);
      ptr += sizeof(inotify_event) + event->len;
    }
  }

 private:
  int file_descriptor_ = -1;
  size_t watches_count_ = 0;
  int watches_[1024];
  alignas(sizeof(inotify_event)) char events_[1024 * sizeof(inotify_event)];
};
#endif

struct EngineModules {
  EngineModules(Slice<const char*> args, sqlite3* db, DbAssets* db_assets,
                const GameConfig& config, size_t audio_channels,
                size_t audio_buffer_samples, SDL_Window* sdl_window,
                Allocator* allocator, const char* source_directory)
      : console(allocator),
        db(db),
        assets(db_assets),
        source_directory(source_directory),
        config(&config),
        filesystem(allocator),
        window(sdl_window),
        shaders(allocator),
        batch_renderer(GetWindowViewport(sdl_window), &shaders, allocator),
        keyboard(allocator),
        controllers(allocator),
        text_files_table_(allocator),
        text_files_(256, allocator),
        sound(audio_channels, audio_buffer_samples, allocator),
        renderer(*db_assets, &batch_renderer, db, allocator),
        lua_allocator(allocator->Alloc(Megabytes(64), kMaxAlign),
                      Megabytes(64)),
        lua(args, db, db_assets, &lua_allocator),
        physics(FVec(config.window_width, config.window_height),
                Physics::kPixelsPerMeter, allocator),
        frame_allocator(allocator, Megabytes(128)),
        pool(allocator, 4),
        allocator_(allocator),
        hotload_allocator_(allocator, kHotReloadMemory),
        watcher_(db) {
    mu = SDL_CreateMutex();
    SDL_SetAtomicInt(&pending_changes_, 0);
  }

  ~EngineModules() { SDL_DestroyMutex(mu); }

  static int StaticCheckChangedFiles(void* ctx) {
    auto* e = static_cast<EngineModules*>(ctx);
    e->CheckChangedFiles();
    return 0;
  }

  void CheckChangedFiles() {
    LOG("Checking files in the background");
    auto is_stopped = [this] {
      LockMutex l(mu);
      return stopped;
    };
    while (!is_stopped()) {
      if (source_directory == nullptr) {
        // Packaged mode: no source files to hot-reload.  Keep the thread
        // alive but idle until it is stopped.
        SDL_Delay(100);
        continue;
      }
      hotload_allocator_.Reset();
      auto result = WriteAssetsToDb(source_directory, db, &hotload_allocator_);
      if (result.is_error()) {
        LOG("Hotload failed: ", result.error().message());
        continue;
      }
      SDL_SetAtomicInt(&pending_changes_, result.release_value().written_files);
      SDL_Delay(10);
    }
  }

  int PendingChanges() { return SDL_GetAtomicInt(&pending_changes_); }

  void MarkChangesAsProcessed() { SDL_SetAtomicInt(&pending_changes_, 0); }

  void Initialize() {
    TIMER();
    filesystem.Initialize(*config);
    lua.LoadLibraries();
    lua.Register(&shaders);
    lua.Register(&batch_renderer);
    lua.Register(&renderer);
    lua.Register(window);
    lua.Register(&keyboard);
    lua.Register(&mouse);
    lua.Register(&controllers);
    lua.Register(&shaders);
    lua.Register(&sound);
    lua.Register(&filesystem);
    lua.Register(&physics);
    lua.Register(&console);
    lua.Register(assets);
    AddByteBufferLibrary(&lua);
    AddFilesystemLibrary(&lua);
    AddGraphicsLibrary(&lua);
    AddInputLibrary(&lua);
    AddMathLibrary(&lua);
    AddPhysicsLibrary(&lua);
    AddRandomLibrary(&lua);
    AddSoundLibrary(&lua);
    AddSystemLibrary(&lua);
    AddAssetsLibrary(&lua);
    AddCollisionLibrary(&lua);
    lua.BuildCompilationCache();
    RegisterLoaders();
    assets->Load();
    DbAssets::TextFile* controller_db = nullptr;
    text_files_table_.Lookup("gamecontrollerdb", &controller_db);
    if (controller_db) {
      controllers.Initialize(
          ByteSlice(controller_db->contents, controller_db->size));
    } else {
      controllers.Initialize();
    }
    lua.LoadMain();
    lua.FlushCompilationCache();
    pool.Start();
    pool.Queue(StaticCheckChangedFiles, this);
  }

  void RegisterLoaders() {
    assets->RegisterShaderLoad(
        [](DbAssets::Shader* shader, StringBuffer* /*err*/, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          auto result = self->shaders.Load(*shader);
          if (result.is_error()) {
            LOG("Shader load failed: ", result.error().message());
            self->lua.SetError(result.error().file(), result.error().line(),
                               result.error().message());
            return;
          }
        },
        this);
    assets->RegisterScriptLoad(
        [](DbAssets::Script* script, StringBuffer* /*err*/, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->lua.LoadScript(*script);
        },
        this);
    assets->RegisterImageLoad(
        [](DbAssets::Image* image, StringBuffer* /*err*/, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->renderer.LoadImage(*image);
        },
        this);
    assets->RegisterSpritesheetLoad(
        [](DbAssets::Spritesheet* spritesheet, StringBuffer* /*err*/,
           void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->renderer.LoadSpritesheet(*spritesheet);
        },
        this);
    assets->RegisterSpriteLoad(
        [](DbAssets::Sprite* sprite, StringBuffer* /*err*/, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->renderer.LoadSprite(*sprite);
        },
        this);
    assets->RegisterSoundLoad(
        [](DbAssets::Sound* sound, StringBuffer* /*err*/, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->sound.LoadSound(*sound);
        },
        this);
    assets->RegisterFontLoad(
        [](DbAssets::Font* font, StringBuffer* /*err*/, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->renderer.LoadFont(*font);
        },
        this);
    assets->RegisterTextLoad(
        [](DbAssets::TextFile* text_file, StringBuffer* /*err*/, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->text_files_.Push(*text_file);
          self->text_files_table_.Insert(text_file->name,
                                         &self->text_files_.back());
        },
        this);
  }

  void Deinitialize() {
    {
      LockMutex l(mu);
      stopped = true;
    }
    pool.Stop();
    pool.Wait();
  }

  void StartFrame() {
    frame_allocator.Reset();
    mouse.InitForFrame();
    keyboard.InitForFrame();
    controllers.InitForFrame();
  }

  void ForwardEventToLua(const SDL_Event& event) {
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

  void Reload() {
    sound.StopAll();
    assets->Load();
  }

  void HandleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_WINDOW_RESIZED) {
      if (config->resizable) {
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
    controllers.PushEvent(event);
    ForwardEventToLua(event);
  }

  SDL_Mutex* mu;
  DebugConsole console;
  sqlite3* db;
  bool stopped = false;
  DbAssets* assets;
  const char* const source_directory;
  const GameConfig* config = nullptr;
  Filesystem filesystem;
  SDL_Window* window;
  Shaders shaders;
  BatchRenderer batch_renderer;
  Keyboard keyboard;
  Mouse mouse;
  Controllers controllers;
  Dictionary<DbAssets::TextFile*> text_files_table_;
  FixedArray<DbAssets::TextFile> text_files_;
  Sound sound;
  Renderer renderer;
  MimallocAllocator lua_allocator;
  Lua lua;
  Physics physics;
  ArenaAllocator frame_allocator;
  ThreadPool pool;
  Allocator* allocator_;
  ArenaAllocator hotload_allocator_;
  Filewatcher watcher_;
  SDL_AtomicInt pending_changes_;
};

class Game {
 public:
  Game(const GameOptions& opts, sqlite3* db, Allocator* allocator)
      : opts_(opts), allocator_(allocator), db_(db) {
    TIMER("Setup");
    InitializeLogging();
    for (size_t i = 0; i < opts.all_args.size(); ++i) {
      LOG("args[", i, "]: ", opts.all_args[i]);
    }
    LOG("Program name = game, source = ",
        opts.source_directory ? opts.source_directory : "(packaged)");
    PHYSFS_CHECK(PHYSFS_init("game"), "Could not initialize PhysFS");
    {
      TIMER("Getting assets");
      if (opts.source_directory != nullptr) {
        MUST(WriteAssetsToDb(opts.source_directory, db_, allocator_));
      }
      db_assets_ = allocator_->New<DbAssets>(db_, allocator_);
    }
    {
      TIMER("Loading config");
      LoadConfigFromDatabase(db_, &config_, allocator_);
    }
    LOG("Using engine version ", GAME_VERSION_STR);
    LOG("Game requested engine version ", config_.version.major, ".",
        config_.version.minor);
    CHECK(config_.version.major == GAME_VERSION_MAJOR,
          "Unsupported major version requested");
    CHECK(config_.version.minor <= GAME_VERSION_MINOR,
          "Unsupported minor engine version requested");
    {
      TIMER("SDL3 initialization");
      InitializeSDL(config_);
      window_ = CreateWindow(config_);
      context_ = CreateOpenglContext(config_, window_);
    }
    PrintSystemInformation();
  }

  ~Game() {
    // Destroy the audio stream first to stop the callback thread before
    // destroying EngineModules (which owns the Sound mutex).
    SDL_DestroyAudioStream(audio_stream_);
    e_->Deinitialize();
    allocator_->Destroy(e_);
    if (SDL_WasInit(SDL_INIT_HAPTIC) != 0) {
      SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    }
    if (SDL_WasInit(SDL_INIT_JOYSTICK)) {
      SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }
    if (SDL_WasInit(SDL_INIT_GAMEPAD)) {
      SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
    PHYSFS_CHECK(PHYSFS_deinit(), "Could not close PhysFS");
    SDL_GL_DestroyContext(context_);
    SDL_DestroyWindow(window_);
    LOG("Statistics (in ms): ", stats_);
    SDL_Quit();
    sqlite3_close(db_);
  }

  void Init() {
    TIMER("Game Initialization");
    e_ = allocator_->New<EngineModules>(opts_.args, db_, db_assets_, config_,
                                        /*audio_channels=*/2,
                                        /*audio_buffer_samples=*/8192, window_,
                                        allocator_, opts_.source_directory);
    e_->Initialize();
    e_->lua.Init();
    if (opts_.hotreload) {
      e_->watcher_.Watch(opts_.source_directory);
    }
  }

  void Run() {
    SDL_ResumeAudioDevice(audio_device_);
    double last_frame = NowInSeconds();
    constexpr double kStep = TimeStepInSeconds();
    double t = 0, accum = 0;
    for (;;) {
      if (e_->lua.Stopped()) return;
      if (e_->lua.HasError() && e_->keyboard.IsDown(SDL_SCANCODE_Q)) {
        e_->lua.Stop();
        return;
      }
      if (e_->PendingChanges()) {
        TIMER("Hotload requested");
        e_->lua.ClearError();
        e_->Reload();
        e_->lua.LoadMain();
        e_->lua.Init();
        e_->MarkChangesAsProcessed();
      }
      const double now = NowInSeconds();
      const double frame_time = now - last_frame;
      last_frame = now;
      accum += frame_time;
      if (accum < kStep) {
        SDL_Delay(1);
        continue;
      }
      const auto frame_start = NowInSeconds();
      e_->StartFrame();
      SDL_StartTextInput(window_);
      for (SDL_Event event; SDL_PollEvent(&event);) {
        if (event.type == SDL_EVENT_QUIT) {
          e_->lua.HandleQuit();
          return;
        }
        e_->HandleEvent(event);
        if (event.type == SDL_EVENT_KEY_DOWN &&
            e_->keyboard.IsDown(SDL_SCANCODE_TAB)) {
          if (config_.enable_debug_rendering) {
            debug_ = !debug_;
          }
        }
      }
      while (accum >= kStep) {
        Update(t, kStep);
        t += kStep;
        accum -= kStep;
      }
      Render();
      stats_.AddSample((NowInSeconds() - frame_start) * 1000.0);
    }
  }

  void RenderCrashScreen(std::string_view error) {
    const IVec2 viewport = e_->batch_renderer.GetViewport();
    e_->renderer.ClearForFrame();
    e_->renderer.SetColor(Color::Black());
    e_->renderer.DrawRect(/*top_left=*/FVec(0, 0), FVec(viewport.x, viewport.y),
                          /*angle=*/0);
    e_->renderer.SetColor(Color::White());
    e_->renderer.DrawText("debug_font.ttf", 24, error, FVec(50, 50));
  }

  // Update state given current time t and frame delta dt, both in ms.
  void Update(double t, double dt) {
    if (e_->lua.HasError()) {
      e_->sound.StopAll();
      return;
    }
    e_->physics.Update(dt);
    e_->lua.Update(t, dt);
  }

  void Render() {
    e_->renderer.ClearForFrame();
    FixedStringBuffer<1024> buf;
    if (e_->lua.Error(&buf)) {
      RenderCrashScreen(buf.str());
    } else {
      e_->lua.Draw();
    }
    // Draw FPS counter in debug mode.
    if (debug_ && stats_.samples() > 0) {
      FixedStringBuffer<kMaxLogLineLength> log(
          "FPS: ", (1000.0 / stats_.avg()), " Stats = ", stats_,
          "\nLua memory usage: ", (e_->lua.MemoryUsage() / 1024.0f));
      const IVec2 dims =
          e_->renderer.TextDimensions("debug_font.ttf", 16, log.str());
      const IVec2 viewport = e_->batch_renderer.GetViewport();
      const FVec2 text_pos(viewport.x - dims.x, viewport.y - dims.y);
      e_->renderer.SetColor(Color::White());
      e_->renderer.DrawText("debug_font.ttf", 16, log.str(), text_pos);
    }
    e_->renderer.FlushFrame();
    e_->batch_renderer.Render(&e_->frame_allocator);
    SDL_GL_SwapWindow(window_);
  }

 private:
  void InitializeLogging() {
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
    SetLogSink(LogToSDL);
    SetCrashHandler(SdlCrash);
  }

  void InitializeSDL(const GameConfig& config) {
    SDL_SetAppMetadata(config.app_name[0] != '\0' ? config.app_name : "game",
                       GAME_VERSION_STR,
                       config.org_name[0] != '\0' ? config.org_name : nullptr);
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS),
          "Could not initialize SDL: ", SDL_GetError());
    if (config.enable_joystick) {
      CHECK(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD),
            "Could not initialize SDL joysticks: ", SDL_GetError());
      SDL_SetJoystickEventsEnabled(true);
      SDL_SetGamepadEventsEnabled(true);
    }
    SDL_HideCursor();
    // Initialize audio.
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, 44100};
    audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                              &spec, StaticAudioCallback, this);
    CHECK(audio_stream_ != nullptr,
          "Could not open audio stream: ", SDL_GetError());
    audio_device_ = SDL_GetAudioStreamDevice(audio_stream_);
  }

  static void SDLCALL StaticAudioCallback(void* userdata,
                                          SDL_AudioStream* stream,
                                          int additional_amount,
                                          int /*total_amount*/) {
    auto* game = static_cast<Game*>(userdata);
    constexpr int channels = 2;
    // 44100 Hz * 2 channels at ~100ms max = 8820 floats. 16384 is generous.
    constexpr int kMaxFloats = 16384;
    const int total_floats = additional_amount / (int)sizeof(float);
    const int clamped = total_floats < kMaxFloats ? total_floats : kMaxFloats;
    const int samples_per_channel = clamped / channels;
    float buf[kMaxFloats];
    game->e_->sound.SoundCallback(buf, samples_per_channel, channels);
    SDL_PutAudioStreamData(stream, buf,
                           samples_per_channel * channels * sizeof(float));
  }

  void PrintSystemInformation() {
    LOG("Compiled with ", COMPILER, " version ", COMPILER_VERSION);
    LOG("Using SDL ", SDL_GetVersion());
    LOG("Using OpenGL Version: ", glGetString(GL_VERSION));
    LOG("Using GLAD Version: ", GLVersion.major, ".", GLVersion.minor);
    LOG("Using ", LUA_VERSION);
    LOG("Using Box2D ", b2_version.major, ".", b2_version.minor, ".",
        b2_version.revision);
    PHYSFS_Version physfs_version;
    PHYSFS_getLinkedVersion(&physfs_version);
    LOG("Using PhysFS ", physfs_version.major, ".", physfs_version.minor, ".",
        physfs_version.patch);
    LOG("Using SQLite Version ", SQLITE_VERSION);
    LOG("Running on platform: ", SDL_GetPlatform());
    LOG("Have ", SDL_GetNumLogicalCPUCores(), " logical cores");
  }

  SDL_Window* CreateWindow(const GameConfig& config) {
    TIMER("Initializing basic attributes");
    CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4),
          "Could not set major version", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6),
          "Could not set minor version", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                              SDL_GL_CONTEXT_PROFILE_CORE),
          "Could not set Core profile", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1),
          "Could not set double buffering version", SDL_GetError());
#ifdef GAME_WITH_ASSERTS
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (config.borderless) flags |= SDL_WINDOW_BORDERLESS;
    if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

    SDL_Window* window = nullptr;

    if (config.centered && !config.fullscreen) {
      LOG("Creating centered window");

      SDL_SetHint(SDL_HINT_X11_WINDOW_TYPE, "_NET_WM_WINDOW_TYPE_DIALOG");

      SDL_DisplayID display = SDL_GetPrimaryDisplay();
      const SDL_DisplayMode* display_mode = SDL_GetCurrentDisplayMode(display);
      CHECK(display_mode != nullptr, "Could not get display mode ",
            SDL_GetError());

      const int screen_width = display_mode->w;
      const int screen_height = display_mode->h;

      LOG("Display mode: width = ", display_mode->w,
          " height = ", display_mode->h,
          " refresh rate = ", display_mode->refresh_rate);

      const int window_x = (screen_width - config.window_width) / 2;
      const int window_y = (screen_height - config.window_height) / 2;

      window = SDL_CreateWindow(config.window_title, config.window_width,
                                config.window_height, flags);
      if (window) SDL_SetWindowPosition(window, window_x, window_y);
    } else {
      window = SDL_CreateWindow(config.window_title, config.window_width,
                                config.window_height, flags);
      CHECK(window != nullptr, "Could not initialize window: ", SDL_GetError());
    }

    CHECK(window != nullptr);
    return window;
  }

  SDL_GLContext CreateOpenglContext(const GameConfig& config,
                                    SDL_Window* window) {
    LOG("Creating SDL context");
    CHECK(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1),
          " failed to set multi sample buffers: ", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config.msaa_samples),
          " failed to set multi samples: ", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1),
          " failed to set accelerated visual: ", SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    CHECK(context != nullptr,
          "Could not load OpenGL context: ", SDL_GetError());
    CHECK(gladLoadGLLoader([](const char* name) -> void* {
            return (void*)SDL_GL_GetProcAddress(name);
          }),
          "Could not load GLAD");
    if (config.vsync_mode != 0) {
      CHECK(SDL_GL_SetSwapInterval(config.vsync_mode),
            "Could not set up VSync to mode ", config.vsync_mode, ": ",
            SDL_GetError());
    }
    const bool supports_opengl_debug = GLAD_GL_VERSION_4_3 && GLAD_GL_KHR_debug;
    if (supports_opengl_debug && config.enable_opengl_debug) {
      LOG("OpenGL Debug Callback Support is enabled!");
      glEnable(GL_DEBUG_OUTPUT);
      glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
      glDebugMessageCallback(OpenglMessageCallback,
                             /*userParam=*/GetOpenGLSourceLine());
    } else {
      LOG("OpenGL Debug Callback Support is disabled");
    }
    return context;
  }

  GameOptions opts_;
  Allocator* allocator_;
  sqlite3* db_;
  DbAssets* db_assets_ = nullptr;
  GameConfig config_;
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_;
  SDL_AudioStream* audio_stream_ = nullptr;
  SDL_AudioDeviceID audio_device_;
  EngineModules* e_;
  bool debug_ = false;
  Stats stats_;
};

void RunGame(const GameOptions& opts, sqlite3* db) {
  // Heap-allocated and never freed: the MimallocAllocator inside Game registers
  // an arena via mi_manage_os_memory_ex with no unregister API, so the backing
  // memory must outlive mimalloc's mi_process_done arena purge at process exit.
  // Freeing it causes use-after-free; this is a one-shot allocation reclaimed
  // by the OS on exit.
  static ArenaAllocator* allocator = [] {
    auto* buf = static_cast<uint8_t*>(malloc(kEngineMemory));
    return new ArenaAllocator(buf, kEngineMemory);
  }();
  auto* g = allocator->New<Game>(opts, db, allocator);
  g->Init();
  g->Run();
  allocator->Destroy(g);
}

int Main(int argc, const char* argv[]) {
  // Top-level arena for CLI subcommand memory.
  auto* cli_buf = static_cast<uint8_t*>(malloc(Megabytes(512)));
  ArenaAllocator cli_arena(cli_buf, Megabytes(512));

  if (argc >= 2) {
    std::string_view cmd = argv[1];
    // Validate the command before doing anything else.
    if (cmd != "init" && cmd != "run" && cmd != "clean" && cmd != "package" &&
        cmd != "stubs" && cmd != "version" && cmd != "help" &&
        cmd != "--help" && cmd != "--version") {
      fprintf(stderr, "Error: unknown command '%s'.\n\n", argv[1]);
      return CmdHelp(argv[0], /*subcommand=*/nullptr);
    }
    Slice<const char*> sub(argv + 1, (size_t)(argc - 1));
    if (cmd == "init") return CmdInit(sub, &cli_arena);
    if (cmd == "run") return CmdRun(sub, &cli_arena);
    if (cmd == "clean") return CmdClean(sub, &cli_arena);
    if (cmd == "package") return CmdPackage(sub, &cli_arena);
    if (cmd == "stubs") return CmdStubs(sub, &cli_arena);
    if (cmd == "version") return CmdVersion(argv[0]);
    if (cmd == "help") return CmdHelp(argv[0], argc > 2 ? argv[2] : nullptr);
    if (cmd == "--help") return CmdHelp(argv[0], /*subcommand=*/nullptr);
    if (cmd == "--version") return CmdVersion(argv[0]);
  }
  // No subcommand: packaged game mode or help.
  if (PackagedGameExists(argv[0])) {
    return CmdRunPackaged({argv, (size_t)argc}, &cli_arena);
  }
  return CmdHelp(argv[0], /*subcommand=*/nullptr);
}

}  // namespace G

int main(int argc, const char* argv[]) { return G::Main(argc, argv); }
