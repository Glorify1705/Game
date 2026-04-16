---
status: implemented
tags: [sdl, migration, core]
---

# SDL3 Migration

## Completion Note

This migration is **complete**. SDL3 is vendored at `libraries/SDL3`, all
source files use SDL3 headers and APIs directly, the sdl2-compat shim has been
removed, and `cmake/FindSDL2.cmake` is deleted. The audio system uses
`SDL_OpenAudioDeviceStream`, events use `SDL_EVENT_*` constants, gamepad uses
`SDL_Gamepad`, and I/O uses `SDL_IOStream`. The remainder of this document is
the original migration plan, preserved for reference.

## Original State (Pre-Migration)

The engine targeted the SDL2 API but ran on SDL3 through the sdl2-compat
shim. The Nix devenv pulled in `SDL2` which resolved to `sdl2-compat-2.32.x`
backed by SDL3 3.4.0. This worked but had drawbacks:

- SDL3 prints its own startup metadata ("App name: SDL Application", "App ID:
  \<unspecified\>", "SDL revision: ...") that we cannot fully control from the
  SDL2 API surface.
- ASan's dlopen interceptor cannot find `libSDL3.so.0` loaded at runtime by
  sdl2-compat because the RPATH is not followed, requiring manual
  `LD_LIBRARY_PATH` workarounds.
- We pay for two layers of indirection (our code -> SDL2 ABI -> sdl2-compat
  translation -> SDL3) with no benefit.
- We cannot use SDL3 features (SDL_AudioStream, properties-based window
  creation, `SDL_SetAppMetadata`, improved gamepad API).

Migrating directly to SDL3 removes the compat shim and gives us a single,
well-supported dependency.

## Scope

Migrate all engine SDL2 calls to SDL3. Remove the sdl2-compat dependency.
No functional changes to the engine beyond what SDL3 requires.

## Files to Change

| File | Subsystem | Effort |
|------|-----------|--------|
| `devenv.nix` | Build env: SDL2 -> SDL3 package | Low |
| `CMakeLists.txt` | `find_package(SDL3)`, link `SDL3::SDL3` | Low |
| `cmake/FindSDL2.cmake` | Delete (SDL3 ships its own CMake config) | Low |
| `game.cc` | Init, window, GL context, audio device, event loop | High |
| `input.cc`, `input.h` | Event types, keyboard/mouse/gamepad structs | High |
| `sound.h` | Audio spec, callback removal | High |
| `clock.cc` | Timer calls (trivial renames) | Low |
| `console.h` | Log output function | Low |
| `thread.h`, `thread_pool.h`, `thread_pool.cc` | Mutex/cond/thread API | Medium |
| `lua_system.cc` | `SDL_GetPlatform`, `SDL_OpenURL`, clipboard | Low |
| `lua_graphics.cc` | Window queries | Low |

## Breaking Changes by Subsystem

### Build system

Replace `find_package(SDL2 REQUIRED)` with `find_package(SDL3 REQUIRED CONFIG)`
and link against `SDL3::SDL3`. Delete `cmake/FindSDL2.cmake` (SDL3 ships its
own config-file package). On Windows, copy `SDL3.dll` instead of `SDL2.dll`.

Update `devenv.nix` to depend on `SDL3` directly and remove SDL2_mixer (unused).

Include `SDL3/SDL_main.h` explicitly in `game.cc` (it is no longer pulled in
by `SDL.h`). The separate `libSDLmain` static library is gone; SDL_main.h is
header-only in SDL3.

### Initialization

```
SDL_INIT_TIMER         -> removed (delete from flags)
SDL_INIT_GAMECONTROLLER -> SDL_INIT_GAMEPAD
```

Return values change from `int < 0` to `bool`:
```cpp
// Before
CHECK(SDL_Init(...) == 0, ...);
// After
CHECK(SDL_Init(...), ...);
```

Set app metadata before `SDL_Init`:
```cpp
SDL_SetAppMetadata(config.app_name, GAME_VERSION_STR, /*appid=*/nullptr);
```

### Window and OpenGL context

The window subsystem touches `game.cc` (CreateWindow, CreateOpenglContext,
GetWindowViewport) and `lua_graphics.cc` (the `kWindowLib` Lua API).

**`SDL_CreateWindow` signature change.** SDL3 drops the x/y position
parameters. Our centered-window path currently computes window_x/window_y
from the display mode and passes them to `SDL_CreateWindow`. In SDL3:

```cpp
// Before (game.cc:744)
window = SDL_CreateWindow(title, window_x, window_y, w, h, flags | SDL_WINDOW_SHOWN);
// After
window = SDL_CreateWindow(title, w, h, flags);
SDL_SetWindowPosition(window, window_x, window_y);
```

The non-centered path used `SDL_WINDOWPOS_UNDEFINED` which is also gone.
Just create the window and let the WM place it (SDL3 default behavior).

**`SDL_WINDOW_SHOWN` is removed.** Windows are shown by default in SDL3.
Drop the flag entirely.

**`SDL_WINDOW_FULLSCREEN_DESKTOP` is gone.** SDL3 unifies fullscreen:
`SDL_WINDOW_FULLSCREEN` is always borderless (desktop) fullscreen. For
exclusive fullscreen, call `SDL_SetWindowFullscreenMode` with a display mode
before entering fullscreen. This affects three places:

1. `CreateWindow` (game.cc:721) -- `SDL_WINDOW_FULLSCREEN` flag still works
   but is now borderless. If the config says `fullscreen`, this is probably
   fine (borderless is the better default).

2. `set_fullscreen` in `kWindowLib` (lua_graphics.cc:681) -- currently passes
   `SDL_WINDOW_FULLSCREEN` for exclusive fullscreen. In SDL3 this becomes
   borderless. To get exclusive, we would need to set a fullscreen mode first
   via `SDL_SetWindowFullscreenMode`. For now, just make this borderless too
   (simpler and avoids mode-switch flicker).

3. `set_borderless` in `kWindowLib` (lua_graphics.cc:690) -- currently passes
   `SDL_WINDOW_FULLSCREEN_DESKTOP`. In SDL3 this is just
   `SDL_SetWindowFullscreen(window, true)`. Both `set_fullscreen` and
   `set_borderless` collapse to the same call.

4. `set_windowed` (lua_graphics.cc:700) -- currently passes `0`. In SDL3
   use `SDL_SetWindowFullscreen(window, false)`.

**Display mode queries.** Our centered-window code calls
`SDL_GetCurrentDisplayMode(0, &mode)` with display index 0. SDL3 replaces
index-based displays with display IDs:

```cpp
// Before (game.cc:731)
SDL_DisplayMode display_mode;
SDL_GetCurrentDisplayMode(0, &display_mode);
// After
SDL_DisplayID display = SDL_GetPrimaryDisplay();
const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
```

The returned pointer is owned by SDL -- do not free it. Fields `w`, `h`,
`refresh_rate` are still present.

**`SDL_GL_GetDrawableSize` is removed.** We use this in
`GetWindowViewport` (game.cc:101) to get the actual pixel dimensions
(accounting for HiDPI). Replace with `SDL_GetWindowSizeInPixels`:

```cpp
// Before
SDL_GL_GetDrawableSize(window, &result.x, &result.y);
// After
SDL_GetWindowSizeInPixels(window, &result.x, &result.y);
```

Note: SDL3 always enables HiDPI (`SDL_HINT_VIDEO_HIGHDPI_DISABLED` is
removed). This is fine since we already use drawable size for the viewport.

**`SDL_GL_GetProcAddress` return type changes** from `void*` to
`SDL_FunctionPointer`. Our GLAD loader call (game.cc:771) needs a cast:

```cpp
// Before
gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
// After
gladLoadGLLoader((GLADloadproc)(void(*)(void))SDL_GL_GetProcAddress);
// Or just cast inline -- GLAD expects void*(*)(const char*).
```

Actually, `SDL_FunctionPointer` is `void(*)(void)` which is compatible with
what GLAD expects after a cast. The simplest fix is:

```cpp
gladLoadGLLoader([](const char* name) -> void* {
    return (void*)SDL_GL_GetProcAddress(name);
});
```

**`SDL_GL_DeleteContext` -> `SDL_GL_DestroyContext`.** One rename in the
destructor (game.cc:499).

**`SDL_GL_SetAttribute` return value changes** from `int` (0 on success)
to `bool`. Our CHECK macros comparing `== 0` need to drop the comparison.

**`SDL_GL_SetSwapInterval` return value changes** similarly to `bool`.

**`SDL_SetWindowFullscreen` signature change.** SDL2 took a uint32 flags
argument (0, SDL_WINDOW_FULLSCREEN, or SDL_WINDOW_FULLSCREEN_DESKTOP). SDL3
takes a bool:

```cpp
// Before
SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
SDL_SetWindowFullscreen(window, 0);
// After
SDL_SetWindowFullscreen(window, true);   // borderless fullscreen
SDL_SetWindowFullscreen(window, true);   // same -- both become borderless
SDL_SetWindowFullscreen(window, false);  // windowed
```

**`SDL_ShowCursor` changes.** Currently called as `SDL_ShowCursor(false)` in
`InitializeSDL`. SDL3 splits this into `SDL_ShowCursor()` / `SDL_HideCursor()`:

```cpp
// Before
SDL_ShowCursor(false);
// After
SDL_HideCursor();
```

### Audio (largest change)

The SDL2 callback-based audio model is removed in SDL3. This is the highest-
risk change because it touches the real-time audio path where bugs manifest
as audible glitches.

#### Current architecture

The audio pipeline currently works like this:

1. `InitializeSDL` (game.cc:637-654) opens a device with `SDL_OpenAudioDevice`
   requesting 44100 Hz, `AUDIO_F32SYS`, 2 channels, 256 samples, and a
   callback function `StaticAudioCallback`.

2. SDL creates an audio thread that calls `StaticAudioCallback` whenever the
   device needs more samples. The callback casts the userdata to `Game*` and
   calls `Sound::SoundCallback(float* result, samples_per_channel, channels)`.

3. `SoundCallback` (sound.cc:410-425) acquires `mu_` (an `SDL_mutex`), zeros
   the output buffer, iterates all active streams calling `stream.Load()` to
   decode/resample into a per-stream `buffer_`, accumulates into the output,
   and applies `global_gain_`.

4. Each Stream (sound.h:125-249) has a 2048-sample internal ring that it
   fills from either a `QoaSampler` (streaming QOA decode, one frame at a
   time) or a `PcmSampler` (pre-decoded float buffer for effects). Pitch
   shifting uses linear interpolation; panning uses equal-power pan law.
   Looping uses a 20ms crossfade to avoid clicks.

5. The mutex `mu_` synchronizes the callback thread with the main thread.
   All public `Sound` methods (AddSource, Stop, SetGain, etc.) acquire `mu_`.
   We do NOT use `SDL_LockAudioDevice`/`SDL_UnlockAudioDevice`.

6. `SDL_PauseAudioDevice(audio_device_, 0)` in `Game::Run()` starts playback.
   `SDL_CloseAudioDevice` in `~Game()` stops it (ordered before destroying
   EngineModules to prevent use-after-free of the mutex).

#### SDL3 replacement: `SDL_OpenAudioDeviceStream`

SDL3 provides `SDL_OpenAudioDeviceStream` which accepts a callback -- this is
the direct replacement for our pattern. The key insight is that our
architecture barely changes: we still get called on the audio thread to
produce samples, we just push them to a stream instead of writing to a
raw buffer.

```cpp
// Before (game.cc, InitializeSDL)
SDL_AudioSpec desired_spec;
std::memset(&desired_spec, 0, sizeof(desired_spec));
desired_spec.freq = 44100;
desired_spec.format = AUDIO_F32SYS;
desired_spec.samples = 256;
desired_spec.callback = &StaticAudioCallback;
desired_spec.channels = 2;
desired_spec.userdata = this;
audio_device_ = SDL_OpenAudioDevice(nullptr, 0, &desired_spec, &obtained_spec_, 0);

// After
SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, 44100 };
audio_stream_ = SDL_OpenAudioDeviceStream(
    SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, StaticAudioCallback, this);
CHECK(audio_stream_ != nullptr, "Could not open audio stream: ", SDL_GetError());
audio_device_ = SDL_GetAudioStreamDevice(audio_stream_);
```

The callback signature changes:

```cpp
// Before
static void StaticAudioCallback(void* userdata, uint8_t* buffer, int length_in_bytes) {
    auto* game = static_cast<Game*>(userdata);
    game->e_->sound.SoundCallback(
        reinterpret_cast<float*>(buffer),
        length_in_bytes / sizeof(float) / obtained_spec_.channels,
        obtained_spec_.channels);
}

// After
static void SDLCALL StaticAudioCallback(void* userdata, SDL_AudioStream* stream,
                                         int additional_amount, int /*total_amount*/) {
    auto* game = static_cast<Game*>(userdata);
    const int channels = 2;
    const int samples_per_channel = additional_amount / sizeof(float) / channels;
    // Use a stack buffer. additional_amount is typically small (256 * 2 * 4 = 2048 bytes).
    float buf[samples_per_channel * channels];
    game->e_->sound.SoundCallback(buf, samples_per_channel, channels);
    SDL_PutAudioStreamData(stream, buf, additional_amount);
}
```

The crucial difference: SDL2 gave us a buffer to fill in-place; SDL3 expects
us to allocate a buffer, fill it, then push it. For our 256-sample buffer
at 2 channels of float32, that's 2048 bytes on the stack -- fine.

#### What changes in `Sound`

Almost nothing. `Sound::SoundCallback` writes to a caller-provided `float*`
buffer -- it does not know or care whether that buffer came from SDL or from
our callback wrapper. The signature stays the same.

Changes required in `sound.h`:

1. **Constructor**: currently takes `const SDL_AudioSpec& spec` and uses
   `spec.channels * spec.samples` to size `buffer_`. SDL3's `SDL_AudioSpec`
   no longer has a `samples` field. We should pass channels and buffer size
   explicitly instead of the SDL spec:

   ```cpp
   // Before
   explicit Sound(const SDL_AudioSpec& spec, Allocator* allocator)
       : buffer_(static_cast<size_t>(spec.channels) * spec.samples, allocator), ...

   // After
   explicit Sound(size_t channels, size_t samples_per_channel, Allocator* allocator)
       : buffer_(channels * samples_per_channel, allocator), ...
   ```

   The `buffer_` is a scratch buffer used per-stream in the callback
   (sound.cc:417). Its size needs to be at least `samples_per_channel *
   channels` for the largest callback request. With SDL3's stream callback,
   `additional_amount` can vary. We should size `buffer_` generously (e.g.
   4096 samples) and handle the case where a callback requests more than
   `buffer_` can hold by processing in chunks.

   Actually, looking more carefully: `additional_amount` is in bytes and
   corresponds to how much data the stream wants. With
   `SDL_OpenAudioDeviceStream`, SDL internally manages the device buffer size.
   The callback will typically request the same amount each time but the
   exact size is not guaranteed. We should either:
   - Use a `DynArray` for `buffer_` and resize on demand, or
   - Keep a fixed 4096-sample buffer and process in chunks if needed.

   The chunked approach is safer for real-time (no allocation in the callback).

2. **Mutex type**: `SDL_mutex*` -> `SDL_Mutex*` (typedef rename, same
   pointer). `SDL_CreateMutex` / `SDL_DestroyMutex` / `SDL_LockMutex` /
   `SDL_UnlockMutex` names are unchanged in SDL3. No changes needed to
   the locking code.

#### Device lifecycle changes

```cpp
// Before
SDL_PauseAudioDevice(audio_device_, 0);  // unpause to start
SDL_CloseAudioDevice(audio_device_);     // stop and close

// After
SDL_ResumeAudioDevice(audio_device_);    // start playback
SDL_DestroyAudioStream(audio_stream_);   // cleans up stream and device
```

We no longer call `SDL_CloseAudioDevice` directly. Destroying the stream
unbinds it from the device. If we opened via `SDL_OpenAudioDeviceStream`
with `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`, the device is managed by SDL and
closed when the last stream is destroyed.

The destructor ordering in `~Game()` still matters: destroy the audio stream
before destroying `EngineModules` (which owns Sound and its mutex).

#### `SDL_AudioSpec` changes

SDL3's `SDL_AudioSpec` is just `{ SDL_AudioFormat format; int channels;
int freq; }`. No `samples`, `silence`, `size`, `callback`, or `userdata`
fields. We store `obtained_spec_` (game.cc:798) and pass it to the Sound
constructor. After migration, we no longer need `obtained_spec_` at all --
the spec is exactly what we requested (SDL handles conversion internally
via the audio stream).

#### Validation we currently do

After `SDL_OpenAudioDevice`, we log warnings if the obtained spec differs
from the desired spec (game.cc:647-658). In SDL3 with
`SDL_OpenAudioDeviceStream`, the stream handles format conversion
transparently, so these checks are no longer needed. We can just log the
requested spec.

#### Summary of audio changes

| Component | Change | Risk |
|-----------|--------|------|
| `game.cc` InitializeSDL | Replace `SDL_OpenAudioDevice` with `SDL_OpenAudioDeviceStream` | Low -- direct replacement |
| `game.cc` StaticAudioCallback | New signature, allocate stack buffer, push via `SDL_PutAudioStreamData` | Medium -- verify buffer sizing |
| `game.cc` ~Game | `SDL_DestroyAudioStream` instead of `SDL_CloseAudioDevice` | Low |
| `game.cc` Run | `SDL_ResumeAudioDevice` instead of `SDL_PauseAudioDevice(dev, 0)` | Low |
| `sound.h` constructor | Take explicit channels + buffer size instead of `SDL_AudioSpec` | Low |
| `sound.h` buffer_ | Consider oversizing or chunking for variable callback sizes | Medium |
| `Sound::SoundCallback` | No change | None |
| All stream/sampler code | No change | None |
| Mutex synchronization | No change (we use our own mutex, not SDL audio lock) | None |

### Events

All event constants gain an `SDL_EVENT_` prefix. Window sub-events are
promoted to top-level events:

```
SDL_QUIT                    -> SDL_EVENT_QUIT
SDL_KEYDOWN                 -> SDL_EVENT_KEY_DOWN
SDL_KEYUP                   -> SDL_EVENT_KEY_UP
SDL_MOUSEBUTTONDOWN         -> SDL_EVENT_MOUSE_BUTTON_DOWN
SDL_MOUSEBUTTONUP           -> SDL_EVENT_MOUSE_BUTTON_UP
SDL_MOUSEMOTION             -> SDL_EVENT_MOUSE_MOTION
SDL_MOUSEWHEEL              -> SDL_EVENT_MOUSE_WHEEL
SDL_TEXTINPUT               -> SDL_EVENT_TEXT_INPUT
SDL_CONTROLLERDEVICEADDED   -> SDL_EVENT_GAMEPAD_ADDED
SDL_JOYDEVICEREMOVED        -> SDL_EVENT_JOYSTICK_REMOVED
SDL_JOYBUTTONDOWN           -> SDL_EVENT_JOYSTICK_BUTTON_DOWN
SDL_JOYBUTTONUP             -> SDL_EVENT_JOYSTICK_BUTTON_UP
```

Window events are no longer nested:
```cpp
// Before
if (event.type == SDL_WINDOWEVENT &&
    event.window.event == SDL_WINDOWEVENT_RESIZED) { ... }
