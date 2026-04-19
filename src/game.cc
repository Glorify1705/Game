#include "game.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string_view>

#include "allocators.h"
#include "assets.h"
#include "cli.h"
#include "clock.h"
#include "config.h"
#include "debug_ui.h"
#include "engine.h"
#include "executor.h"
#include "libraries/sqlite3.h"
#include "libraries/stb_image_write.h"
#include "logging.h"
#include "packer.h"
#include "platform.h"
#include "profiler.h"
#include "sdl_init.h"
#include "stats.h"
#include "stringlib.h"
#include "thread.h"
#include "units.h"
#include "vec.h"
#include "version.h"

namespace G {

constexpr size_t kEngineMemory = Gigabytes(4);

namespace {

// Holds state needed by the SDL audio callback thread.
struct AudioCallbackContext {
  Sound* sound;
  float buf[kAudioBufFloats];
};

void SDLCALL StaticAudioCallback(void* userdata, SDL_AudioStream* stream,
                                 int additional_amount, int /*total_amount*/) {
  static bool named = false;
  if (!named) {
    SetCurrentThreadName("audio");
    named = true;
  }
  auto* ctx = static_cast<AudioCallbackContext*>(userdata);
  const int total_floats = additional_amount / (int)sizeof(float);
  const int clamped =
      total_floats < kAudioBufFloats ? total_floats : kAudioBufFloats;
  const int samples_per_channel = clamped / kAudioChannels;
  ctx->sound->SoundCallback(ctx->buf, samples_per_channel, kAudioChannels);
  SDL_PutAudioStreamData(stream, ctx->buf,
                         static_cast<size_t>(samples_per_channel) *
                             kAudioChannels * sizeof(float));
}

void TakeScreenshotToClipboard(BatchRenderer* batch_renderer,
                               Allocator* allocator) {
  const char* write_dir = PHYSFS_getWriteDir();
  if (write_dir == nullptr) {
    LOG("Cannot take screenshot: no PhysFS write directory set");
    return;
  }
  CmdBuffer dir(write_dir, "screenshots");
  if (MakeDirs(dir.str()).is_error()) {
    LOG("Failed to create screenshot directory: ", dir.str());
    return;
  }
  CmdBuffer path(dir.str(), "/screenshot_",
                 static_cast<uint64_t>(SDL_GetTicks()), ".png");
  ArenaAllocator scratch(allocator, Megabytes(32));
  auto screenshot = batch_renderer->TakeScreenshot(&scratch);
  int ok = stbi_write_png(path.str(), screenshot.width, screenshot.height,
                          /*comp=*/4, screenshot.buffer, screenshot.width * 4);
  if (!ok) {
    LOG("Failed to write screenshot to ", path.str());
    return;
  }
  SDL_SetClipboardText(path.str());
  LOG("Screenshot saved to ", path.str());
}

// Owns the main loop state and orchestrates per-frame work.
struct Game {
  Engine* engine;
  const GameConfig* config;
  const GameOptions* opts;
  SdlContext* sdl;
  HotReloadManager* hot_reload;
  Allocator* allocator;
  DebugUI* debug_ui;

  Stats stats = {};
  Time last_frame = {};
  DebugUI::FrameBreakdown last_breakdown_ = {};
  double t = 0;
  double real_t = 0;
  double accum = 0;
  bool screenshot_requested = false;
  bool first_update_done = false;
  bool running = true;

  Game(Engine* engine_, const GameConfig* config_, const GameOptions* opts_,
       SdlContext* sdl_, HotReloadManager* hot_reload_, Allocator* allocator_,
       DebugUI* debug_ui_)
      : engine(engine_),
        config(config_),
        opts(opts_),
        sdl(sdl_),
        hot_reload(hot_reload_),
        allocator(allocator_),
        debug_ui(debug_ui_) {}

  // Runs the game loop until quit or Lua stop.
  void Run();

  // Applies any pending hot-reload changes to the engine.
  void HandleHotReload();

  // Polls SDL events, dispatches to engine, handles debug keys.
  void PollEvents();

  // Runs one fixed-timestep tick.
  void UpdateTick(double scaled_dt);

  // Runs fixed-timestep updates, consuming the accumulated frame time.
  void RunUpdates();

  // Renders a frame, including debug overlay and screenshots.
  void Render();

