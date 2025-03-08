#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_hints.h"
#include "allocators.h"
#include "assets.h"
#include "circular_buffer.h"
#include "clock.h"
#include "config.h"
#include "console.h"
#include "filesystem.h"
#include "image.h"
#include "input.h"
#include "libraries/glad.h"
#include "logging.h"
#include "lua.h"
#include "lua_assets.h"
#include "lua_bytebuffer.h"
#include "lua_filesystem.h"
#include "lua_graphics.h"
#include "lua_input.h"
#include "lua_math.h"
#include "lua_physics.h"
#include "lua_random.h"
#include "lua_sound.h"
#include "lua_system.h"
#include "mat.h"
#include "math.h"
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
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace G {

#if defined(__GNUC__)
#if defined(__clang__)
#define COMPILER "clang++"
#else
#define COMPILER "g++"
#endif
#define COMPILER_VERSION __VERSION__
#elif defined(_MSC_VER)
#define COMPILER "msvc"
#define COMPILER_VERSION _MSC_FULL_VER
#else
#error Please add your compiler here.
#endif

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
    case LOG_LEVEL_FATAL:
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LOG_LEVEL_INFO:
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
  SDL_GL_GetDrawableSize(window, &result.x, &result.y);
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
  EngineModules(size_t argc, const char* argv[], sqlite3* db,
                DbAssets* db_assets, const GameConfig& config,
                const SDL_AudioSpec& spec, SDL_Window* sdl_window,
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
        controllers(db_assets, allocator),
        sound(spec, allocator),
        renderer(*db_assets, &batch_renderer, allocator),
        lua(argc, argv, db, db_assets, SystemAllocator::Instance()),
        physics(FVec(config.window_width, config.window_height),
                Physics::kPixelsPerMeter, allocator),
        frame_allocator(allocator, Megabytes(128)),
        pool(allocator, 4),
        allocator_(allocator),
        hotload_allocator_(allocator, kHotReloadMemory),
        watcher_(db) {
    mu = SDL_CreateMutex();
    SDL_AtomicSet(&pending_changes_, 0);
  }

  ~EngineModules() { SDL_DestroyMutex(mu); }

  void AudioCallback(uint8_t* buffer, int length) {
    std::memset(buffer, 0, length);
    auto* sample_buffer = reinterpret_cast<float*>(buffer);
    sound.SoundCallback(sample_buffer, length / sizeof(float));
  }

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
      hotload_allocator_.Reset();
      const auto result =
          WriteAssetsToDb(source_directory, db, &hotload_allocator_);
      SDL_AtomicSet(&pending_changes_, result.written_files);
      SDL_Delay(10);
    }
  }

  int PendingChanges() { return SDL_AtomicGet(&pending_changes_); }

  void MarkChangesAsProcessed() { SDL_AtomicSet(&pending_changes_, 0); }

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
    lua.BuildCompilationCache();
    RegisterLoaders();
    assets->Load();
    lua.LoadMain();
    lua.FlushCompilationCache();
    pool.Start();
    pool.Queue(StaticCheckChangedFiles, this);
  }

  void RegisterLoaders() {
    assets->RegisterShaderLoad(
        [](DbAssets::Shader* shader, StringBuffer* err, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          Shaders::Error error;
          if (!self->shaders.Load(*shader, &error)) {
            self->lua.SetError(error.file.str(), error.line, error.error.str());
            return;
          }
        },
        this);
    assets->RegisterScriptLoad(
        [](DbAssets::Script* script, StringBuffer* err, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->lua.LoadScript(*script);
        },
        this);
    assets->RegisterImageLoad(
        [](DbAssets::Image* image, StringBuffer* err, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->renderer.LoadImage(*image);
        },
        this);
    assets->RegisterSpritesheetLoad(
        [](DbAssets::Spritesheet* spritesheet, StringBuffer* err, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->renderer.LoadSpritesheet(*spritesheet);
        },
        this);
    assets->RegisterSpriteLoad(
        [](DbAssets::Sprite* sprite, StringBuffer* err, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->renderer.LoadSprite(*sprite);
        },
        this);
    assets->RegisterSoundLoad(
        [](DbAssets::Sound* sound, StringBuffer* err, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->sound.LoadSound(*sound);
        },
        this);
    assets->RegisterFontLoad(
        [](DbAssets::Font* font, StringBuffer* err, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->renderer.LoadFont(*font);
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
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
      if (event.type == SDL_KEYDOWN) {
        lua.HandleKeypressed(event.key.keysym.scancode);
      }
      if (event.type == SDL_KEYUP) {
        lua.HandleKeyreleased(event.key.keysym.scancode);
      }
    }
    if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
         event.type == SDL_MOUSEMOTION)) {
      if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          lua.HandleMousePressed(0);
        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
          lua.HandleMousePressed(1);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          lua.HandleMousePressed(2);
        }
      }
      if (event.type == SDL_MOUSEBUTTONUP) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          lua.HandleMouseReleased(0);
        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
          lua.HandleMouseReleased(1);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          lua.HandleMouseReleased(2);
        }
      }
      if (event.type == SDL_MOUSEMOTION) {
        lua.HandleMouseMoved(FVec2(event.motion.x, event.motion.y),
                             FVec2(event.motion.xrel, event.motion.yrel));
      }
    }
    if (event.type == SDL_TEXTINPUT) {
      lua.HandleTextInput(event.text.text);
    }
  }

  void Reload() {
    sound.StopAll();
    assets->Load();
  }

  void HandleEvent(const SDL_Event& event) {
    if (event.type == SDL_WINDOWEVENT) {
      if (config->resizable && event.window.event == SDL_WINDOWEVENT_RESIZED) {
        IVec2 new_viewport(event.window.data1, event.window.data2);
        batch_renderer.SetViewport(new_viewport);
        physics.UpdateDimensions(new_viewport);
      }
    }
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
      keyboard.PushEvent(event);
    }
    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
        event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL) {
      mouse.PushEvent(event);
    }
    controllers.PushEvent(event);
    ForwardEventToLua(event);
  }

  SDL_mutex* mu;
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
  Sound sound;
  Renderer renderer;
  Lua lua;
  Physics physics;
  ArenaAllocator frame_allocator;
  ThreadPool pool;
  Allocator* allocator_;
  ArenaAllocator hotload_allocator_;
  Filewatcher watcher_;
  SDL_atomic_t pending_changes_;
};