// After
if (event.type == SDL_EVENT_WINDOW_RESIZED) { ... }
```

Keyboard event struct changes:
```cpp
// Before
event.key.keysym.scancode
event.key.keysym.mod
// After
event.key.scancode
event.key.mod
```

Mouse events use `float` coordinates. Mouse button constants unchanged.

### Gamepad and Joystick

No games currently use the gamepad, so no backwards compatibility is needed.
Just do the mechanical renames: `SDL_GameController*` -> `SDL_Gamepad*`,
`SDL_CONTROLLER_BUTTON_*` -> `SDL_GAMEPAD_BUTTON_*` (with positional names:
A/B/X/Y become South/East/West/North), index-based enumeration
(`SDL_NumJoysticks`) -> ID-array enumeration (`SDL_GetGamepads`),
`SDL_RWFromMem` -> `SDL_IOFromMem` for controller DB loading,
`SDL_JoystickEventState`/`SDL_GameControllerEventState` ->
`SDL_SetJoystickEventsEnabled`/`SDL_SetGamepadEventsEnabled`.

### Threading

```
SDL_CreateMutex    -> SDL_CreateMutex (returns SDL_Mutex*, no change)
SDL_DestroyMutex   -> SDL_DestroyMutex
SDL_LockMutex      -> SDL_LockMutex (returns void, not int)
SDL_UnlockMutex    -> SDL_UnlockMutex (returns void, not int)
SDL_CreateCond     -> SDL_CreateCondition
SDL_DestroyCond    -> SDL_DestroyCondition
SDL_CondWait       -> SDL_WaitCondition
SDL_CondSignal     -> SDL_SignalCondition
SDL_CondBroadcast  -> SDL_BroadcastCondition
SDL_CreateThread   -> SDL_CreateThread (callback returns int, not Sint32)
SDL_WaitThread     -> SDL_WaitThread
```

Types: `SDL_cond` -> `SDL_Condition`, `SDL_mutex` -> `SDL_Mutex`.

### Logging

```
SDL_LogSetAllPriority    -> SDL_SetLogPriorities
SDL_LogCritical          -> SDL_LogCritical (unchanged)
SDL_LogInfo              -> SDL_LogInfo (unchanged)
SDL_LogGetOutputFunction -> SDL_GetLogOutputFunction
SDL_LogSetOutputFunction -> SDL_SetLogOutputFunction
```

Log output callback signature changes:
```cpp
// Before
void Callback(void* userdata, int category, SDL_LogPriority priority,
              const char* message);
