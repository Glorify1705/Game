# Test Input System

## 1. Problem statement

There is no way to programmatically test a game built with this engine. The only option is a human sitting at the keyboard pressing keys and watching the screen. This means:

- **No CI testing** — you can't run automated tests in a headless pipeline.
- **Manual testing is slow and unreliable** — humans are inconsistent, and regression testing by hand doesn't scale.
- **No reproducibility** — you can't replay the exact same sequence of inputs to verify a fix.

We need a system that injects synthetic inputs identical to real SDL inputs, driven by a script, so that a game can be tested automatically.

## 2. Architecture overview

### Current input pipeline

```
SDL_PollEvent → EngineModules::HandleEvent
                 ├→ Keyboard::PushEvent   (sets bitset: pressed_[scancode])
                 ├→ Mouse::PushEvent      (sets pressed_[button], mouse_wheel_)
                 ├→ Controllers::PushEvent (sets pressed[button])
                 └→ ForwardEventToLua     (calls _Game:keypressed, mousepressed, etc.)
```

At the start of each frame, `StartFrame()` copies current state into `previous_pressed_` (for edge detection: `IsPressed` = `!prev && cur`, `IsReleased` = `prev && !cur`, `IsDown` = `prev && cur`).

Game scripts poll via `G.input.is_key_down("space")`, `G.input.is_key_pressed("w")`, etc. Callbacks like `_Game:keypressed(scancode)` fire synchronously during event processing.

### Where synthetic inputs get injected

Synthetic inputs bypass SDL entirely. New `Inject*` methods on `Keyboard`, `Mouse`, and `Controllers` modify the bitsets/state directly — the same fields that `PushEvent` writes to. From the game's perspective, the input is indistinguishable from a real keypress.

For Lua callbacks (`keypressed`, `mousepressed`, etc.), the test system calls `ForwardEventToLua` with a synthetic `SDL_Event` struct so that callbacks fire normally.

### The coroutine model

The game defines a `test_inputs()` method on its `_Game` table. The engine wraps it in a Lua coroutine and resumes it once per frame. Each `G.test.*` call that needs to wait calls `coroutine.yield()` internally, suspending the test script until the next frame.

## 3. Coroutine execution model

A coroutine-based API lets the test script read like a sequential story:

```lua
function Game:test_inputs()
  G.test.press("space")
  G.test.wait_frames(10)
  G.test.press("right")
  G.test.wait_frames(30)
  G.test.screenshot("after_move.qoi")
end
```

Each `G.test.*` call that needs to wait calls `coroutine.yield()` internally. The engine resumes the coroutine once per frame.

### Lifecycle

1. **Creation**: After `_Game:init()` completes (and `_Game:init_test()` if defined), the engine creates the coroutine: `co = coroutine.create(_Game.test_inputs)`.

2. **First resume**: Before the first `_Game:update()` call. This lets the test inject inputs that will be visible to the very first update.

3. **Yield/resume cycle**: Each frame, the engine:
   ```
   StartFrame()
   SDL_PollEvent loop (real events still processed)
   Resume test coroutine ← here, after event polling, before Update
   while accum >= kStep: Update(t, dt)
   Draw()
   SwapWindow()
   ```
   The coroutine runs until it yields or finishes. Injected inputs are immediately visible to `Update` because they modify the same bitsets that `InitForFrame` and `PushEvent` write to.

4. **Completion**: When the coroutine returns normally → `exit(0)`. When it errors (including `G.test.assert` failure) → print error with traceback, `exit(1)`.

### Interaction with the fixed timestep loop

The engine uses a fixed timestep of `1/60s` (`TimeStepInSeconds()` in `clock.h`). `Game::Run()` accumulates real time and may call `Update` multiple times per frame to catch up. The test coroutine is resumed **once per frame** (not once per timestep tick), which means:

- `G.test.wait_frames(1)` waits for one frame, which may contain multiple `Update` ticks if the system is catching up.
- `G.test.wait_seconds(s)` waits based on game time (`t` accumulated from `kStep` increments), not wall-clock time. This ensures determinism.

For test mode specifically, the engine should cap `accum` so that only one `Update` tick runs per frame. This ensures `wait_frames(1)` always corresponds to exactly one `Update` call, making tests fully deterministic regardless of system speed.

## 4. Lua API: `G.test`

All functions live under `G.test`. Functions that yield suspend the coroutine and resume next frame.

### Keyboard