class Game {
 public:
  Game(int argc, const char** argv, Allocator* allocator)
      : argc_(argc), argv_(argv), allocator_(allocator) {
    TIMER("Setup");
    InitializeLogging();
    LOG("Program name = ", argv[0], " args = ", argc);
    for (int i = 1; i < argc; ++i) {
      LOG("argv[", i, "] = ", argv[i]);
    }
    PHYSFS_CHECK(PHYSFS_init(argv[0]),
                 "Could not initialize PhysFS: ", argv[0]);
    {
      TIMER("Load database");
      load_ = LoadDb(argc_ - 1, argv_ + 1);
    }
    {
      TIMER("Loading config");
      LoadConfigFromDatabase(db_, &config_, allocator_);
    }
    {
      TIMER("Getting assets");
      db_assets_ = GetAssets(argv + 1, argc - 1, db_);
    }
    LOG("Using engine version ", GAME_VERSION_STR);
    LOG("Game requested engine version ", config_.version.major, ".",
        config_.version.minor);
    CHECK(config_.version.major == GAME_VERSION_MAJOR,
          "Unsupported major version requested");
    CHECK(config_.version.minor <= GAME_VERSION_MINOR,
          "Unsupported minor engine version requested");
    {
      TIMER("SDL2 initialization");
      InitializeSDL(config_);
      window_ = CreateWindow(config_);
      context_ = CreateOpenglContext(config_, window_);
    }
    PrintSystemInformation();
  }

