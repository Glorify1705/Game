---
status: implemented
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

1. **[DONE, PR #57]** **Extract `sdl_init.h/cc`** — Moved SDL/GL/audio init
   out of `Game` into a standalone module. `SdlContext` struct bundles
   window, GL context, audio stream, and device ID. Audio constants
   (`kAudioChannels`, `kAudioSampleRate`, `kAudioBufFloats`) moved to
   `sdl_init.h`. The `StaticAudioCallback` stays in `game.cc` because it
   accesses `Game::e_->sound` and `audio_buf_`. PhysFS init/deinit stays
   in `game.cc` — it's orthogonal to SDL.
2. **[DONE, PR #58]** **Extract `hot_reload.h/cc`** — Pulled file watcher
   background task, change detection, and hot-reload arena out of
   `EngineModules` into a standalone `HotReloadManager` class.
   `HotReloadChanges`, `kHotReloadMemory`, and audio/script extension
   helpers moved alongside. `EngineModules::Initialize()` now ends with
   `pool.Start(); hot_reload.Start();`, `Deinitialize()` is
   `hot_reload.Stop(); pool.Shutdown();`. Main loop polls
   `e_->hot_reload.PendingChanges()` / `ConsumePendingChanges()`.
   `Reload()` (subsystem-side reset) intentionally stayed on
   `EngineModules` for Step 3. Same PR also flattened
   `CheckChangedFiles` using early-continue and extracted
   `DescribePendingReload` / `LogChanges` helpers.
3. **[DONE]** **Rename `EngineModules` → `Engine`**, move to
   `engine.h/cc`. Absorbed `RegisterLoaders()` into `Initialize()`.
   Moved text file storage (`text_files_table_`, `text_files_`) into
   `DbAssets` with `LookupTextFile()` method. Removed
   `RegisterTextLoad()` callback.
4. **[DONE]** **Flatten `Game` into `RunGame()`** — Dissolved the
   `Game` class. Constructor/destructor became setup/teardown in
   `RunGame()`. Methods became anonymous-namespace free functions.
   Audio callback uses `AudioCallbackContext` struct.
   `HotReloadManager` moved from `Engine` into `RunGame()` scope.
   `Engine::Deinitialize()` removed; pool and hot-reload lifecycle
   managed explicitly in `RunGame()`.
5. **[DONE]** **Clean up** — Removed dead includes, ran clang-format.

Each step should build and pass tests independently.

### Progress snapshot (2026-04-12)

| File | Before | After Step 2 | After Step 5 |
|------|-------:|-------------:|-------------:|
| `src/game.cc` | 1019 | 656 | 421 |
| `src/engine.{h,cc}` | — | — | 82 + 232 |
| `src/sdl_init.{h,cc}` | — | 52 + 254 | 52 + 255 |
| `src/hot_reload.{h,cc}` | — | 71 + 169 | 71 + 169 |

All five steps are complete. The refactor is a pure code move with no
behavior changes.

### Detailed implementation plan for Steps 3–5

#### Step 3a: Move text file storage into `DbAssets`

`text_files_` (FixedArray) and `text_files_table_` (Dictionary) store
loaded text file data for lookup by name. This is asset data, not engine
state. Moving it into `DbAssets` lets `DbAssets::LoadText()` handle
storage directly without a callback.

**`src/assets.h` changes:**

- Add private members: `FixedArray<TextFile> text_files_` and
  `Dictionary<TextFile*> text_files_table_`.
- Add public method: `TextFile* LookupTextFile(std::string_view name)`.
- Remove `RegisterTextLoad()` and `LoadFn<TextFile> text_file_loader_`.
  `DbAssets::LoadText()` now pushes directly into its own storage.

**`src/assets.cc` changes:**

- `LoadText()`: instead of calling `text_file_loader_.Load(&file)`,
  push to `text_files_` and insert into `text_files_table_` directly.

**`src/game.cc` changes:**

- Remove `text_files_table_` and `text_files_` members from
  `EngineModules`.
- Remove the `RegisterTextLoad` lambda from `RegisterLoaders()`.
- Replace `text_files_table_.Lookup("gamecontrollerdb", &controller_db)`
  with `assets->LookupTextFile("gamecontrollerdb")`.

#### Step 3b: Create `engine.h/cc`, rename struct

**`src/engine.h`:**

- Move the `EngineModules` struct here, renamed to `Engine`.
- Include all subsystem headers needed for by-value members.
- Same constructor signature (minus `text_files_*` members).
- Same public methods: `Initialize()`, `Deinitialize()`, `StartFrame()`,
  `ForwardEventToLua()`, `Reload()`, `HandleEvent()`.
- `RegisterLoaders()` disappears as a separate method — its body is
  inlined into `Initialize()`.

**`src/engine.cc`:**

- Method implementations move here from `game.cc`.
- All `static_cast<EngineModules*>` → `static_cast<Engine*>`.
- Fold the 7 remaining loader registrations (shader, script, image,
  spritesheet, sprite, sound, font) directly into `Initialize()` before
  the `assets->Load()` call.

**`CMakeLists.txt`:** Add `src/engine.cc` to the source list.

**`src/game.cc`:** `#include "engine.h"`, change `EngineModules* e_` →
`Engine* e_`, change `New<EngineModules>` → `New<Engine>`, update
destructor comment.

#### Step 4: Flatten `Game` into `RunGame()`

Dissolve the `Game` class entirely. Its constructor/destructor become
setup/teardown code in `RunGame()`. Its methods become anonymous-namespace
free functions.

**Audio callback context:** `StaticAudioCallback` currently captures
`Game*` and accesses `game->e_->sound` and `game->audio_buf_`. After
flattening, introduce a small struct:

```cpp
struct AudioCallbackContext {
  Sound* sound;
  float buf[kAudioBufFloats];
};
```

Allocated from the arena, passed as SDL audio callback userdata.

**HotReloadManager ownership:** Moves from inside `Engine` to local
scope in `RunGame()`. `Engine` no longer knows about hot reload.
`Engine::Initialize()` no longer calls `pool.Start()` or
`hot_reload.Start()` — those move to `RunGame()`.
`Engine::Deinitialize()` becomes just `pool.Shutdown()`.

**New `game.cc` structure:**

```
namespace G {
namespace {

constexpr size_t kEngineMemory = Gigabytes(4);

void TakeScreenshotToClipboard(BatchRenderer& br, Allocator* a) { ... }
void RenderCrashScreen(Renderer& r, BatchRenderer& br, ...) { ... }
void Update(Engine* e, double t, double real_t, ...) { ... }
void Render(Engine* e, bool debug, Stats& stats, ...) { ... }
void SDLCALL StaticAudioCallback(...) { ... }

}  // namespace

int RunGame(const GameOptions& opts, sqlite3* db) {
  // 1. Allocator setup (existing static ArenaAllocator)
  // 2. Logging, PhysFS, asset DB setup (from Game ctor)
  // 3. Config load + validation
  // 4. SDL init (audio callback uses AudioCallbackContext)
  // 5. Engine construction + Initialize
  // 6. HotReloadManager construction + Start
  // 7. pool.Start()
  // 8. Main loop (from Game::Run, using engine.field directly)
  // 9. Teardown: hot_reload.Stop, pool.Shutdown, audio stream
  //    destroy, PhysFS deinit, ShutdownSdl
}

int Main(int argc, const char* argv[]) { ... }  // unchanged
}
```

**Teardown ordering (critical):** The audio stream must be destroyed
before `Engine` (which owns the `Sound` mutex). Then `Engine` is
destroyed, then PhysFS, then SDL shutdown. This matches the current
`~Game()` ordering, just expressed as sequential statements instead of
destructor ordering.

#### Step 5: Clean up

- Remove dead includes from `game.cc` (subsystem headers now in
  `engine.cc`).
- Run `game-format`.
- Run `game-build` and `game-test` to verify.

#### Files modified

| File | Action |
|------|--------|
| `src/engine.h` | **New** — `Engine` struct declaration |
| `src/engine.cc` | **New** — `Engine` method implementations |
| `src/assets.h` | Add text file storage, `LookupTextFile()`, remove `RegisterTextLoad()` |
| `src/assets.cc` | `LoadText()` stores directly, remove callback |
| `src/game.cc` | Remove `EngineModules`, flatten `Game` into `RunGame()` |
| `CMakeLists.txt` | Add `src/engine.cc` |

## What This Does NOT Change

- The main loop structure (fixed-timestep accumulator) stays the same.
- Subsystem APIs (`Renderer`, `Sound`, `Physics`, etc.) are untouched.
- The `DbAssets` callback-based loading pattern stays (just cleaner wiring).
- Memory allocation strategy is unchanged.
- Thread model is unchanged (main thread + file watcher + thread pool).
- No new abstractions or indirection layers are introduced.
