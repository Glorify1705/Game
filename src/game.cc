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
#include "SDL_mixer.h"
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
#include "strings.h"
#include "thread_pool.h"
#include "units.h"
#include "vec.h"
#include "version.h"
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

struct EngineModules {
  EngineModules(size_t argc, const char* argv[], sqlite3* db,
                DbAssets* db_assets, const GameConfig& config,
                SDL_Window* sdl_window, Allocator* allocator,
                const char* source_directory)
      : console(allocator),
        db(db),
        assets(db_assets),
        source_directory(source_directory),
        config(&config),
        filesystem(allocator),
        window(sdl_window),
        shaders(
            Shaders::ErrorHandler{.handler =
                                      [](void* ctx, std::string_view file,
                                         int line, std::string_view error) {
                                        auto* e =
                                            static_cast<EngineModules*>(ctx);
                                        e->HandleShaderError(file, line, error);
                                      },
                                  .ud = this},
            allocator),
        batch_renderer(GetWindowViewport(sdl_window), &shaders, allocator),
        keyboard(allocator),
        controllers(db_assets, allocator),
        sound(*db_assets, allocator),
        renderer(*db_assets, &batch_renderer, allocator),
        lua(argc, argv, db, db_assets, SystemAllocator::Instance()),
        physics(FVec(config.window_width, config.window_height),
                Physics::kPixelsPerMeter, allocator),
        frame_allocator(allocator, Megabytes(128)),
        pool(allocator, 4),
        allocator_(allocator),
        hotload_allocator_(allocator, kHotReloadMemory) {
    mu = SDL_CreateMutex();
  }

  ~EngineModules() { SDL_DestroyMutex(mu); }

  static int CheckChangedFiles(void* ctx) {
    auto* e = static_cast<EngineModules*>(ctx);
    e->CheckChangedFiles();
    return 0;
  }

  void HandleShaderError(std::string_view file, int line,
                         std::string_view error) {
    Log(file, line, error);
    lua.SetError(file, line, error);
  }

  void CheckChangedFiles() {
    LOG("Checking files in the background");
    auto is_stopped = [this] {
      LockMutex l(mu);
      return stopped;
    };
    while (!is_stopped()) {
      hotload_allocator_.Reset();
      WriteAssetsToDb(source_directory, db, &hotload_allocator_);
      SDL_Delay(10);
    }
  }

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
    shaders.CompileAssetShaders(*assets);
    lua.LoadScripts();
    lua.LoadMain();
    RegisterReloads();
    pool.Start();
    pool.Queue(CheckChangedFiles, this);
  }

  void RegisterReloads() {
    assets->RegisterLoadFn(
        "shader",
        [](DbAssets* assets, std::string_view name, char* err, void* ud) {
          auto* self = static_cast<EngineModules*>(ud);
          self->shaders.Reload(*self->assets->GetShader(name));
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

  void Reload() { assets->Load(); }

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
};

void InitializeLogging() {
  SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
  SetLogSink(LogToSDL);
  SetCrashHandler(SdlCrash);
}

void InitializeSDL(const GameConfig& config) {
  CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER |
                 SDL_INIT_EVENTS) == 0,
        "Could not initialize SDL: ", SDL_GetError());
  CHECK(Mix_OpenAudio(44100, MIX_INIT_OGG, 2, 2048) == 0,
        "Could not initialize audio: ", Mix_GetError());
  if (config.enable_joystick) {
    CHECK(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0,
          "Could not initialize SDL joysticks: ", SDL_GetError());
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);
  }
  SDL_ShowCursor(false);
}

void PrintSystemInformation() {
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
  const SDL_version& mix_version = *Mix_Linked_Version();
  LOG("Using Linked SDL Mixer ",
      SDL_VERSIONNUM(mix_version.major, mix_version.minor, mix_version.patch));
  LOG("Using Compiled Mixer ", SDL_MIXER_COMPILEDVERSION);
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

    LOG("Display mode: width = ", display_mode.w, " height = ", display_mode.h,
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
  CHECK(
      SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config.msaa_samples) == 0,
      " failed to set multi samples: ", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1) == 0,
        " failed to set accelerated visual: ", SDL_GetError());
  auto context = SDL_GL_CreateContext(window);
  CHECK(context != nullptr, "Could not load OpenGL context: ", SDL_GetError());
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
      TIMER("Getting assets");
      db_assets_ = GetAssets(argv + 1, argc - 1, db_);
    }
    {
      TIMER("Loading config");
      LoadConfig(*db_assets_, &config_, allocator_);
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
    Mix_Quit();
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
    result->Load();
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
                                        window_, allocator_,
                                        load_.source_directory);
    e_->Initialize();
    e_->lua.Init();
  }

  void Run() {
    double last_frame = NowInSeconds();
    constexpr double kStep = TimeStepInSeconds();
    double t = 0, accum = 0;
    for (;;) {
      if (e_->lua.Stopped()) return;
      if (e_->lua.HasError() && e_->keyboard.IsDown(SDL_SCANCODE_Q)) {
        e_->lua.Stop();
        return;
      }
      if (e_->lua.HotloadRequested()) {
        LOG("Hotload requested");
        e_->Reload();
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
    e_->renderer.DrawText("debug_font.ttf", 20, error, FVec(50, 50));
  }

  // Update state given current time t and frame delta dt, both in ms.
  void Update(double t, double dt) {
    char error[1024];
    if (size_t error_len = e_->lua.Error(error, sizeof(error) - 1);
        error_len > 0) {
      e_->sound.Stop();
      RenderCrashScreen(std::string_view(error, error_len));
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
          "\nLua memory usage: ", e_->lua.MemoryUsage());
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
  const size_t argc_;
  const char** const argv_;
  LoadResult load_;
  Allocator* allocator_;
  sqlite3* db_;
  DbAssets* db_assets_ = nullptr;
  GameConfig config_;
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_;
  EngineModules* e_;
  bool debug_ = false;
  Stats stats_;
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