  // Renders the error screen when Lua has a fatal error.
  void RenderCrashScreen(std::string_view error);
};

void Game::Run() {
  SDL_ResumeAudioDevice(sdl->audio_device);
  last_frame = Now();
  constexpr double kStep = TimeStepInSeconds();
  while (running) {
    if (engine->lua.Stopped()) break;
    if (engine->lua.HasError() && engine->keyboard.IsDown(SDL_SCANCODE_Q)) {
      engine->lua.Stop();
      break;
    }
    HandleHotReload();
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
      engine->StartFrame();
      SDL_StartTextInput(sdl->window);
    }
    PollEvents();
    if (opts->test_mode) {
      engine->lua.ResumeTestCoroutine();
    }
    // Pause simulation while the window lacks keyboard focus. Rendering and
    // event polling continue so the window redraws and focus events are
    // still picked up. Test mode drives its own update cadence and is
    // excluded. The first update is never paused: games may legitimately
    // start unfocused (launched from a terminal) and `draw` commonly
    // depends on state that `update` must set at least once.
    const bool paused =
        !opts->test_mode && first_update_done &&
        (SDL_GetWindowFlags(sdl->window) & SDL_WINDOW_INPUT_FOCUS) == 0;
    if (paused) accum = 0;
    const Time update_start = Now();
    RunUpdates();
    last_breakdown_.update_ms =
        static_cast<float>(ToSeconds(Now() - update_start) * 1000.0);
    first_update_done = true;
    engine->batch_renderer.SetFrameTime(static_cast<float>(t));
    {
      PROFILE_SCOPE_N("Render");
      Render();
    }
    double frame_ms = ToSeconds(Now() - frame_start) * 1000.0;
    PROFILE_COUNTER("Frame Time (ms)", frame_ms);
    PROFILE_COUNTER("Lua Memory (KB)", engine->lua.MemoryUsage() / 1024.0);
    stats.AddSample(frame_ms);
    debug_ui->AddFrameTimeSample(static_cast<float>(frame_ms));
    debug_ui->AddLuaMemorySample(
        static_cast<float>(engine->lua.MemoryUsage()) / 1024.0f);
    debug_ui->AddBreakdownSample(last_breakdown_);
  }
}

void Game::HandleHotReload() {
  if (!hot_reload->PendingChanges()) return;
  PROFILE_SCOPE_N("HotReload");
  TIMER("Hotload requested");
  auto changes = hot_reload->ConsumePendingChanges();
  engine->lua.ClearError();
  engine->Reload(changes);
  if (changes.has_script_changes) {
    engine->lua.LoadMain();
    engine->lua.Init();
  }
  LOG("Hot-reload complete: ", changes.file_count, " file(s) changed",
      changes.has_script_changes ? " (scripts)" : "",
      changes.has_audio_changes ? " (audio)" : "");
}

void Game::PollEvents() {
  PROFILE_SCOPE_N("PollEvents");
  for (SDL_Event event; SDL_PollEvent(&event);) {
    if (event.type == SDL_EVENT_QUIT) {
      engine->lua.HandleQuit();
      running = false;
      return;
    }
    // Forward every event to ImGui before the engine processes it.
    debug_ui->ProcessEvent(&event);
    // Toggle the debug UI with Tab (processed before capture check so
    // Tab always works regardless of ImGui focus state).
    if (event.type == SDL_EVENT_KEY_DOWN &&
        event.key.scancode == SDL_SCANCODE_TAB) {
      if (config->enable_debug_rendering) {
        debug_ui->Toggle();
      }
    }
    // Debug UI shortcuts (F5/F6/F7).
    if (event.type == SDL_EVENT_KEY_DOWN) {
      debug_ui->HandleKeyShortcut(event.key.scancode);
    }
    // Skip game input when ImGui wants to capture it.
    if (debug_ui->WantCaptureKeyboard() &&
        (event.type == SDL_EVENT_KEY_DOWN ||
         event.type == SDL_EVENT_KEY_UP ||
         event.type == SDL_EVENT_TEXT_INPUT)) {
      continue;
    }
    if (debug_ui->WantCaptureMouse() &&
        (event.type == SDL_EVENT_MOUSE_MOTION ||
         event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
         event.type == SDL_EVENT_MOUSE_WHEEL)) {
      continue;
    }
    engine->HandleEvent(event);
    if (event.type == SDL_EVENT_KEY_DOWN &&
        engine->keyboard.IsDown(SDL_SCANCODE_F12)) {
      screenshot_requested = true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN &&
        engine->keyboard.IsDown(SDL_SCANCODE_F11)) {
#ifdef GAME_WITH_PROFILING
      GetProfiler()->ToggleRecording();
#else
      LOG("Profiling is disabled (build with -DENABLE_PROFILING=ON)");
#endif
    }
  }
}

void Game::UpdateTick(double scaled_dt) {
  if (engine->lua.HasError()) {
    engine->sound.StopAll();
    return;
  }
  constexpr double kStep = TimeStepInSeconds();
  engine->lua.SetRealTime(real_t, kStep);
  {
    PROFILE_SCOPE_N("Timers::Update");
    engine->timers.Update(static_cast<float>(scaled_dt),
                          static_cast<float>(kStep));
  }
  {
    PROFILE_SCOPE_N("Physics::Update");
    engine->physics.Update(scaled_dt);
  }
  {
    PROFILE_SCOPE_N("Lua::Update");
    engine->lua.Update(t, scaled_dt);
  }
  IVec2 vp = engine->batch_renderer.GetViewport();
  engine->camera.Update(scaled_dt, FVec2(vp.x, vp.y));
}

void Game::RunUpdates() {
  PROFILE_SCOPE_N("Update");
  constexpr double kStep = TimeStepInSeconds();
  if (opts->test_mode) {
    // In test mode, run exactly one update per frame for determinism.
    const double scaled_dt = kStep * engine->lua.TimeScale();
    UpdateTick(scaled_dt);
    t += scaled_dt;
    real_t += kStep;
    accum = 0;
  } else {
    while (accum >= kStep) {
      const double scaled_dt = kStep * engine->lua.TimeScale();
      UpdateTick(scaled_dt);
      t += scaled_dt;
      real_t += kStep;
      accum -= kStep;
    }
  }
}

void Game::RenderCrashScreen(std::string_view error) {
  const IVec2 viewport = engine->batch_renderer.GetViewport();
  engine->renderer.ClearForFrame();
  engine->renderer.SetColor(Color::Black());
  engine->renderer.DrawRect(/*top_left=*/FVec(0, 0),
                            FVec(viewport.x, viewport.y), /*angle=*/0);
  engine->renderer.SetColor(Color::White());
  engine->renderer.DrawString("debug_font.ttf", 24, error, FVec(50, 50));
}

void Game::Render() {
  // Draw phase: Lua draw callback (or crash screen).
  engine->renderer.ClearForFrame();
  {
    const Time draw_start = Now();
    CmdBuffer buf(kTruncating);
    if (engine->lua.Error(&buf)) {
      RenderCrashScreen(buf.str());
    } else {
      PROFILE_SCOPE_N("Lua::Draw");
      engine->lua.Draw();
    }
    last_breakdown_.draw_ms =
        static_cast<float>(ToSeconds(Now() - draw_start) * 1000.0);
  }
  if (screenshot_requested) {
    screenshot_requested = false;
    TakeScreenshotToClipboard(&engine->batch_renderer, allocator);
  }
  // Render phase: flush commands and submit to GPU.
  {
    const Time render_start = Now();
    engine->renderer.FlushFrame();
    {
      PROFILE_SCOPE_N("BatchRenderer::Render");
      engine->batch_renderer.Render();
    }
    last_breakdown_.render_ms =
        static_cast<float>(ToSeconds(Now() - render_start) * 1000.0);
  }
  // Debug UI phase: ImGui renders after the batch renderer, directly to the
  // default framebuffer. The OpenGL3 backend saves and restores all GL state.
  {
    const Time dbgui_start = Now();
    PROFILE_SCOPE_N("DebugUI");
    debug_ui->BeginFrame();
    DebugUI::FrameContext ctx;
    ctx.frame_stats = engine->batch_renderer.GetFrameStats();
    ctx.lua_memory_kb =
        static_cast<float>(engine->lua.MemoryUsage()) / 1024.0f;
    ctx.lua_memory_bytes = engine->lua.MemoryUsage();
    ctx.cmd_buf_used = engine->batch_renderer.GetCommandBufferUsed();
    ctx.cmd_buf_capacity = engine->batch_renderer.GetCommandBufferCapacity();
    ctx.breakdown = last_breakdown_;
    debug_ui->DrawAll(ctx);
    debug_ui->EndFrame();
    last_breakdown_.debug_ui_ms =
        static_cast<float>(ToSeconds(Now() - dbgui_start) * 1000.0);
    if (debug_ui->ConsumeScreenshotRequest()) {
      screenshot_requested = true;
    }
    if (debug_ui->ConsumeHotReloadRequest()) {
      engine->lua.RequestHotload();
    }
    if (debug_ui->ConsumeQuitRequest()) {
      engine->lua.HandleQuit();
      running = false;
    }
    // Frame stepping: when paused, run one tick then re-pause.
    if (debug_ui->ConsumeStepRequest()) {
      float saved_ts = engine->lua.TimeScale();
      engine->lua.SetTimeScale(1.0f);
      constexpr double kStepDt = TimeStepInSeconds();
      UpdateTick(kStepDt);
      engine->lua.SetTimeScale(saved_ts);
    }
  }
  {
    PROFILE_SCOPE_N("SwapWindow");
    SDL_GL_SwapWindow(sdl->window);
  }
}

}  // namespace

