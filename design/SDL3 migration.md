# SDL3 Migration

## Current State

The engine targets the SDL2 API but runs on SDL3 through the sdl2-compat
shim. The Nix devenv pulls in `SDL2` which resolves to `sdl2-compat-2.32.x`
backed by SDL3 3.4.0. This works but has drawbacks:

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

### Window creation

SDL3 still has `SDL_CreateWindow` but the signature changes. Position is no
longer a parameter (use `SDL_SetWindowPosition` after creation or use
properties). `SDL_WINDOW_SHOWN` is removed. `SDL_WINDOW_FULLSCREEN_DESKTOP`
is gone; use `SDL_WINDOW_FULLSCREEN` (which is always borderless in SDL3,
exclusive fullscreen uses `SDL_SetWindowFullscreenMode`).

```cpp
// Before
SDL_CreateWindow(title, x, y, w, h, flags);
// After
SDL_CreateWindow(title, w, h, flags);
SDL_SetWindowPosition(window, x, y);
```

Display mode queries change:
```cpp
// Before
SDL_DisplayMode mode;
SDL_GetCurrentDisplayMode(0, &mode);
// After
const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
```

`SDL_GL_GetDrawableSize` is removed; use `SDL_GetWindowSizeInPixels`.

### OpenGL context

`SDL_GL_SetAttribute` / `SDL_GL_CreateContext` / `SDL_GL_SwapWindow` still
exist with the same semantics. `SDL_GL_GetProcAddress` changes return type
from `void*` to `SDL_FunctionPointer`. GLAD's loader typedef may need a cast.

`SDL_GL_DeleteContext` becomes `SDL_GL_DestroyContext`.

`SDL_GL_SetSwapInterval` still works.

### Audio (largest change)

The callback-based audio model is removed entirely. SDL3 uses
`SDL_AudioStream` objects bound to devices.

Our audio system (`sound.h`, `game.cc`) currently registers a
`StaticAudioCallback` that the SDL audio thread calls each frame to fill a
buffer. In SDL3 the equivalent is:

```cpp
SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, 44100 };
SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
    SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, AudioCallback, userdata);
SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(stream));
```

SDL3 still supports a callback via `SDL_OpenAudioDeviceStream` -- this is the
closest 1:1 replacement. The callback signature changes:

```cpp
// Before
void Callback(void* userdata, uint8_t* buffer, int length_in_bytes);
// After
void Callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount);
```

Inside the new callback, instead of writing directly to a buffer, push data
to the stream:

```cpp
void AudioCallback(void* userdata, SDL_AudioStream* stream,
                   int additional_amount, int /*total_amount*/) {
    float buf[additional_amount / sizeof(float)];
    // ... fill buf with samples ...
    SDL_PutAudioStreamData(stream, buf, additional_amount);
}
```

Key differences:
- `AUDIO_F32SYS` becomes `SDL_AUDIO_F32`.
- `SDL_AudioSpec` loses `silence`, `samples`, `size` fields. Only `format`,
  `channels`, `freq` remain.
- `SDL_PauseAudioDevice(dev, 0)` becomes `SDL_ResumeAudioDevice(dev)`.
  `SDL_PauseAudioDevice(dev)` takes no boolean parameter.
- `SDL_CloseAudioDevice` still exists.
- No more `SDL_LockAudioDevice`/`SDL_UnlockAudioDevice`. Stream operations
  are thread-safe. If we need synchronization for our own state accessed from
  the callback, use our own mutex (we already have one in Sound).

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

### Gamepad (was GameController)

The entire `SDL_GameController*` API is renamed to `SDL_Gamepad*`:

```
SDL_GameControllerOpen       -> SDL_OpenGamepad
SDL_GameControllerClose      -> SDL_CloseGamepad
SDL_GameControllerName       -> SDL_GetGamepadName
SDL_GameControllerGetAxis    -> SDL_GetGamepadAxis
SDL_GameControllerGetJoystick -> SDL_GetGamepadJoystick
SDL_IsGameController         -> SDL_IsGamepad
SDL_GameControllerAddMappingsFromRW -> SDL_AddGamepadMappingsFromIO
```

Button constants change from abstract labels to positional names:
```
SDL_CONTROLLER_BUTTON_A     -> SDL_GAMEPAD_BUTTON_SOUTH
SDL_CONTROLLER_BUTTON_B     -> SDL_GAMEPAD_BUTTON_EAST
SDL_CONTROLLER_BUTTON_X     -> SDL_GAMEPAD_BUTTON_WEST
SDL_CONTROLLER_BUTTON_Y     -> SDL_GAMEPAD_BUTTON_NORTH
SDL_CONTROLLER_BUTTON_START -> SDL_GAMEPAD_BUTTON_START
SDL_CONTROLLER_BUTTON_BACK  -> SDL_GAMEPAD_BUTTON_BACK
SDL_CONTROLLER_BUTTON_DPAD_* -> SDL_GAMEPAD_BUTTON_DPAD_*
```

Axis constants:
```
SDL_CONTROLLER_AXIS_LEFTX       -> SDL_GAMEPAD_AXIS_LEFTX
SDL_CONTROLLER_AXIS_TRIGGERLEFT -> SDL_GAMEPAD_AXIS_LEFT_TRIGGER
(etc.)
```

Device enumeration changes from index-based to ID-array:
```cpp
// Before
for (int i = 0; i < SDL_NumJoysticks(); ++i) { ... }
// After
int count;
SDL_JoystickID* ids = SDL_GetGamepads(&count);
for (int i = 0; i < count; ++i) { ... }
SDL_free(ids);
```

### Joystick

```
SDL_JoystickInstanceID     -> SDL_GetJoystickID
SDL_JoystickEventState     -> SDL_SetJoystickEventsEnabled
SDL_GameControllerEventState -> SDL_SetGamepadEventsEnabled
SDL_NumJoysticks           -> SDL_GetJoysticks (returns ID array)
```

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

- **Audio latency**: The stream-based model may behave differently under
  load. The callback variant (`SDL_OpenAudioDeviceStream`) should be
  equivalent but needs testing with the QOA decoder.
- **Gamepad button mapping**: Positional names (South/East/West/North) may
  confuse Lua scripts that use "a"/"b"/"x"/"y". Keep the Lua-facing names
  unchanged and translate internally.
- **Float coordinates**: Mouse events report float x/y. Our input system
  likely truncates to int. Verify no precision issues at window edges.
