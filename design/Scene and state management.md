---
status: in-design
tags: [scenes, state, lua-api, gameplay]
---

# Scene and State Management

## Problem

The engine has no scene or state management abstraction. All game logic lives
in a single `_Game` global table with `init()`, `update(t, dt)`, and `draw()`
callbacks. There is no mechanism to:

- Switch between distinct game states (menu, gameplay, pause, game-over).
- Overlay a state on top of another (pause menu over gameplay).
- Pass data between states (selected level, score, etc.).
- Clean up resources when leaving a state.
- Receive lifecycle notifications on transitions (enter/leave).

Every non-trivial game needs this. Currently, game developers must hand-roll
state management in Lua with an `if/elseif` chain or a custom state machine.
This is error-prone, verbose, and reinvented in every project.

## Goals

1. A game developer can define scenes as plain Lua tables (same pattern as the
   current `_Game` module — `init`, `update`, `draw`).
2. Switching scenes is a single function call: `G.scene.switch("gameplay")`.
3. Overlays work via a stack: `G.scene.push("pause")` / `G.scene.pop()`.
4. Lifecycle hooks (`enter`, `leave`, `resume`) fire automatically.
5. Input and other events are routed to the active (top-of-stack) scene.
6. Works with hot-reload — switching scenes doesn't break the reload cycle.
7. Zero overhead when not used. A game that never calls `G.scene` works
   exactly as it does today.

## Non-goals

- Per-scene resource isolation (e.g., per-scene physics worlds). This is
  valuable but orthogonal — it can be added later without changing the scene
  API.
- Transition animations (fades, wipes). These are implementable in user Lua
  code on top of the scene lifecycle hooks.
- Scene serialization or persistence. That's the save system's job.

## How other engines handle it

### Love2D + hump.gamestate (community standard)

No built-in system. The community uses `hump.gamestate`:

```lua
Gamestate.switch(to, ...)      -- Replace current state
Gamestate.push(to, ...)        -- Push overlay (pause menu)
Gamestate.pop(...)             -- Pop, resume underlying
```

Callbacks per state: `init()` (once), `enter(previous, ...)`,
`leave()`, `resume()`, `update(dt)`, `draw()`, plus input events
(`keypressed`, `mousepressed`, etc.).

States are plain Lua tables. No class system needed. Data is passed
via arguments to `switch()` and `push()`.

**Key insight**: States are just tables with optional methods. The
library calls whichever methods exist and silently skips missing ones.

### high_impact (C, struct-based)

```c
typedef struct {
    void (*init)(void);
    void (*update)(float dt);
    void (*draw)(void);
    void (*cleanup)(void);
} scene_t;

engine_set_scene(&scene_game);  // Deferred to next frame
```

Switching is deferred — the new scene activates at the start of the next
frame, avoiding mid-frame inconsistencies. Scenes own entity lists that
are automatically cleaned up on transition.

### libGDX (Java, minimal)

```java
interface Screen {
    void show();         // When screen becomes active.
    void render(float delta);
    void pause();
    void resume();
    void hide();         // When switching away.
    void dispose();      // Explicit cleanup.
}

game.setScreen(new GameplayScreen());
```

One active screen at a time. No built-in stack. `show()`/`hide()` are
the lifecycle hooks. `dispose()` must be called manually (no GC safety).

**Key insight**: The simplest production-grade pattern. One interface,
one setter, clear lifecycle.

### Carimbo (C++ + Lua, the reference)

```lua
scenemanager:set(name)       -- Switch (deferred to next update)
scenemanager:destroy(name)   -- Cleanup scene
```

Per-scene callbacks: `on_enter`, `on_leave`, `on_loop(dt)`, `on_tick`,
plus input callbacks (`on_keypress`, `on_touch`, etc.). Scenes are Lua
modules loaded by name. Switching clears `package.loaded` for the old
scene to enable clean hot-reload.

**Key insight**: Deferred switching + `package.loaded` cleanup for
hot-reload safety.

### Raylib (no built-in)

Uses an enum + switch statement pattern. Each screen has standalone
`UpdateXScreen()` / `DrawXScreen()` functions. Transitions are
assignments to a `currentScreen` enum. Simple, zero abstraction cost,
but doesn't scale and is verbose.

## Design

### Scene definition

A scene is a Lua table with optional lifecycle methods. This is the same
pattern as the current `_Game` module — no new concepts:

```lua
local Gameplay = {}

function Gameplay:init()
    -- One-time setup (called once, before first enter).
end

function Gameplay:enter(prev_scene_name, ...)
    -- Called every time this scene becomes active.
    -- prev_scene_name is nil on first entry.
    -- Varargs carry data from switch()/push().
end

function Gameplay:update(t, dt)
    -- Per-frame update.
end

function Gameplay:draw()
    -- Per-frame draw.
end

function Gameplay:leave()
    -- Called when transitioning away (switch or push).
end

function Gameplay:resume(...)
    -- Called when a pushed scene above us is popped.
    -- Varargs carry data from pop().
end

return Gameplay
```