| Function | Behavior | Yields? |
|---|---|---|
| `G.test.press(key)` | Inject keydown this frame, keyup next frame. The key is visible as `IsPressed` for the current frame's update. | Yes (1 frame) |
| `G.test.key_down(key)` | Inject keydown. Key stays held until `key_up` is called. | No |
| `G.test.key_up(key)` | Inject keyup. | No |
| `G.test.text_input(text)` | Inject an `SDL_TEXTINPUT` event. Triggers `_Game:textinput(text)`. | No |

The `key` parameter uses the same string names as `G.input.is_key_down`: `"space"`, `"w"`, `"escape"`, `"lctrl"`, etc. — mapped via `Keyboard::MapKey` to `SDL_Scancode` values.

### Mouse

| Function | Behavior | Yields? |
|---|---|---|
| `G.test.mouse_move(x, y)` | Set mouse position. Triggers `_Game:mousemoved`. | No |
| `G.test.mouse_press(button)` | Inject mousedown this frame, mouseup next frame. | Yes (1 frame) |
| `G.test.mouse_down(button)` | Inject mousedown, stays held until `mouse_up`. | No |
| `G.test.mouse_up(button)` | Inject mouseup. | No |
| `G.test.mouse_wheel(dx, dy)` | Inject scroll event. | No |

Button values: `0` = left, `1` = middle, `2` = right (matching the existing `Mouse::Button` enum).

**Note on `mouse_move`**: The current `Mouse::GetPosition()` calls `SDL_GetMouseState` directly, which returns the real cursor position. For test mode, the engine would need to either (a) use `SDL_WarpMouseInWindow` to actually move the cursor, or (b) add an override position that `GetPosition()` checks first. Option (b) is cleaner since it avoids OS interaction.

### Controller

| Function | Behavior | Yields? |
|---|---|---|
| `G.test.controller_press(button)` | Inject button press this frame, release next frame. | Yes (1 frame) |
| `G.test.controller_down(button)` | Inject button down, stays held until `controller_up`. | No |
| `G.test.controller_up(button)` | Inject button up. | No |
| `G.test.controller_axis(axis, value)` | Set axis position (`-32768` to `32767`). | No |

Button names: `"a"`, `"b"`, `"x"`, `"y"`, `"start"`, `"back"`, `"dpadl"`, `"dpadr"`, `"dpadu"`, `"dpadd"` — same strings as `Controllers::StrToButton`.

Axis names: `"lanalogx"`, `"lanalogy"`, `"ranalogx"`, `"ranalogy"`, `"ltrigger"`, `"rtrigger"` — same as `Controllers::StrToAxisOrTrigger`.

For axis injection, the engine needs a stored value per axis (since `AxisPositions` currently calls `SDL_GameControllerGetAxis` directly). Test mode would override this with injected values.

### Flow control

| Function | Behavior | Yields? |
|---|---|---|
| `G.test.wait_frames(n)` | Yield for `n` frames. | Yes (n frames) |
| `G.test.wait_seconds(s)` | Yield until `s` seconds of game time have elapsed (measured via the `t` accumulator from the fixed timestep loop). | Yes (variable) |

### Assertions / output

| Function | Behavior | Yields? |
|---|---|---|
| `G.test.screenshot(filename?)` | Capture framebuffer to file (`.qoi` format) or return as `byte_buffer` if no filename given. Reuses the existing `take_screenshot` logic in `lua_graphics.cc`. | No |
| `G.test.assert(condition, message?)` | If `condition` is falsy, fail the test: log error with traceback, set exit code to 1, stop the engine. | No |
| `G.test.log(...)` | Print to stdout. Same as `print()`/`log()` but always writes to stdout even if logging is redirected. For CI output. | No |

## 5. Engine-side changes (high level)

### `--test` CLI flag

Added to the `GameOptions` struct in `game.h`:

```cpp
struct GameOptions {
  const char* source_directory = nullptr;
  bool hotreload = true;
  bool test_mode = false;       // new
  int test_seed = 0;            // new: RNG seed for deterministic runs
  Slice<const char*> args;
};
```

Parsed in `CmdRun` (in the CLI layer). If `_Game` defines `test_inputs` but `--test` is not set, the engine logs a warning and runs normally:

```
WARNING: test_inputs defined but --test flag not passed, ignoring
```

### `init_test` callback

If `_Game` defines `init_test`, it's called after `init()` but only in test mode. This allows test-specific setup (spawning at a fixed position, disabling menus, seeding world generation) without polluting the game's normal init.

