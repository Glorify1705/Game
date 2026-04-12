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
#include "engine.h"
#include "executor.h"
#include "libraries/stb_image_write.h"
#include "logging.h"
#include "packer.h"
#include "platform.h"
#include "profiler.h"
#include "sdl_init.h"
#include "sqlite3.h"
#include "stats.h"
#include "stringlib.h"
#include "thread.h"
#include "units.h"
#include "vec.h"
#include "version.h"

namespace G {

constexpr size_t kEngineMemory = Gigabytes(4);

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
      sdl_ = InitializeSdl(config_, StaticAudioCallback, this);
    }
    PrintSystemInformation();
  }

  ~Game() {
    // Stop the audio callback thread (via audio-stream destroy) before
    // destroying Engine, which owns the Sound mutex.
    SDL_DestroyAudioStream(sdl_.audio_stream);
    sdl_.audio_stream = nullptr;
    e_->Deinitialize();
    allocator_->Destroy(e_);
    PHYSFS_CHECK(PHYSFS_deinit(), "Could not close PhysFS");
    ShutdownSdl(&sdl_);
    LOG("Statistics (in ms): ", stats_);
    sqlite3_close(db_);
  }

  void Init() {
    TIMER("Game Initialization");
    e_ = allocator_->New<Engine>(opts_.args, db_, db_assets_, config_,
                                 /*audio_channels=*/2,
                                 /*audio_buffer_samples=*/8192, sdl_.window,
                                 allocator_, opts_.source_directory);
    if (opts_.test_mode) {
      e_->keyboard.SetTestMode(true);
      e_->mouse.SetTestMode(true);
      e_->controllers.SetTestMode(true);
    }
    e_->Initialize();
    e_->lua.Init();
    if (opts_.test_mode) {
      e_->lua.StartTestCoroutine();
    }
  }

  void Run() {
    SDL_ResumeAudioDevice(sdl_.audio_device);
    Time last_frame = Now();
    constexpr double kStep = TimeStepInSeconds();
    double t = 0, real_t = 0, accum = 0;
    bool first_update_done = false;
    for (;;) {
      if (e_->lua.Stopped()) return;
      if (e_->lua.HasError() && e_->keyboard.IsDown(SDL_SCANCODE_Q)) {
        e_->lua.Stop();
        return;
      }
      if (e_->hot_reload.PendingChanges()) {
        PROFILE_SCOPE_N("HotReload");
        TIMER("Hotload requested");
        auto changes = e_->hot_reload.ConsumePendingChanges();
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
        SDL_StartTextInput(sdl_.window);
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
#ifdef GAME_WITH_PROFILING
            GetProfiler()->ToggleRecording();
#else
            LOG("Profiling is disabled (build with -DENABLE_PROFILING=ON)");
#endif
          }
        }
      }
      if (opts_.test_mode) {
        e_->lua.ResumeTestCoroutine();
      }
      // Pause simulation while the window lacks keyboard focus. Rendering and
      // event polling continue so the window redraws and focus events are
      // still picked up. Test mode drives its own update cadence and is
      // excluded. The first update is never paused: games may legitimately
      // start unfocused (launched from a terminal) and `draw` commonly
      // depends on state that `update` must set at least once.
      const bool paused =
          !opts_.test_mode && first_update_done &&
          (SDL_GetWindowFlags(sdl_.window) & SDL_WINDOW_INPUT_FOCUS) == 0;
      if (paused) accum = 0;
      {
        PROFILE_SCOPE_N("Update");
        if (opts_.test_mode) {
          // In test mode, run exactly one Update per frame for determinism.
          const double scaled_dt = kStep * e_->lua.TimeScale();
          Update(t, real_t, scaled_dt, kStep);
          t += scaled_dt;
          real_t += kStep;
          accum = 0;
        } else {
          while (accum >= kStep) {
            const double scaled_dt = kStep * e_->lua.TimeScale();
            Update(t, real_t, scaled_dt, kStep);
            t += scaled_dt;
            real_t += kStep;
            accum -= kStep;
          }
        }
      }
      first_update_done = true;
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
      const auto& fs = e_->batch_renderer.GetFrameStats();
      log.Append("FPS: ", (1000.0 / stats_.avg()), " Stats = ", stats_,
                 "\nDraw calls: ", fs.draw_calls, "  Vertices: ", fs.vertices,
                 "  Commands: ", fs.commands,
                 "\nRedundant skipped: tex=", fs.redundant_texture,
                 " xform=", fs.redundant_transform,
                 " shader=", fs.redundant_shader,
                 "\nLua memory usage: ", (e_->lua.MemoryUsage() / 1024.0f));
      if (fs.flush_overflow > 0) {
        log.Append("\nCmd buffer overflows: ", fs.flush_overflow);
      }
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
      e_->batch_renderer.Render();
    }
    {
      PROFILE_SCOPE_N("SwapWindow");
      SDL_GL_SwapWindow(sdl_.window);
    }
  }

  int TestExitCode() const { return e_->lua.TestExitCode(); }

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
    const int samples_per_channel = clamped / kAudioChannels;
    game->e_->sound.SoundCallback(game->audio_buf_, samples_per_channel,
                                  kAudioChannels);
    SDL_PutAudioStreamData(stream, game->audio_buf_,
                           static_cast<size_t>(samples_per_channel) *
                               kAudioChannels * sizeof(float));
  }

  GameOptions opts_;
  Allocator* allocator_;
  sqlite3* db_;
  DbAssets* db_assets_ = nullptr;
  GameConfig config_;
  SdlContext sdl_;
  float audio_buf_[kAudioBufFloats];
  Engine* e_;
  bool debug_ = false;
  bool screenshot_requested_ = false;
  Stats stats_;
};

int RunGame(const GameOptions& opts, sqlite3* db) {
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
  int exit_code = opts.test_mode ? g->TestExitCode() : 0;
  allocator->Destroy(g);
  return exit_code;
}

int Main(int argc, const char* argv[]) {
  InstallSignalHandlers();
  // Top-level arena for CLI subcommand memory.
  // Sized to comfortably hold the packer's 512MB sub-arena plus the
  // config arena and other CLI scratch state.
  auto* cli_buf = static_cast<uint8_t*>(malloc(Gigabytes(1)));
  ArenaAllocator cli_arena(cli_buf, Gigabytes(1));
  DEFER([cli_buf] { free(cli_buf); });

  if (argc >= 2) {
    std::string_view cmd = argv[1];
    // Validate the command before doing anything else.
    if (cmd != "init" && cmd != "run" && cmd != "clean" && cmd != "package" &&
        cmd != "stubs" && cmd != "convert" && cmd != "atlas" &&
        cmd != "version" && cmd != "help" && cmd != "--help" &&
        cmd != "--version") {
      fprintf(stderr, "Error: unknown command '%s'.\n\n", argv[1]);
      return CmdHelp(argv[0], /*subcommand=*/nullptr);
    }
    Slice<const char*> sub(argv + 1, (size_t)(argc - 1));
    if (cmd == "init") return CmdInit(sub, &cli_arena);
    if (cmd == "run") return CmdRun(sub, &cli_arena);
    if (cmd == "clean") return CmdClean(sub, &cli_arena);
    if (cmd == "package") return CmdPackage(sub, &cli_arena);
    if (cmd == "stubs") return CmdStubs(sub, &cli_arena);
    if (cmd == "convert") return CmdConvert(sub, &cli_arena);
    if (cmd == "atlas") return CmdAtlas(sub, &cli_arena);
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
