---
status: in-design
tags: [core, architecture, refactor, hot-reload]
---

# Game Module Refactor

## Problem

`game.cc` is 1019 lines containing three tightly coupled structures (`Game`,
`EngineModules`, and the free functions around them) that have grown
organically. The main pain points:

1. **`EngineModules` is a god struct.** It owns every subsystem (renderer,
   physics, sound, Lua, input, shaders, file watcher, thread pool, camera,
   timers, console, filesystem...) as direct members with fragile
   initialization order. Adding or removing a subsystem means touching the
   constructor initializer list, `Initialize()`, `RegisterLoaders()`,
   `Reload()`, and `Deinitialize()` — four separate places that must stay in
   sync.

2. **Hot-reload logic is scattered.** The file watcher thread, change
   detection, asset re-loading, Lua re-init, and audio stop/restart are spread
   across `CheckChangedFiles()`, `ConsumePendingChanges()`, `Reload()`, and
   the hot-reload block inside `Game::Run()`. The control flow between the
   background thread and the main loop is hard to follow.

3. **Mixed abstraction levels.** `game.cc` handles SDL initialization, OpenGL
   context creation, window management, audio callbacks, screenshot capture,
   debug overlay rendering, the main loop timing, input dispatch, Lua library
   registration, and the CLI dispatch — all in one file.

4. **`Game` and `EngineModules` are redundantly split.** `Game` owns the SDL
   window, OpenGL context, audio device, and config, while `EngineModules`
   owns everything else including a raw pointer back to the window. The split
   doesn't correspond to a clear boundary — `Game::Render()` reaches deep
   into `e_->` for every operation.

5. **Asset loader registration is boilerplate.** Each `RegisterXLoad` call is
   a lambda that casts `void* ud` to `EngineModules*` and forwards to the
   real method. Eight nearly identical lambdas.

## Constraints

Any refactor must preserve these engine properties:

- **No hidden allocation.** All memory comes from explicit `Allocator*`
  parameters. The engine pre-allocates a 4 GB arena; subsystems carve out
  sub-arenas. No `new`, no `malloc` outside the top-level allocation.
- **Arena-resettable hot-reload.** Hot-reload works by resetting arenas and
  re-loading assets from the database. Subsystem state must be rebuildable
  from a fresh `DbAssets::Load()` call. Pointers into arena memory become
  invalid after reset.
- **Deterministic initialization order.** Subsystems have dependencies (e.g.
  `Renderer` needs `Shaders` and `BatchRenderer`; `Lua` needs `DbAssets`).
  The order must be explicit and auditable, not hidden behind dynamic
  registration.
- **Single-threaded main loop.** Only the file watcher and thread pool run
  on background threads. The main loop (input, update, render) is
  single-threaded. The refactor must not introduce thread-safety concerns.
- **No RTTI, no exceptions.** The engine compiles with `-fno-rtti
  -fno-exceptions`. Dynamic dispatch is limited to the `Allocator` virtual
  base. Subsystem interfaces are concrete classes, not abstract bases.

## Proposed Design

### 1. Split `game.cc` into focused files

| New file | Responsibility |
|----------|---------------|
| `game.cc` | `RunGame()`, `Main()`, CLI dispatch (stays) |
| `engine.h/cc` | `Engine` class: owns subsystems, runs the main loop |
| `hot_reload.h/cc` | `HotReloadManager`: file watcher thread, change detection, reload orchestration |
| `sdl_init.h/cc` | SDL/OpenGL/audio initialization and teardown helpers |

`game.cc` becomes a thin entry point: parse options, open DB, create
`Engine`, call `Engine::Run()`.

### 2. Replace `EngineModules` with `Engine`

`Engine` is a struct that owns all subsystems as members (same as today) but
with clear lifecycle methods:

```cpp
struct Engine {
  // Subsystems, in initialization order.
  Filesystem filesystem;
  Shaders shaders;
  BatchRenderer batch_renderer;
  Renderer renderer;
  Sound sound;
  Keyboard keyboard;
  Mouse mouse;
  Controllers controllers;
  Camera camera;
  Physics physics;
  TimerSystem timers;
  DebugConsole console;
  Lua lua;

  // Infrastructure.
  DbAssets* assets;
  ThreadPoolExecutor pool;
  ArenaAllocator frame_allocator;

  // Lifecycle.
  void Initialize();
  void Shutdown();

  // Per-frame.
  void StartFrame();
  void Update(double t, double dt);
  void Render();
};
```

Key differences from `EngineModules`:

- **No hot-reload members.** The file watcher, mutex, pending changes, and
  hotload allocator move to `HotReloadManager`.
- **No SDL state.** Window, GL context, audio stream/device stay in the
  caller (the main loop function or a small `Window` struct).