Call order in test mode:
1. `_Game:init()`
2. `_Game:init_test()` (if defined)
3. Create test coroutine from `_Game:test_inputs`
4. Enter frame loop; resume coroutine each frame before `Update`

### RNG seeding

In test mode, seed the RNG with a fixed value (default `0`, configurable via `--test-seed N`) for deterministic runs. This applies to `G.random.*` functions.

### Input injection methods

New methods on the C++ input classes that modify internal state directly:

**Keyboard:**
```cpp
void InjectKeyDown(SDL_Scancode code);  // pressed_[code] = true
void InjectKeyUp(SDL_Scancode code);    // pressed_[code] = false
```

**Mouse:**
```cpp
void InjectButtonDown(int button);   // pressed_[button] = true
void InjectButtonUp(int button);     // pressed_[button] = false
void InjectWheel(float dx, float dy); // mouse_wheel_ += FVec(dx, dy)
void SetPositionOverride(float x, float y); // for GetPosition override
void ClearPositionOverride();
```

**Controllers:**
```cpp
void InjectButtonDown(int button, int controller_id);
void InjectButtonUp(int button, int controller_id);
void SetAxisOverride(SDL_GameControllerAxis axis, int value, int controller_id);
```

These methods are simple — they just set the same bitset/state fields that `PushEvent` writes. The key difference is they don't require constructing an `SDL_Event`.

### Test coroutine management

Either in the `Lua` class or a new `TestRunner` helper:

- Store the coroutine `lua_State*` and its ref in the registry.
- Each frame, call `lua_resume(co, 0)`. Check the return:
  - `LUA_YIELD` → coroutine yielded, continue next frame.
  - `0` → coroutine finished, exit with code 0.
  - Error → print error + traceback, exit with code 1.
- The `G.test.*` C functions (registered as a Lua library) call `lua_yield(co, 0)` when they need to wait.

### Frame loop integration

In `Game::Run()`, after `StartFrame()` and `SDL_PollEvent` loop, resume the test coroutine if active:

```cpp
// After SDL_PollEvent loop, before the Update loop:
if (test_mode_ && test_coroutine_active_) {
  ResumeTestCoroutine();
}
```

This ensures injected inputs are visible to the game's `update()` via the normal `G.input.*` polling API, since `InitForFrame` has already run and copied the previous state.

### Activation logic

Test mode activates when **both** conditions are met:
1. `--test` flag is set
2. `_Game.test_inputs` exists (is a function)

If `--test` is set but `test_inputs` is not defined, warn and exit with code 1.

### Exit behavior

| Coroutine state | Action |
|---|---|
| Returns normally | `exit(0)` — test passed |
| `G.test.assert` fails | Log assertion message + traceback, `exit(1)` |
| Lua error (runtime) | Log error + traceback, `exit(1)` |

This makes the engine usable as a CI test runner: `game run . --test && echo "PASS"`.

## 6. How callbacks interact with injected inputs

Injected inputs go through the same callback path as real inputs. When the test coroutine injects a keydown:

1. `Keyboard::InjectKeyDown(scancode)` sets the bitset.
2. The test system also calls `ForwardEventToLua` with a synthetic `SDL_Event` containing the scancode.
3. `ForwardEventToLua` calls `lua.HandleKeypressed(scancode)`, which calls `_Game:keypressed(scancode)`.

So `_Game:keypressed`, `_Game:keyreleased`, `_Game:mousepressed`, `_Game:mousereleased`, `_Game:mousemoved`, and `_Game:textinput` all fire normally for injected inputs, exactly as they would for real SDL events.

## 7. Example: testing a Flappy Bird game

```lua
function Game:init_test()
  -- Test-only setup: disable menu, spawn player at known position
  self.player.x = 100
  self.player.y = 300
  self.skip_menu = true
end

function Game:test_inputs()
  -- Wait for game to settle
  G.test.wait_frames(5)

  -- Flap
  G.test.press("w")
  G.test.wait_frames(30)

  -- Verify player moved up
  G.test.assert(self.player.y < 300, "Player should have moved up after flap")

  -- Take a screenshot for visual regression
  G.test.screenshot("flap_test.qoi")

  -- Let gravity pull down
  G.test.wait_frames(60)
  G.test.assert(self.player.y > 300, "Player should fall after no input")

  G.test.log("All assertions passed")
end
```

Running:
```
game run . --test
```