  ~Game() {
    e_->Deinitialize();
    allocator_->Destroy(e_);
    if (SDL_WasInit(SDL_INIT_HAPTIC) != 0) {
      SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    }
    SDL_CloseAudioDevice(audio_device_);
    if (SDL_WasInit(SDL_INIT_JOYSTICK)) {
      SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
      SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    PHYSFS_CHECK(PHYSFS_deinit(), "Could not close PhysFS");
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    LOG("Statistics (in ms): ", stats_);
    SDL_Quit();
    sqlite3_close(db_);
  }

  DbAssets* GetAssets(const char* argv[], size_t argc, sqlite3* db) {
    WriteAssetsToDb(argv[0], db, allocator_);
    auto* result = allocator_->New<DbAssets>(db, allocator_);
    return result;
  }

  struct LoadResult {
    const char* source_directory;
    bool should_hotreload;
  };

  LoadResult LoadDb(size_t argc, const char** argv) {
    LoadResult result;
    result.should_hotreload = false;
    result.source_directory = nullptr;
    if (argc == 0) {
      LOG("Reading assets from default DB since no file was provided");
      if (sqlite3_open("assets.sqlite3", &db_) != SQLITE_OK) {
        DIE("Failed to open ", argv[0], ": ", sqlite3_errmsg(db_));
      }
    } else if (argc_ == 1) {
      LOG("Reading assets from ", argv[0]);
      if (sqlite3_open(argv[0], &db_) != SQLITE_OK) {
        DIE("Failed to open ", argv[0], ": ", sqlite3_errmsg(db_));
      }
    } else {
      LOG("Packing all files in directory ", argv[0], " into the database");
      result.should_hotreload = true;
      result.source_directory = argv[0];
      if (sqlite3_open(argv[1], &db_) != SQLITE_OK) {
        DIE("Failed to open ", argv[0], ": ", sqlite3_errmsg(db_));
      }
      InitializeAssetDb(db_);
    }
    return result;
  }

  void Init() {
    TIMER("Game Initialization");
    e_ = allocator_->New<EngineModules>(argc_, argv_, db_, db_assets_, config_,
                                        obtained_spec_, window_, allocator_,
                                        load_.source_directory);
    e_->Initialize();
    e_->lua.Init();
    if (load_.should_hotreload) {
      e_->watcher_.Watch(load_.source_directory);
    }
  }

  void Run() {
    SDL_PauseAudioDevice(audio_device_, 0);
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
      SDL_StartTextInput();
      for (SDL_Event event; SDL_PollEvent(&event);) {
        if (event.type == SDL_QUIT) {
          e_->lua.HandleQuit();
          return;
        }
        e_->HandleEvent(event);
        if (event.type == SDL_KEYDOWN &&
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
    FixedStringBuffer<1024> buf;
    if (e_->lua.Error(&buf)) {
      e_->sound.StopAll();
      RenderCrashScreen(buf.str());
    } else {
      e_->physics.Update(dt);
      e_->lua.Update(t, dt);
    }
  }

  void Render() {
    e_->lua.Draw();
    // Draw FPS counter in debug mode.
    if (debug_ && stats_.samples() > 0) {
      FixedStringBuffer<kMaxLogLineLength> log(
          "FPS: ", (1000.0f / stats_.avg()), " Stats = ", stats_,
          "\nLua memory usage: ", (e_->lua.MemoryUsage() / 1024.0));
      e_->renderer.SetColor(Color::White());
      const IVec2 dims =
          e_->renderer.TextDimensions("debug_font.ttf", 12, log.str());
      const IVec2 viewport = e_->batch_renderer.GetViewport();
      e_->renderer.DrawText("debug_font.ttf", 12, log.str(),
                            FVec(viewport.x - dims.x, viewport.y - dims.y));
    }
    e_->renderer.FlushFrame();
    e_->batch_renderer.Render(&e_->frame_allocator);
    SDL_GL_SwapWindow(window_);
  }

 private:
  void InitializeLogging() {
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
    SetLogSink(LogToSDL);
    SetCrashHandler(SdlCrash);
  }

  void InitializeSDL(const GameConfig& config) {
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER |
                   SDL_INIT_EVENTS) == 0,
          "Could not initialize SDL: ", SDL_GetError());
    if (config.enable_joystick) {
      CHECK(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0,
            "Could not initialize SDL joysticks: ", SDL_GetError());
      SDL_JoystickEventState(SDL_ENABLE);
      SDL_GameControllerEventState(SDL_ENABLE);
    }
    SDL_ShowCursor(false);
    // Initialize audio.
    SDL_AudioSpec desired_spec;
    std::memset(&desired_spec, 0, sizeof(desired_spec));
    desired_spec.freq = 44100;
    desired_spec.format = AUDIO_F32SYS;
    desired_spec.samples = 256;
    desired_spec.callback = &StaticAudioCallback;
    desired_spec.channels = 2;
    desired_spec.userdata = this;
    audio_device_ =
        SDL_OpenAudioDevice(nullptr, 0, &desired_spec, &obtained_spec_, 0);
    CHECK(audio_device_ > 0, "Could not open audio device ", SDL_GetError());
    LOG("Audio Spec Channels: ", obtained_spec_.channels);
    LOG("Audio Spec Buffer Samples: ", obtained_spec_.samples);
    LOG("Audio Spec Sample Frequency: ", obtained_spec_.freq);
    LOG("Audio Spec Format: ", obtained_spec_.format);
  }

  static void StaticAudioCallback(void* userdata, uint8_t* buffer,
                                  int samples) {
    auto* g = static_cast<Game*>(userdata);
    g->e_->AudioCallback(buffer, samples);
  }

  void PrintSystemInformation() {
    LOG("Compiled with ", COMPILER, " version ", COMPILER_VERSION);
    SDL_version compiled, linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    LOG("Using Compiled SDL ",
        SDL_VERSIONNUM(compiled.major, compiled.minor, compiled.patch));
    LOG("Using Linked SDL ",
        SDL_VERSIONNUM(linked.major, linked.minor, linked.patch));
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
    LOG("Have ", SDL_GetCPUCount(), " logical cores");
  }

  SDL_Window* CreateWindow(const GameConfig& config) {
    TIMER("Initializing basic attributes");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0,
          "Could not set major version", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) == 0,
          "Could not set minor version", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                              SDL_GL_CONTEXT_PROFILE_CORE) == 0,
          "Could not set Core profile", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) == 0,
          "Could not set double buffering version", SDL_GetError());
    uint32_t flags = SDL_WINDOW_OPENGL;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (config.borderless) flags |= SDL_WINDOW_BORDERLESS;
    if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

    SDL_Window* window = nullptr;

    if (config.centered && !config.fullscreen) {
      LOG("Creating centered window");

      SDL_SetHint(SDL_HINT_X11_WINDOW_TYPE, "_NET_WM_WINDOW_TYPE_DIALOG");

      SDL_DisplayMode display_mode;
      CHECK(SDL_GetCurrentDisplayMode(0, &display_mode) == 0,
            "Could not get display mode ", SDL_GetError());

      const int screen_width = display_mode.w;
      const int screen_height = display_mode.h;

      LOG("Display mode: width = ", display_mode.w,
          " height = ", display_mode.h,
          " refresh rate = ", display_mode.refresh_rate);

      const int window_x = (screen_width - config.window_width) / 2;
      const int window_y = (screen_height - config.window_height) / 2;

      window = SDL_CreateWindow(config.window_title, window_x, window_y,
                                config.window_width, config.window_height,
                                flags | SDL_WINDOW_SHOWN);
    } else {
      window = SDL_CreateWindow(config.window_title, SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED, config.window_width,
                                config.window_height, flags);
      CHECK(window != nullptr, "Could not initialize window: ", SDL_GetError());
    }

    CHECK(window != nullptr);
    return window;
  }

  SDL_GLContext CreateOpenglContext(const GameConfig& config,
                                    SDL_Window* window) {
    LOG("Creating SDL context");
    CHECK(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1) == 0,
          " failed to set multi sample buffers: ", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config.msaa_samples) ==
              0,
          " failed to set multi samples: ", SDL_GetError());
    CHECK(SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1) == 0,
          " failed to set accelerated visual: ", SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    CHECK(context != nullptr,
          "Could not load OpenGL context: ", SDL_GetError());
    CHECK(gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress),
          "Could not load GLAD");
    if (config.vsync_mode != 0) {
      CHECK(SDL_GL_SetSwapInterval(config.vsync_mode) == 0,
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

  const size_t argc_;
  const char** const argv_;
  LoadResult load_;
  Allocator* allocator_;
  sqlite3* db_;
  DbAssets* db_assets_ = nullptr;
  GameConfig config_;
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_;
  SDL_AudioSpec obtained_spec_;
  EngineModules* e_;
  bool debug_ = false;
  Stats stats_;
  SDL_AudioDeviceID audio_device_;
};

void GameMain(int argc, const char* argv[]) {
  auto* allocator = new StaticAllocator<kEngineMemory>();
  // Ensure we don't overflow the stack in any module by allocating Game on
  // the heap.
  auto* g = allocator->New<Game>(argc, argv, allocator);
  g->Init();
  g->Run();
  allocator->Destroy(g);
  delete allocator;
}

}  // namespace G

int main(int argc, const char* argv[]) {
  G::GameMain(argc, argv);
  return 0;
}
