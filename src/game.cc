#include "game.h"

#include <SDL3/SDL.h>

#include <cstdint>
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
  FixedStringBuffer<512> dir(write_dir, "screenshots");
  if (MakeDirs(dir.str()).is_error()) {
    LOG("Failed to create screenshot directory: ", dir.str());
    return;
  }
  FixedStringBuffer<512> path(dir.str(), "/screenshot_",
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

void RenderCrashScreen(Engine* e, std::string_view error) {
  const IVec2 viewport = e->batch_renderer.GetViewport();
  e->renderer.ClearForFrame();
  e->renderer.SetColor(Color::Black());
  e->renderer.DrawRect(/*top_left=*/FVec(0, 0), FVec(viewport.x, viewport.y),
                       /*angle=*/0);
  e->renderer.SetColor(Color::White());
  e->renderer.DrawText("debug_font.ttf", 24, error, FVec(50, 50));
}

// Update state given scaled game time t and scaled delta dt.
void Update(Engine* e, double t, double real_t, double scaled_dt,
            double real_dt) {
  if (e->lua.HasError()) {
    e->sound.StopAll();
    return;
  }
  e->lua.SetRealTime(real_t, real_dt);
  {
    PROFILE_SCOPE_N("Timers::Update");
    e->timers.Update(static_cast<float>(scaled_dt),
                     static_cast<float>(real_dt));
  }
  {
    PROFILE_SCOPE_N("Physics::Update");
    e->physics.Update(scaled_dt);
  }
  {
    PROFILE_SCOPE_N("Lua::Update");
    e->lua.Update(t, scaled_dt);
  }
  IVec2 vp = e->batch_renderer.GetViewport();
  e->camera.Update(scaled_dt, FVec2(vp.x, vp.y));
}

void Render(Engine* e, bool debug, Stats* stats, bool* screenshot_requested,
            Allocator* allocator, SDL_Window* window) {
  e->renderer.ClearForFrame();
  FixedStringBuffer<1024> buf;
  buf.AllowTruncation();
  if (e->lua.Error(&buf)) {
    RenderCrashScreen(e, buf.str());
  } else {
    PROFILE_SCOPE_N("Lua::Draw");
    e->lua.Draw();
  }
  // Draw FPS counter in debug mode.
  if (debug && stats->samples() > 0) {
    FixedStringBuffer<kMaxLogLineLength> log;
    log.AllowTruncation();
    const auto& fs = e->batch_renderer.GetFrameStats();
    log.Append("FPS: ", (1000.0 / stats->avg()), " Stats = ", *stats,
               "\nDraw calls: ", fs.draw_calls, "  Vertices: ", fs.vertices,
               "  Commands: ", fs.commands,
               "\nRedundant skipped: tex=", fs.redundant_texture,
               " xform=", fs.redundant_transform,
               " shader=", fs.redundant_shader,
               "\nLua memory usage: ", (e->lua.MemoryUsage() / 1024.0f));
    if (fs.flush_overflow > 0) {
      log.Append("\nCmd buffer overflows: ", fs.flush_overflow);
    }
    const IVec2 dims =
        e->renderer.TextDimensions("debug_font.ttf", 16, log.str());
    const IVec2 viewport = e->batch_renderer.GetViewport();
    const FVec2 text_pos(viewport.x - dims.x, viewport.y - dims.y);
    e->renderer.SetColor(Color::White());
    e->renderer.DrawText("debug_font.ttf", 16, log.str(), text_pos);
  }
  if (*screenshot_requested) {
    *screenshot_requested = false;
    TakeScreenshotToClipboard(&e->batch_renderer, allocator);
  }
  e->renderer.FlushFrame();
  {
    PROFILE_SCOPE_N("BatchRenderer::Render");
    e->batch_renderer.Render();
  }
  {
    PROFILE_SCOPE_N("SwapWindow");
    SDL_GL_SwapWindow(window);
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
  Stats stats;
  bool debug = false;
  bool screenshot_requested = false;
  SDL_ResumeAudioDevice(sdl.audio_device);
  Time last_frame = Now();
  constexpr double kStep = TimeStepInSeconds();
  double t = 0, real_t = 0, accum = 0;
  bool first_update_done = false;
  bool running = true;
  while (running) {
    if (e->lua.Stopped()) break;
    if (e->lua.HasError() && e->keyboard.IsDown(SDL_SCANCODE_Q)) {
      e->lua.Stop();
      break;
    }
    if (hot_reload.PendingChanges()) {
      PROFILE_SCOPE_N("HotReload");
      TIMER("Hotload requested");
      auto changes = hot_reload.ConsumePendingChanges();
      e->lua.ClearError();
      e->Reload(changes);
      if (changes.has_script_changes) {
        e->lua.LoadMain();
        e->lua.Init();
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
      e->StartFrame();
      SDL_StartTextInput(sdl.window);
    }
    {
      PROFILE_SCOPE_N("PollEvents");
      for (SDL_Event event; SDL_PollEvent(&event);) {
        if (event.type == SDL_EVENT_QUIT) {
          e->lua.HandleQuit();
          running = false;
          break;
        }
        e->HandleEvent(event);
        if (event.type == SDL_EVENT_KEY_DOWN &&
            e->keyboard.IsDown(SDL_SCANCODE_TAB)) {
          if (config.enable_debug_rendering) {
            debug = !debug;
          }
        }
        if (event.type == SDL_EVENT_KEY_DOWN &&
            e->keyboard.IsDown(SDL_SCANCODE_F12)) {
          screenshot_requested = true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN &&
            e->keyboard.IsDown(SDL_SCANCODE_F11)) {
#ifdef GAME_WITH_PROFILING
          GetProfiler()->ToggleRecording();
#else
          LOG("Profiling is disabled (build with -DENABLE_PROFILING=ON)");
#endif
        }
      }
    }
    if (opts.test_mode) {
      e->lua.ResumeTestCoroutine();
    }
    // Pause simulation while the window lacks keyboard focus. Rendering and
    // event polling continue so the window redraws and focus events are
    // still picked up. Test mode drives its own update cadence and is
    // excluded. The first update is never paused: games may legitimately
    // start unfocused (launched from a terminal) and `draw` commonly
    // depends on state that `update` must set at least once.
    const bool paused =
        !opts.test_mode && first_update_done &&
        (SDL_GetWindowFlags(sdl.window) & SDL_WINDOW_INPUT_FOCUS) == 0;
    if (paused) accum = 0;
    {
      PROFILE_SCOPE_N("Update");
      if (opts.test_mode) {
        // In test mode, run exactly one Update per frame for determinism.
        const double scaled_dt = kStep * e->lua.TimeScale();
        Update(e, t, real_t, scaled_dt, kStep);
        t += scaled_dt;
        real_t += kStep;
        accum = 0;
      } else {
        while (accum >= kStep) {
          const double scaled_dt = kStep * e->lua.TimeScale();
          Update(e, t, real_t, scaled_dt, kStep);
          t += scaled_dt;
          real_t += kStep;
          accum -= kStep;
        }
      }
    }
    first_update_done = true;
    e->batch_renderer.SetFrameTime(static_cast<float>(t));
    {
      PROFILE_SCOPE_N("Render");
      Render(e, debug, &stats, &screenshot_requested, allocator, sdl.window);
    }
    PROFILE_COUNTER("Frame Time (ms)", ToSeconds(Now() - frame_start) * 1000.0);
    PROFILE_COUNTER("Lua Memory (KB)", e->lua.MemoryUsage() / 1024.0);
    stats.AddSample(ToSeconds(Now() - frame_start) * 1000.0);
  }

  int exit_code = opts.test_mode ? e->lua.TestExitCode() : 0;
  // Tear down in reverse order: hot-reload watcher, thread pool, audio
  // stream (before Engine, which owns the Sound mutex), then Engine.
  hot_reload.Stop();
  e->pool.Shutdown();
  SDL_DestroyAudioStream(sdl.audio_stream);
  sdl.audio_stream = nullptr;
  allocator->Destroy(e);
  PHYSFS_CHECK(PHYSFS_deinit(), "Could not close PhysFS");
  ShutdownSdl(&sdl);
  LOG("Statistics (in ms): ", stats);
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