With a custom seed:
```
game run . --test --test-seed 42
```

## 8. Comparison with other engines

| Engine | Approach | Built-in? |
|---|---|---|
| **Love2D** | No test input injection. Tests mock `love.keyboard` or use external tools. | No |
| **Godot** | `InputEventAction` and `Input.action_press()` inject synthetic inputs. Has `GUT` (Godot Unit Testing) framework. | Partial (input injection yes, test framework via plugin) |
| **Unity** | `UnityEngine.Input` can be mocked. The Input System package has `InputTestFixture` for synthetic events at the C# level. | Yes (Input System package) |

Our approach is most similar to Godot's: direct injection into the input state, with a coroutine-based scripting API on top. The coroutine model is simpler than callback-based test frameworks since test scripts read linearly.

## 9. Input recording and replay

The test API should support recording real inputs from a play session and replaying them. This allows creating tests by playing the game once and saving the input trace.

### API

| Function | Behavior | Yields? |
|---|---|---|
| `G.test.start_recording()` | Begin capturing all input events with frame timestamps. | No |
| `G.test.stop_recording()` | Stop recording, return a `byte_buffer` with the serialized trace. | No |
| `G.test.replay(data)` | Play back a recorded input trace, yielding per frame until complete. | Yes |

### Recording mechanism

During recording, the engine intercepts all events in `HandleEvent` and appends them to a buffer with the current frame number. The recording captures the frame-relative timing, not wall-clock time, so replays are deterministic.

### Serialization format: Protobuf

Protobuf is a natural fit for the structured, versioned binary data. The schema:

```protobuf
syntax = "proto3";

message KeyEvent {
  uint32 scancode = 1;
  bool down = 2;  // true = keydown, false = keyup
}

message MouseMoveEvent {
  float x = 1;
  float y = 2;
}

message MouseButtonEvent {
  uint32 button = 1;
  bool down = 2;
}

message MouseWheelEvent {
  float dx = 1;
  float dy = 2;
}

message ControllerButtonEvent {
  uint32 button = 1;
  bool down = 2;
  uint32 controller_id = 3;
}

message ControllerAxisEvent {
  uint32 axis = 1;
  int32 value = 2;
  uint32 controller_id = 3;
}

message InputEvent {
  uint32 frame = 1;
  oneof event {
    KeyEvent key = 2;
    MouseMoveEvent mouse_move = 3;
    MouseButtonEvent mouse_button = 4;
    MouseWheelEvent mouse_wheel = 5;
    ControllerButtonEvent controller_button = 6;
    ControllerAxisEvent controller_axis = 7;
  }
}

message InputRecording {
  repeated InputEvent events = 1;
}
```

**Dependency note**: Protobuf is not currently in the project. Adding it requires:
- Adding `protobuf` (or `protobuf-lite`) as a dependency in `CMakeLists.txt`
- Running `protoc` to generate C++ code from the `.proto` file
- This is **future work** — the initial implementation can use a simpler binary format or skip recording/replay entirely.

## 10. Future work

- **Screenshot diffing against golden images**: Compare `G.test.screenshot()` output against saved reference images, with a configurable pixel tolerance. Fail the test if the diff exceeds threshold.
- **Visual regression CI pipeline**: Automated pipeline that runs tests, captures screenshots, and compares against golden images stored in the repo.
- **Headless rendering**: Run tests with an offscreen framebuffer (EGL or osmesa) so CI doesn't need a display server.
- **Test discovery**: Multiple test functions (`test_movement`, `test_combat`, etc.) run sequentially or in parallel.

## Key files referenced

| File | Role |
|---|---|
| `src/input.h` | `Keyboard`, `Mouse`, `Controllers` classes — injection points |
| `src/input.cc` | Input implementations (`PushEvent`, `InitForFrame`) |
| `src/lua_input.cc` | Current input Lua bindings (`G.input.*`) — pattern to follow for `G.test.*` |
| `src/lua.cc` | Lua state management, callback invocation (`HandleKeypressed`, etc.), coroutine handling |
| `src/game.cc` | Frame loop (`Game::Run`), event handling (`EngineModules::HandleEvent`, `ForwardEventToLua`) |
| `src/game.h` | `GameOptions` struct (where `--test` flag would go) |
| `src/lua_graphics.cc` | `take_screenshot` implementation to reuse |
| `src/clock.h` | `TimeStepInSeconds()` — fixed timestep constant (1/60s) |
