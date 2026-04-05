#include "game.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>

#include "allocators.h"
#include "assets.h"
#include "camera.h"
#include "cli.h"
#include "clock.h"
#include "config.h"
#include "console.h"
#include "executor.h"
#include "file_watcher.h"
#include "filesystem.h"
#include "input.h"
#include "libraries/glad.h"
#include "libraries/stb_image_write.h"
#include "logging.h"
#include "lua.h"
#include "lua_assets.h"
#include "lua_bytebuffer.h"
#include "lua_camera.h"
#include "lua_collision.h"
#include "lua_filesystem.h"
#include "lua_graphics.h"
#include "lua_input.h"
#include "lua_log.h"
#include "lua_math.h"
#include "lua_physics.h"
#include "lua_random.h"
#include "lua_sound.h"
#include "lua_system.h"
#include "lua_timer.h"
#include "mimalloc_allocator.h"
#include "packer.h"
#include "physics.h"
#include "platform.h"
#include "profiler.h"
#include "renderer.h"
#include "shaders.h"
#include "sound.h"
#include "sqlite3.h"
#include "stats.h"
#include "stringlib.h"
#include "thread.h"
#include "timer.h"
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
    case LogLevel::kFatal:
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kError:
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kWarn:
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kInfo:
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kDebug:
      SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kTrace:
      SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
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

namespace {

// Audio file extensions checked during hot-reload to decide whether
// sound.StopAll() is necessary.
constexpr std::array<std::string_view, 3> kAudioExtensions = {
    ".qoa",
    ".ogg",
    ".wav",
};

// Script file extensions checked during hot-reload to decide whether
// lua.LoadMain() + lua.Init() must re-run.
constexpr std::array<std::string_view, 2> kScriptExtensions = {
    ".lua",
    ".fnl",
};

bool HasAudioExtension(const char* path) {
  std::string_view sv(path);
  for (auto ext : kAudioExtensions) {
    if (HasSuffix(sv, ext)) return true;
  }
  return false;
}

bool HasScriptExtension(const char* path) {
  std::string_view sv(path);
  for (auto ext : kScriptExtensions) {
    if (HasSuffix(sv, ext)) return true;
  }
  return false;
}

}  // namespace