All methods are optional. Missing methods are silently skipped.

### Lua API

```lua
-- Register a scene by name. The table is the scene module.
G.scene.register(name, table)

-- Switch to a named scene. Replaces the current scene.
-- Calls leave() on current, enter(prev, ...) on new.
-- Deferred to the start of the next frame.
G.scene.switch(name, ...)

-- Push a scene onto the stack (overlay).
-- Calls leave() on current, enter(prev, ...) on new.
-- The underlying scene is paused but not destroyed.
G.scene.push(name, ...)

-- Pop the top scene off the stack.
-- Calls leave() on top, resume(...) on the scene below.
G.scene.pop(...)

-- Get the name of the currently active scene.
G.scene.current()  -- returns string or nil

-- Get the number of scenes on the stack.
G.scene.depth()  -- returns integer
```

### Scene registration

Scenes must be registered before they can be switched to. Registration
associates a name with a Lua table:

```lua
-- In main.lua:
local Menu = require("menu")
local Gameplay = require("gameplay")
local Pause = require("pause")

G.scene.register("menu", Menu)
G.scene.register("gameplay", Gameplay)
G.scene.register("pause", Pause)

G.scene.switch("menu")
```

Alternative considered: auto-load scenes by name from `require()` inside
`switch()`. This is more magical and harder to reason about — the game
developer should control when modules are loaded.

### Lifecycle

Scene transitions follow this sequence:

**`G.scene.switch("B", data)`** (A is current):

```
1. A:leave()              -- if A exists and has leave()
2. active = B
3. B:init()               -- only if B has never been entered before
4. B:enter("A", data)     -- if B has enter()
```

**`G.scene.push("B", data)`** (A is current):

```
1. A:leave()              -- if A has leave()
2. push B onto stack
3. B:init()               -- only if B has never been entered before
4. B:enter("A", data)     -- if B has enter()
```

**`G.scene.pop(data)`** (B is on top, A is below):

```
1. B:leave()              -- if B has leave()
2. pop B from stack
3. A:resume(data)         -- if A has resume()
```

### Event routing

Only the top-of-stack scene receives `update()`, `draw()`, and input events
(`keypressed`, `keyreleased`, `mousepressed`, `mousereleased`, `mousemoved`,
`textinput`).

Scenes below the top are paused — they receive no callbacks until they become
active again (via `pop()` or `switch()`).

If a game wants the underlying scene to keep rendering (e.g., a translucent
pause overlay), it can call `G.scene.draw_below()` from its own `draw()`:

```lua
function Pause:draw()
    G.scene.draw_below()  -- Draw the scene below us.
    -- Draw pause menu on top.
    G.graphics.set_color(0, 0, 0, 128)
    G.graphics.rectangle("fill", 0, 0, 800, 600)
    G.graphics.set_color(255, 255, 255, 255)
    G.graphics.print("PAUSED", 350, 280)
end
```

### Deferred switching

`switch()`, `push()`, and `pop()` are deferred — they take effect at the
start of the next frame, before `update()` is called. This avoids mid-frame
state inconsistencies (e.g., switching scenes inside `update()` while the
old scene's `draw()` hasn't run yet).

Multiple transitions in the same frame are coalesced: only the last one
takes effect.

### Hot-reload interaction

On hot-reload, the engine re-runs `main.lua`, which re-registers all scenes
with fresh module tables. The scene manager preserves the current scene name
and stack structure across reloads. After reload, it re-resolves scene names
to the newly registered tables. `init()` is not called again — only `enter()`
fires if the scene was active.

This means hot-reload replaces the scene's code but preserves its position
in the stack. Scene-local state stored in upvalues or the module table is
reset (same as current hot-reload behavior for `_Game`).

### Backward compatibility

If a game never calls `G.scene`, the engine works exactly as it does today.
The `_Game` global table is still the default target for all callbacks. The
scene system only activates when `G.scene.switch()` is first called, at which
point it takes over callback routing from `_Game`.

## Implementation

### C++ side (engine)

The scene manager lives in the Lua binding layer, not in a new C++ subsystem.
It's implemented as a set of `luaL_Reg` functions in a new `lua_scene.cc`.

**State**: A Lua-side registry table stores registered scenes and the stack:

```
Registry["_SceneManager"] = {
    scenes = { menu = <table>, gameplay = <table>, ... },
    stack = { "menu" },           -- bottom to top
    initialized = { menu = true },  -- tracks which scenes had init() called
    pending = nil,                -- deferred transition {type, name, args}
}
```

The scene manager table is stored in the Lua registry (not as a global),
accessed only by the C binding functions.

**Callback routing**: `Lua::Update()`, `Lua::Draw()`, and the input handlers
currently look up `_Game` via `lua_getglobal`. When the scene system is
active, they instead look up the top-of-stack scene from the registry table.
This is a single branch per callback — negligible cost.