// After
void Callback(void* userdata, int category, SDL_LogPriority priority,
              const char* message);  // same signature
```

### Timing

```
SDL_GetPerformanceCounter   -> SDL_GetPerformanceCounter (unchanged)
SDL_GetPerformanceFrequency -> SDL_GetPerformanceFrequency (unchanged)
SDL_Delay                   -> SDL_Delay (unchanged, but also SDL_DelayNS)
```

### Atomics

```
SDL_atomic_t   -> SDL_AtomicInt
SDL_AtomicGet  -> SDL_GetAtomicInt
SDL_AtomicSet  -> SDL_SetAtomicInt
```

### Miscellaneous

```
SDL_GetPlatform       -> SDL_GetPlatform (unchanged)
SDL_GetCPUCount       -> SDL_GetNumLogicalCPUCores
SDL_GetVersion        -> SDL_GetVersion (returns int, not void)
SDL_ShowSimpleMessageBox -> SDL_ShowSimpleMessageBox (unchanged)
SDL_ShowCursor(bool)  -> SDL_ShowCursor() / SDL_HideCursor()
SDL_RWFromMem         -> SDL_IOFromMem
SDL_RWops             -> SDL_IOStream
SDL_OpenURL           -> SDL_OpenURL (unchanged)
SDL_SetClipboardText  -> SDL_SetClipboardText (unchanged)
SDL_GetClipboardText  -> SDL_GetClipboardText (unchanged)
```

### Hints

```
SDL_HINT_RENDER_DRIVER -> removed (we set this to "opengl", not needed)
SDL_HINT_APP_NAME      -> SDL_HINT_APP_NAME (unchanged, or use SDL_SetAppMetadata)
SDL_HINT_X11_WINDOW_TYPE -> SDL_HINT_X11_WINDOW_TYPE (unchanged)
```

## Migration Order

Do this in a single branch. The compat layer means the engine already runs
on SDL3 -- the migration is making the API calls direct.

1. **Build system** -- switch devenv.nix and CMakeLists.txt to SDL3. Get it
   compiling with SDL3 headers. Delete `cmake/FindSDL2.cmake`.

2. **Mechanical renames** -- event constants, gamepad/joystick API, threading
   primitives, atomics, logging functions, misc utilities. These are high
   volume but trivial. SDL provides `rename_symbols.py` /
   `rename_headers.py` / `rename_macros.py` scripts but given the small
   number of files, manual search-and-replace is fine.

3. **Window creation** -- update `SDL_CreateWindow` call signature, display
   mode queries, drawable size. Add `SDL_SetAppMetadata` before `SDL_Init`.

4. **Audio** -- replace `SDL_OpenAudioDevice` + callback with
   `SDL_OpenAudioDeviceStream`. Update `SoundCallback` to push data via
   `SDL_PutAudioStreamData`. This is the riskiest change; test carefully
   for latency and glitches.

5. **Cleanup** -- remove `SDL2_mixer` from devenv.nix (unused), remove the
   `LD_LIBRARY_PATH` workaround for sdl2-compat, remove `SDL_MAIN_HANDLED`
   / update SDL_main include.

## Risk

- **Audio callback buffer sizing**: SDL3's stream callback may request
  varying amounts of data via `additional_amount`. If a request exceeds our
  scratch `buffer_` (currently sized to `channels * samples` = 512 floats),
  we get a buffer overrun. Mitigate by oversizing the scratch buffer or
  processing in chunks. Test with different audio backends (PulseAudio,
  PipeWire, ALSA) which may have different buffer size preferences.
- **Audio latency**: `SDL_OpenAudioDeviceStream` manages its own internal
  buffering. We lose direct control over the device buffer size (the old
  `spec.samples = 256`). If latency is noticeably worse, we can tune it
  via `SDL_SetAudioStreamFrequencyRatio` or by opening the device manually
  with `SDL_OpenAudioDevice` + `SDL_CreateAudioStream` + `SDL_BindAudioStream`
  for more control. This is a fallback, not the initial approach.
- **Fullscreen mode collapse**: SDL2 distinguished exclusive (`FULLSCREEN`)
  from borderless (`FULLSCREEN_DESKTOP`). SDL3 only has borderless. If a game
  later needs exclusive fullscreen for performance (GPU bypass), we would need
  `SDL_SetWindowFullscreenMode` with an explicit display mode. Not a concern
  today.
- **Float coordinates**: Mouse events report float x/y in SDL3. Our input
  system likely truncates to int. Verify no precision issues at window edges.
- **HiDPI always on**: SDL3 removes `SDL_HINT_VIDEO_HIGHDPI_DISABLED`. We
  already use drawable size for the viewport, so this should be fine, but
  test on HiDPI displays to make sure window dimensions and mouse coordinates
  remain consistent.