- **No text files table.** Text file storage moves into `DbAssets` where it
  belongs (it's asset data, not engine state).
- **`Initialize()` absorbs `RegisterLoaders()`** since loader registration
  is part of initialization and there's no reason to separate them.

### 3. Extract `HotReloadManager`

The hot-reload logic becomes its own class:

```cpp
class HotReloadManager {
 public:
  HotReloadManager(const char* source_dir, sqlite3* db,
                   ThreadPoolExecutor* pool, Allocator* allocator);

  // Start the background file-watcher thread.
  void Start();

  // Called from the main thread each frame. Returns true if a reload
  // happened and the caller should re-init Lua.
  bool PollAndReload(Engine* engine);

  // Stop the watcher thread. Called during shutdown.
  void Stop();

 private:
  ArenaAllocator hotload_allocator_;
  FileWatcher watcher_;
  std::mutex mu_;
  std::atomic<int> pending_changes_{0};
  HotReloadChanges pending_reload_;
  bool stopped_ = false;
  // ...
};
```

This encapsulates the entire hot-reload flow:
- Background: watch files, write to DB, record what changed.
- Main thread: `PollAndReload()` checks the atomic, resets arenas, reloads
  assets, and returns whether scripts changed (so the caller knows to re-init
  Lua).

### 4. Extract SDL initialization

Move `InitializeSDL()`, `CreateWindow()`, `CreateOpenglContext()`,
`PrintSystemInformation()`, the audio callback, and `InitializeLogging()`
to `sdl_init.h/cc`. These are pure setup code that runs once and doesn't
interact with the rest of the engine.

```cpp
struct SdlContext {
  SDL_Window* window;
  SDL_GLContext gl_context;
  SDL_AudioStream* audio_stream;
  SDL_AudioDeviceID audio_device;
};

// Creates window, GL context, audio stream. Caller owns teardown.
SdlContext InitializeSdl(const GameConfig& config);
void ShutdownSdl(SdlContext* ctx);
void InitializeLogging();
```

### 5. Simplify asset loader registration

Instead of eight lambdas casting `void*` to `EngineModules*`, the `Engine`
registers itself directly:

```cpp
void Engine::RegisterLoaders() {
  assets->RegisterShaderLoad(
      [](DbAssets::Shader* s, void* ud) -> ErrorOr<void> {
        return static_cast<Engine*>(ud)->LoadShader(s);
      }, this);
  // ...
}
```

Each `Engine::LoadXxx` method handles the subsystem-specific logic (e.g.
shader errors go to `lua.SetError()`). This is a minor improvement — the
real win is that `Engine` is a well-defined type instead of a grab-bag, so
the cast-and-forward pattern is at least casting to a meaningful thing.

A further step (optional, lower priority) would be to change `DbAssets` to
accept a `LoadListener` interface instead of individual callbacks:

```cpp
struct AssetLoadListener {
  virtual ErrorOr<void> OnShaderLoaded(DbAssets::Shader*) { return {}; }
  virtual ErrorOr<void> OnImageLoaded(DbAssets::Image*) { return {}; }
  // ...
};
```

This eliminates the function-pointer + `void*` pattern entirely. However,
it introduces a virtual interface, which is a trade-off against the "no
RTTI, minimal virtual dispatch" philosophy. The function-pointer approach
is fine and consistent with the rest of the codebase (e.g.
`Physics::ContactCallback`).

### 6. Simplify the main loop

With the extractions above, the main loop in `game.cc` becomes:

```cpp
void RunGame(const GameOptions& opts, sqlite3* db) {
  ArenaAllocator allocator = ...;
  GameConfig config = LoadConfig(db, &allocator);
  SdlContext sdl = InitializeSdl(config);

  Engine engine(db, &config, sdl.window, &allocator);
  engine.Initialize();

  HotReloadManager hotreload(opts.source_directory, db,
                             &engine.pool, &allocator);
  hotreload.Start();

  // Main loop.
  Time last = Now();
  double t = 0, accum = 0;
  for (;;) {
    if (engine.lua.Stopped()) break;

    if (hotreload.PollAndReload(&engine)) {
      engine.lua.LoadMain();
      engine.lua.Init();
    }

    // ... timing, events, update, render (same structure, less nesting)
  }

  hotreload.Stop();
  engine.Shutdown();
  ShutdownSdl(&sdl);
}
```

This is readable top-to-bottom with no indirection through `e_->`.

## Memory Layout

The allocation hierarchy stays the same:

```
malloc (4 GB)
  └─ ArenaAllocator (engine root)
       ├─ Engine subsystems (carved out by each constructor)
       ├─ frame_allocator (128 MB, reset each frame)
       ├─ hotload_allocator_ (128 MB, reset each reload cycle)
       └─ lua_allocator (64 MB, mimalloc-backed for Lua GC)
```

Hot-reload resets the asset arenas inside `DbAssets` and re-calls
`Engine::Initialize()` for the asset-dependent subset (loaders fire,
textures re-upload, sprites re-register). Subsystem objects themselves are
*not* destroyed — only their asset-derived data is refreshed. This is
unchanged from the current design.

## Migration Plan

This is a pure refactor with no behavior changes. Do it in stages so each
PR is reviewable:

1. **Extract `sdl_init.h/cc`** — Move SDL/GL/audio init out of `Game`.
   Mechanical move, no logic changes.
2. **Extract `hot_reload.h/cc`** — Pull watcher thread, mutex, pending
   changes into `HotReloadManager`. Wire it up from `Game::Run()`.
3. **Rename `EngineModules` → `Engine`**, move to `engine.h/cc`. Absorb
   `RegisterLoaders()` into `Initialize()`. Move text file storage into
   `DbAssets`.
4. **Flatten `Game` into `RunGame()`** — `Game` class is no longer needed
   once SDL context is a plain struct and engine is `Engine`. The main loop
   lives in `RunGame()` directly.
5. **Clean up** — Remove dead code, audit include dependencies.

Each step should build and pass tests independently.

## What This Does NOT Change

- The main loop structure (fixed-timestep accumulator) stays the same.
- Subsystem APIs (`Renderer`, `Sound`, `Physics`, etc.) are untouched.
- The `DbAssets` callback-based loading pattern stays (just cleaner wiring).
- Memory allocation strategy is unchanged.
- Thread model is unchanged (main thread + file watcher + thread pool).
- No new abstractions or indirection layers are introduced.