```cpp
// In Lua::Update, Lua::Draw, etc:
if (scene_active_) {
    // Get top-of-stack scene table from registry.
    PushActiveScene(state_);
} else {
    lua_getglobal(state_, "_Game");
}
```

**Deferred transitions**: `switch()`, `push()`, `pop()` store the pending
transition in the registry table. At the start of `Lua::Update()`, before
calling the scene's `update()`, the engine checks for a pending transition
and executes it.

### Files to modify

- `src/lua_scene.cc` (new) — `G.scene` binding functions
- `src/lua.h` / `src/lua.cc` — add `scene_active_` flag, modify callback
  routing to check scene stack, process deferred transitions in `Update()`
- `src/engine.cc` — register `lua_scene` module

### API function list

| Function | Signature | Description |
|---|---|---|
| `register` | `(name: string, scene: table)` | Register a scene by name |
| `switch` | `(name: string, ...: any)` | Replace current scene (deferred) |
| `push` | `(name: string, ...: any)` | Push overlay scene (deferred) |
| `pop` | `(...: any)` | Pop top scene (deferred) |
| `current` | `() -> string?` | Name of active scene |
| `depth` | `() -> integer` | Number of scenes on stack |
| `draw_below` | `()` | Draw the scene below the current one |

## Usage examples

### Minimal: menu and gameplay

```lua
-- main.lua
local Menu = require("menu")
local Game = require("game")

G.scene.register("menu", Menu)
G.scene.register("game", Game)
G.scene.switch("menu")
```

```lua
-- menu.lua
local Menu = {}

function Menu:draw()
    G.graphics.print("Press ENTER to start", 100, 100)
end

function Menu:keypressed(key)
    if key == "return" then
        G.scene.switch("game", { level = 1 })
    end
end

return Menu
```

```lua
-- game.lua
local Game = {}

function Game:enter(prev, opts)
    self.level = opts and opts.level or 1
    self.score = 0
end

function Game:update(t, dt)
    -- gameplay logic
end

function Game:draw()
    G.graphics.print("Level: " .. self.level, 10, 10)
    G.graphics.print("Score: " .. self.score, 10, 30)
end

function Game:keypressed(key)
    if key == "escape" then
        G.scene.push("pause")
    end
end

return Game
```

### Overlay: pause menu

```lua
-- pause.lua
local Pause = {}

function Pause:draw()
    G.scene.draw_below()
    G.graphics.set_color(0, 0, 0, 128)
    G.graphics.rectangle("fill", 0, 0, 800, 600)
    G.graphics.set_color(255, 255, 255, 255)
    G.graphics.print("PAUSED - Press ESC to resume", 250, 280)
end

function Pause:keypressed(key)
    if key == "escape" then
        G.scene.pop()
    end
end

return Pause
```

### Data passing: level select to gameplay

```lua
-- level_select.lua
function LevelSelect:keypressed(key)
    if key == "return" then
        G.scene.switch("game", {
            level = self.selected_level,
            difficulty = "hard",
        })
    end
end
```

```lua
-- game.lua
function Game:enter(prev, opts)
    self.level = opts.level
    self.difficulty = opts.difficulty
end
```

## Design decisions

- **No `update_below()`**. Paused scenes should not keep simulating.
  If a game needs the world to continue updating behind a pause overlay,
  it can manage that explicitly in its own code. Providing `update_below()`
  as a primitive invites hard-to-debug state mutation while the overlay is
  up.

- **Scene-scoped timers and tweens**: Yes. When leaving a scene, its active
  timers and tweens are automatically paused. When re-entering (via `pop()`
  + `resume()`), they resume. When a scene is fully replaced (via
  `switch()`), its timers and tweens are cancelled. This requires
  integration with the timer system — timers need a scene tag so the scene
  manager can pause/cancel them by scene.

- **Per-scene physics worlds**: Yes, worth doing. Each scene gets its own
  physics world, automatically cleaned up on transition. This prevents
  bodies from leaking across menu/gameplay boundaries (Carimbo's pattern).
  Deferred to a follow-up — the scene system works without this initially,
  and the physics integration can be added once the scene API is stable.

- **`quit` callback**: Routed to the active scene. The active scene decides
  whether to save state, confirm exit, or delegate. If the active scene
  has no `quit()` method, the engine exits immediately (current behavior).

## Open questions

- **Anchor's approach**: Anchor has no scene system — it uses an object
  tree with mixins, where transitions are managed by adding/removing
  subtrees. This is the weakest scene management story among the comparison
  engines. Not a model to follow.

- **Scene-scoped sounds**: Should playing sounds be stopped when leaving a
  scene? Background music often spans scenes, but sound effects should
  probably stop. Could add an optional `stop_sounds_on_leave` flag per
  scene, defaulting to false.

%% We should use a flag in play_music to handle this %%