// Describes what kinds of assets changed during a hot-reload cycle.
struct HotReloadChanges {
  bool has_script_changes = false;
  bool has_audio_changes = false;
  uint32_t file_count = 0;
  // First few changed file paths for logging.
  static constexpr uint32_t kMaxLogFiles = 8;
  const char* files[kMaxLogFiles] = {};
};

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
        pool(allocator, ThreadPoolExecutor::NumDefaultThreads()),
        allocator_(allocator),
        hotload_allocator_(allocator, kHotReloadMemory),
        watcher_(allocator) {
    pending_changes_.store(0);
  }

  ~EngineModules() = default;

  static bool StaticCheckChangedFiles(void* ctx) {
    auto* e = static_cast<EngineModules*>(ctx);
    e->CheckChangedFiles();
    return true;
  }

  void CheckChangedFiles() {
    SetCurrentThreadName("file-watcher");
    LOG("Background file watcher started");
    auto is_stopped = [this] {
      LockMutex l(mu);
      return stopped;
    };
    while (!is_stopped()) {
      if (source_directory == nullptr) {
        SleepMs(100);
        continue;
      }

      watcher_.CheckForEvents();
      auto changes = watcher_.DrainChanges();

      bool should_process = false;
      if (changes.needs_full_rescan) {
        LOG("[hotload] Full rescan requested");
        should_process = true;
      } else if (changes.count > 0) {
        for (uint32_t i = 0; i < changes.count; ++i) {
          LOG("[hotload] File changed: ", changes.paths[i]);
        }
        should_process = true;
      }

      if (should_process) {
        hotload_allocator_.Reset();
        auto result =
            WriteAssetsToDb(source_directory, db, &hotload_allocator_, &pool);
        if (result.is_error()) {
          LOG("[hotload] WriteAssetsToDb failed: ", result.error().message());
          SleepMs(50);
          continue;
        }
        size_t written = result.release_value().written_files;
        LOG("[hotload] WriteAssetsToDb wrote ", written, " file(s)");
        if (written > 0) {
          LockMutex l(mu);
          pending_reload_.file_count = changes.count;
          pending_reload_.has_script_changes = false;
          pending_reload_.has_audio_changes = false;
          if (changes.needs_full_rescan) {
            pending_reload_.has_script_changes = true;
            pending_reload_.has_audio_changes = true;
          } else {
            for (uint32_t i = 0; i < changes.count; ++i) {
              if (HasScriptExtension(changes.paths[i])) {
                pending_reload_.has_script_changes = true;
              }
              if (HasAudioExtension(changes.paths[i])) {
                pending_reload_.has_audio_changes = true;
              }
            }
          }
          pending_changes_.store(static_cast<int>(written));
        }
      }

      // Sleep longer when idle (no events), shorter when active.
      SleepMs(changes.count > 0 ? 10 : 50);
    }
  }

  int PendingChanges() { return pending_changes_.load(); }

  // Get the description of pending changes and reset the atomic flag.
  HotReloadChanges ConsumePendingChanges() {
    LockMutex l(mu);
    HotReloadChanges result = pending_reload_;
    pending_reload_ = {};
    pending_changes_.store(0);
    return result;
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
    lua.Register(&camera);
    lua.Register(assets);
    lua.Register(&timers);
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
    AddTimerLibrary(&lua);
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
    if (source_directory != nullptr) {
      watcher_.Watch(source_directory);
    }
    pool.Start();
    watcher_task_.fn = StaticCheckChangedFiles;
    watcher_task_.userdata = this;
    watcher_task_.cleanup = nullptr;
    pool.Submit(&watcher_task_);
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
    pool.Shutdown();
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

  void Reload(const HotReloadChanges& changes) {
    timers.Clear();
    if (changes.has_audio_changes || changes.has_script_changes) {
      sound.StopAll();
    }
    physics.Clear();
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

  std::mutex mu;
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
  Camera camera;
  MimallocAllocator lua_allocator;
  Lua lua;
  TimerSystem timers;
  Physics physics;
  ArenaAllocator frame_allocator;
  ThreadPoolExecutor pool;
  Allocator* allocator_;
  ArenaAllocator hotload_allocator_;
  FileWatcher watcher_;
  std::atomic<int> pending_changes_{0};
  HotReloadChanges pending_reload_;
  Task watcher_task_;
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
        InlineExecutor inline_executor;
        MUST(WriteAssetsToDb(opts.source_directory, db_, allocator_,
                             &inline_executor));
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
  }

  void Run() {
    SDL_ResumeAudioDevice(audio_device_);
    Time last_frame = Now();
    constexpr double kStep = TimeStepInSeconds();
    double t = 0, real_t = 0, accum = 0;
    for (;;) {
      if (e_->lua.Stopped()) return;
      if (e_->lua.HasError() && e_->keyboard.IsDown(SDL_SCANCODE_Q)) {
        e_->lua.Stop();
        return;
      }
      if (e_->PendingChanges()) {
        PROFILE_SCOPE_N("HotReload");
        TIMER("Hotload requested");
        auto changes = e_->ConsumePendingChanges();
        e_->lua.ClearError();
        e_->Reload(changes);
        if (changes.has_script_changes) {
          e_->lua.LoadMain();
          e_->lua.Init();
        }
        LOG("Hot-reload complete: ", changes.file_count, " file(s) changed",
            changes.has_script_changes ? " (scripts)" : "",
            changes.has_audio_changes ? " (audio)" : "");
      }
      const Time now = Now();
      const double frame_time = ToSeconds(now - last_frame);
      last_frame = now;
      accum += frame_time;
      if (accum < kStep) {
        SleepMs(1);
        continue;
      }
      PROFILE_FRAME;
      const Time frame_start = Now();
      {
        PROFILE_SCOPE_N("StartFrame");
        e_->StartFrame();
        SDL_StartTextInput(window_);
      }
      {
        PROFILE_SCOPE_N("PollEvents");
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
          if (event.type == SDL_EVENT_KEY_DOWN &&
              e_->keyboard.IsDown(SDL_SCANCODE_F12)) {
            screenshot_requested_ = true;
          }
          if (event.type == SDL_EVENT_KEY_DOWN &&
              e_->keyboard.IsDown(SDL_SCANCODE_F11)) {
            GetProfiler()->ToggleRecording();
          }
        }
      }
      {
        PROFILE_SCOPE_N("Update");
        while (accum >= kStep) {
          const double scaled_dt = kStep * e_->lua.TimeScale();
          Update(t, real_t, scaled_dt, kStep);
          t += scaled_dt;
          real_t += kStep;
          accum -= kStep;
        }
      }
      e_->batch_renderer.SetFrameTime(static_cast<float>(t));
      {
        PROFILE_SCOPE_N("Render");
        Render();
      }
      PROFILE_COUNTER("Frame Time (ms)",
                      ToSeconds(Now() - frame_start) * 1000.0);
      PROFILE_COUNTER("Lua Memory (KB)", e_->lua.MemoryUsage() / 1024.0);
      stats_.AddSample(ToSeconds(Now() - frame_start) * 1000.0);
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

  // Update state given scaled game time t and scaled delta dt.
  void Update(double t, double real_t, double scaled_dt, double real_dt) {
    if (e_->lua.HasError()) {
      e_->sound.StopAll();
      return;
    }
    e_->lua.SetRealTime(real_t, real_dt);
    {
      PROFILE_SCOPE_N("Timers::Update");
      e_->timers.Update(static_cast<float>(scaled_dt),
                        static_cast<float>(real_dt));
    }
    {
      PROFILE_SCOPE_N("Physics::Update");
      e_->physics.Update(scaled_dt);
    }
    {
      PROFILE_SCOPE_N("Lua::Update");
      e_->lua.Update(t, scaled_dt);
    }
    IVec2 vp = e_->batch_renderer.GetViewport();
    e_->camera.Update(scaled_dt, FVec2(vp.x, vp.y));
  }

  void Render() {
    e_->renderer.ClearForFrame();
    FixedStringBuffer<1024> buf;
    buf.AllowTruncation();
    if (e_->lua.Error(&buf)) {
      RenderCrashScreen(buf.str());
    } else {
      PROFILE_SCOPE_N("Lua::Draw");
      e_->lua.Draw();
    }
    // Draw FPS counter in debug mode.
    if (debug_ && stats_.samples() > 0) {
      FixedStringBuffer<kMaxLogLineLength> log;
      log.AllowTruncation();
      log.Append("FPS: ", (1000.0 / stats_.avg()), " Stats = ", stats_,
                 "\nLua memory usage: ", (e_->lua.MemoryUsage() / 1024.0f));
      const IVec2 dims =
          e_->renderer.TextDimensions("debug_font.ttf", 16, log.str());
      const IVec2 viewport = e_->batch_renderer.GetViewport();
      const FVec2 text_pos(viewport.x - dims.x, viewport.y - dims.y);
      e_->renderer.SetColor(Color::White());
      e_->renderer.DrawText("debug_font.ttf", 16, log.str(), text_pos);
    }
    if (screenshot_requested_) {
      screenshot_requested_ = false;
      TakeScreenshotToClipboard();
    }
    e_->renderer.FlushFrame();
    {
      PROFILE_SCOPE_N("BatchRenderer::Render");
      e_->batch_renderer.Render(&e_->frame_allocator);
    }
    {
      PROFILE_SCOPE_N("SwapWindow");
      SDL_GL_SwapWindow(window_);
    }
  }

 private:
  void TakeScreenshotToClipboard() {
    const char* write_dir = PHYSFS_getWriteDir();
    if (write_dir == nullptr) {
      LOG("Cannot take screenshot: no PhysFS write directory set");
      return;
    }
    FixedStringBuffer<512> dir(write_dir, "screenshots");
    if (MakeDirs(dir.str()).is_error()) {
      LOG("Failed to create screenshot directory: ", dir.str());
      return;
    }
    FixedStringBuffer<512> path(dir.str(), "/screenshot_",
                                static_cast<uint64_t>(SDL_GetTicks()), ".png");
    ArenaAllocator scratch(allocator_, Megabytes(32));
    auto screenshot = e_->batch_renderer.TakeScreenshot(&scratch);
    int ok =
        stbi_write_png(path.str(), screenshot.width, screenshot.height,
                       /*comp=*/4, screenshot.buffer, screenshot.width * 4);
    if (!ok) {
      LOG("Failed to write screenshot to ", path.str());
      return;
    }
    SDL_SetClipboardText(path.str());
    LOG("Screenshot saved to ", path.str());
  }

  void InitializeLogging() {
#ifdef GAME_WITH_ASSERTS
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
#else
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
#endif
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
    SDL_AudioSpec spec = {SDL_AUDIO_F32, kChannels, 44100};
    audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                              &spec, StaticAudioCallback, this);
    CHECK(audio_stream_ != nullptr,
          "Could not open audio stream: ", SDL_GetError());
    audio_device_ = SDL_GetAudioStreamDevice(audio_stream_);
  }

  static constexpr int kChannels = 2;
  // 44100 Hz * 2 channels at ~100ms max = 8820 floats. 16384 is generous.
  static constexpr int kAudioBufFloats = 16384;

  static void SDLCALL StaticAudioCallback(void* userdata,
                                          SDL_AudioStream* stream,
                                          int additional_amount,
                                          int /*total_amount*/) {
    static bool named = false;
    if (!named) {
      SetCurrentThreadName("audio");
      named = true;
    }
    auto* game = static_cast<Game*>(userdata);
    const int total_floats = additional_amount / (int)sizeof(float);
    const int clamped =
        total_floats < kAudioBufFloats ? total_floats : kAudioBufFloats;
    const int samples_per_channel = clamped / kChannels;
    game->e_->sound.SoundCallback(game->audio_buf_, samples_per_channel,
                                  kChannels);
    SDL_PutAudioStreamData(
        stream, game->audio_buf_,
        static_cast<size_t>(samples_per_channel) * kChannels * sizeof(float));
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
  float audio_buf_[kAudioBufFloats];
  EngineModules* e_;
  bool debug_ = false;
  bool screenshot_requested_ = false;
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