int RunGame(const GameOptions& opts, sqlite3* db) {
  // Heap-allocated and never freed: the MimallocAllocator inside Engine
  // registers an arena via mi_manage_os_memory_ex with no unregister API,
  // so the backing memory must outlive mimalloc's mi_process_done arena
  // purge at process exit. Freeing it causes use-after-free; this is a
  // one-shot allocation reclaimed by the OS on exit.
  static ArenaAllocator* allocator = [] {
    auto* buf = static_cast<uint8_t*>(malloc(kEngineMemory));
    return new ArenaAllocator(buf, kEngineMemory);
  }();

  // Setup.
  TIMER("Setup");
  InitializeLogging();

  // Start capturing log messages into the debug UI ring buffer before any
  // LOG calls so that no startup messages are lost.
  DebugUI debug_ui;
  debug_ui.StartLogCapture(allocator);

  for (size_t i = 0; i < opts.all_args.size(); ++i) {
    LOG("args[", i, "]: ", opts.all_args[i]);
  }
  LOG("Program name = game, source = ",
      opts.source_directory ? opts.source_directory : "(packaged)");
  PHYSFS_CHECK(PHYSFS_init("game"), "Could not initialize PhysFS");
  DbAssets* db_assets;
  {
    TIMER("Getting assets");
    if (opts.source_directory != nullptr) {
      InlineExecutor inline_executor;
      MUST(WriteAssetsToDb(opts.source_directory, db, allocator,
                           &inline_executor));
    }
    db_assets = allocator->New<DbAssets>(db, allocator);
  }
  GameConfig config;
  {
    TIMER("Loading config");
    LoadConfigFromDatabase(db, &config, allocator);
  }
  LOG("Using engine version ", GAME_VERSION_STR);
  LOG("Game requested engine version ", config.version.major, ".",
      config.version.minor);
  CHECK(config.version.major == GAME_VERSION_MAJOR,
        "Unsupported major version requested");
  CHECK(config.version.minor <= GAME_VERSION_MINOR,
        "Unsupported minor engine version requested");

  // Audio callback context must be allocated before SDL init (which
  // starts the audio thread) and must outlive the audio stream.
  auto* audio_ctx = allocator->New<AudioCallbackContext>();
  SdlContext sdl;
  {
    TIMER("SDL3 initialization");
    sdl = InitializeSdl(config, StaticAudioCallback, audio_ctx);
  }
  PrintSystemInformation();

  // Engine initialization.
  Engine* e;
  {
    TIMER("Game Initialization");
    e = allocator->New<Engine>(opts.args, db, db_assets, config,
                               /*audio_channels=*/2,
                               /*audio_buffer_samples=*/8192, sdl.window,
                               allocator);
    audio_ctx->sound = &e->sound;
    if (opts.test_mode) {
      e->keyboard.SetTestMode(true);
      e->mouse.SetTestMode(true);
      e->controllers.SetTestMode(true);
    }
    e->Initialize();
    e->lua.Init();
    if (opts.test_mode) {
      e->lua.StartTestCoroutine();
    }
  }

  // Start the thread pool and hot-reload watcher.
  e->pool.Start();
  HotReloadManager hot_reload(opts.source_directory, db, &e->pool, allocator);
  hot_reload.Start();

  // Main loop.
  debug_ui.Init(sdl.window, sdl.gl_context);
  debug_ui.SetEngine(e);
  debug_ui.SetEngineArena(allocator);
  debug_ui.SetWindowCentered(config.centered);
  Game loop{e, &config, &opts, &sdl, &hot_reload, allocator, &debug_ui};
  loop.Run();
  debug_ui.Shutdown();

  // Tear down in reverse order: hot-reload watcher, thread pool, audio
  // stream (before Engine, which owns the Sound mutex), then Engine.
  int exit_code = opts.test_mode ? e->lua.TestExitCode() : 0;
  hot_reload.Stop();
  e->pool.Shutdown();
  SDL_DestroyAudioStream(sdl.audio_stream);
  sdl.audio_stream = nullptr;
  allocator->Destroy(e);
  PHYSFS_CHECK(PHYSFS_deinit(), "Could not close PhysFS");
  ShutdownSdl(&sdl);
  LOG("Statistics (in ms): ", loop.stats);
  sqlite3_close(db);
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
