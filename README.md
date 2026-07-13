# Game Engine

A 2D game engine written in C++17. Games are scripted in Lua 5.1 (or
[Fennel](https://fennel-lang.org/)) with built-in physics, audio, collision
detection, input handling, asset management, and hot-reloading. A CLI tool
(`game`) drives the workflow: create a project, run it with live reload,
and package it for distribution.

## Features

- **Graphics** --- Batch renderer with sprites, primitives (rect, circle,
  triangle, ellipse, rounded rect, lines), SDF text with word-wrap and
  outlines, transform stack, custom GLSL shaders, off-screen canvases,
  blend modes, stencil/scissor clipping, screenshots
- **Physics** --- Box2D rigid-body dynamics with collision categories,
  contact callbacks, forces/impulses, damping, gravity scaling, CCD,
  joints (revolute, distance, weld, prismatic, mouse, wheel)
- **Collision** --- Lightweight spatial-hash broad-phase (separate from
  Box2D) with circle/AABB shapes, move-and-slide, raycasting, overlap
  queries
- **Tilemap** --- Multi-layer tile rendering with Tiled TMX import, AABB
  sweep collision, parallax scrolling, object layers, viewport culling
- **Audio** --- QOA streaming for music, decoded PCM for effects, volume,
  pitch, panning, looping, global volume control
- **Input** --- Keyboard (pressed/down/released), mouse (buttons, position,
  wheel), gamepad (buttons, axes), synthetic input injection for testing
- **Camera** --- Follow with lerp, deadzone, bounds clamping, screen shake,
  zoom, rotation, parallax, world/screen coordinate conversion
- **Timers** --- `after`, `every`, `during`, `tween` (20+ easing curves),
  `cooldown`, tag-based cancellation, time scaling
- **Scenes** --- `G.scene` API with register/switch/push/pop, deferred
  transitions, lifecycle hooks (init/enter/leave/resume), automatic
  callback routing, overlay support via `draw_below`
- **Particles** --- `G.particles` API with SoA pool, PropertyRamp/ColorRamp
  over-lifetime modulation, instanced GPU rendering, emission shapes
- **Networking** --- ENet reliable UDP with client/server architecture,
  send/broadcast/receive, peer management
- **Assets** --- SQLite-backed asset database, automatic packing, hot-reload
  via inotify, spritesheet support (JSON and XML), single-file packaging
- **Scripting** --- Lua 5.1 with optional Fennel, LuaLS type stubs for IDE
  autocomplete, debug UI with ImGui overlay (Tab key)
- **Tooling** --- CLI workflow (`game init/run/package/stubs`),
  clang-tidy/clang-format, AddressSanitizer + UBSanitizer, Chrome Tracing
  profiler, CPU sampling via samply

## Platform support

| Platform | Toolchain | CI | Status |
|---|---|---|---|
| Linux x86_64 | GCC | Yes | Primary target |
| Windows x86_64 | clang-cl | Yes | Supported |
| macOS arm64 | Apple Clang | Yes | Supported |
| Linux-to-Windows | MinGW (cross-compile) | No | Supported (SFX packaging) |

## Quick start

The project uses a [devenv](https://devenv.sh/) development environment that
provides all build tooling. After entering the dev shell:

```bash
# Build the engine
game-build

# Create a new project
game init my-game
cd my-game

# Run in development mode (with hot-reload)
game run

# Run the test suite
game-test
```

Without devenv, build manually with CMake:

```bash
cmake -G Ninja -B build
cmake --build build
./build/game init my-game
```

This creates the following project structure:

```
my-game/
  conf.json              # Window and project configuration
  main.lua               # Entry point (must return a game module)
  game.lua               # Game module with init/update/draw
  .luarc.json            # LuaLS editor configuration
  definitions/
    game.lua             # Auto-generated type stubs for IDE support
```

### Game lifecycle

`main.lua` must return a table with these methods:

```lua
local Game = {}

function Game:init()
  -- Called once at startup
end

function Game:update(t, dt)
  -- Called every frame. t = elapsed time, dt = delta time
end

function Game:draw()
  -- Called every frame after update
end

return Game
```

### Optional callbacks

The game table may also define these methods. All are optional.

```lua
function Game:keypressed(scancode)  end
function Game:keyreleased(scancode) end
function Game:mousepressed(button)  end
function Game:mousereleased(button) end
function Game:mousemoved(x, y, dx, dy) end
function Game:textinput(text)       end
function Game:quit()                end

-- Networking (requires G.network)
function Game:on_connect(peer_id)                   end
function Game:on_disconnect(peer_id)                end
function Game:on_receive(peer_id, data, channel)    end
```

## CLI reference

### `game init [directory]`

Create a new project with scaffold files. Uses the current directory if none
is specified. Fails if `conf.json` already exists.

Options: `--fennel` scaffolds with `.fnl` files and includes the Fennel
compiler.

### `game run [directory] [flags] [-- game-args...]`

Run a project in development mode.

| Flag | Description |
|------|-------------|
| `--no-hotreload` | Disable file watching and hot-reload |
| `--clean` | Delete cached database and repack all assets |
| `--test` | Run in test mode (enables `G.test` API) |
| `--` | Everything after this is forwarded to `G.system.cli_arguments()` |

Asset metadata is cached in `~/.cache/game/<project-hash>/assets.sqlite3`;
asset contents live next to it in `blobs/`, as content-addressed files named
by hash. `game package` bundles the same blobs into an `assets.zip` next to
the binary.

### `game package [directory] [options]`

Bundle a project for distribution.

| Option | Description |
|--------|-------------|
| `-o, --output <dir>` | Output directory (default: `dist`) |
| `--name <name>` | Override binary name (default: `app_name` from conf.json) |
| `--strip` | Strip debug symbols from the binary |
| `--engine-binary <path>` | Use a pre-built binary (for cross-platform packaging) |
| `--sfx` | Produce a self-extracting .7z.exe archive (Windows) |
| `--target <native\|web>` | Package for desktop (default) or the browser |

#### Web packaging

Build the web engine once with `game-build-web` (requires the devenv
shell, which provides Emscripten), then:

```sh
game package mygame --target web -o dist-web
python3 -m http.server -d dist-web   # test locally (must be HTTP, not file://)
```

`dist-web/` contains `index.html`, `game.js`, `game.wasm`,
`assets.sqlite3`, and `assets.zip`. For itch.io, zip the directory
contents (index.html at the zip root), upload, and mark the file as
playable in the browser — or `butler push dist-web user/game:html5`.
For a personal site, copy the directory anywhere served over HTTP
(`.wasm` should be served as `application/wasm`).

Web limitations (v1): no networking (`G.network` raises an error),
single-threaded, 512 MB fixed memory, lines render 1px wide, and only
`G.save` data persists (to IndexedDB); `G.filesystem` writes are lost on
reload.

### `game stubs [--output <path>]`

Regenerate LuaLS type stubs. Default output: `definitions/game.lua`.

### `game convert`, `game atlas`

Asset conversion tools. `convert` transcodes images (PNG to QOI) and audio
(WAV/OGG resampling). `atlas` packs individual sprites into a spritesheet
with a JSON metadata file.

### `game version`

Print engine version and build date.

### `game completions {bash|zsh|man}`

Generate shell completions or a man page. Output is printed to stdout.

```bash
# Bash
game completions bash > ~/.local/share/bash-completion/completions/game

# Zsh
game completions zsh > ~/.local/share/zsh/site-functions/_game

# Man page
game completions man | sudo tee /usr/local/share/man/man1/game.1
```

### `game help [command]`

Show usage information for a command.

## Configuration

`conf.json` fields and defaults:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `width` | integer | `1440` | Window width in pixels |
| `height` | integer | `1024` | Window height in pixels |
| `title` | string | `""` | Window title |
| `msaa_samples` | integer | `16` | MSAA anti-aliasing samples |
| `borderless` | boolean | `false` | Borderless window mode |
| `fullscreen` | boolean | `false` | Fullscreen mode |
| `centered` | boolean | `true` | Center window on screen |
| `resizable` | boolean | `true` | Allow window resizing |
| `enable_joystick` | boolean | `false` | Enable gamepad/controller input |
| `org_name` | string | `""` | Organization name (used by `package`) |
| `app_name` | string | `""` | Application name (used by `package`) |
| `version` | string | `"0.1"` | Version string (`"major.minor"`) |

## Lua API reference

All functions are accessed through the global `G` object. The canonical
source of truth is `definitions/game.lua` (regenerate with `game stubs`),
which contains LuaLS-typed signatures for every binding. The summary below
is curated for browsing.

### G.graphics

```lua
-- Clear / color
G.graphics.clear([r, g, b, a])
G.graphics.set_color(name)              -- Named color, or:
G.graphics.set_color(r, g, b, a)        -- 0-255 range

-- Sprites and primitives
G.graphics.draw_sprite(name, x, y [, angle])
G.graphics.draw_image(name, x, y [, angle])
G.graphics.draw_rect(x1, y1, x2, y2 [, angle])
G.graphics.draw_rect_outline(x1, y1, x2, y2 [, angle])
G.graphics.draw_circle(x, y, r)
G.graphics.draw_circle_outline(x, y, r)
G.graphics.draw_ellipse(x, y, rx, ry)
G.graphics.draw_ellipse_outline(x, y, rx, ry)
G.graphics.draw_rounded_rect(x1, y1, x2, y2, radius)
G.graphics.draw_rounded_rect_outline(x1, y1, x2, y2, radius)
G.graphics.draw_triangle(p1x, p1y, p2x, p2y, p3x, p3y)
G.graphics.draw_triangle_outline(p1x, p1y, p2x, p2y, p3x, p3y)
G.graphics.draw_line(p1x, p1y, p2x, p2y)
G.graphics.draw_lines(points)

-- Text
G.graphics.print(text, x, y)                                            -- Debug font
G.graphics.draw_text(font, size, text, x, y)
G.graphics.text_dimensions(font, size, text) -> width, height
G.graphics.draw_text_wrapped(font, size, text, x, y, max_width [, align])
G.graphics.text_wrapped_height(font, size, text, max_width) -> height
G.graphics.draw_text_colored(font, size, segments, x, y [, max_width, align])
G.graphics.set_text_outline(r, g, b, a, thickness)
G.graphics.clear_text_outline()

-- Transform stack
G.graphics.push()
G.graphics.pop()
G.graphics.translate(x, y)
G.graphics.rotate(angle)        -- Radians, clockwise
G.graphics.scale(xf, yf)

-- Shaders
G.graphics.new_shader([source])
G.graphics.attach_shader([name])           -- nil resets to default
G.graphics.send_uniform(name, value)       -- Accepts number / vec / mat
G.graphics.has_uniform(name) -> boolean

-- Canvases (off-screen render targets)
G.graphics.new_canvas(width, height [, options]) -> canvas
G.graphics.set_canvas([canvas])            -- nil restores main framebuffer
G.graphics.draw_canvas(canvas, x, y [, angle, w, h])

-- Scissor and stencil
G.graphics.set_scissor(x, y, w, h)
G.graphics.clear_scissor()
G.graphics.stencil_begin(action [, value])
G.graphics.stencil_end()
G.graphics.set_stencil_test(compare [, ref])
G.graphics.clear_stencil_test()

-- Blend mode and screenshots
G.graphics.set_blend_mode(mode)            -- "alpha"/"add"/"multiply"/"replace"/"premultiplied"
G.graphics.take_screenshot([file]) -> byte_buffer | nil
```

### G.window

```lua
G.window.dimensions() -> width, height
G.window.set_dimensions(width, height)
G.window.set_fullscreen()
G.window.set_borderless()
G.window.set_windowed()
G.window.set_title(title)
G.window.get_title() -> string
G.window.has_input_focus() -> boolean
G.window.has_mouse_focus() -> boolean
```

The engine pauses fixed-step `update()` automatically while the window
lacks keyboard focus, so games rarely need to check `has_input_focus()`
themselves.

### G.input

```lua
-- Keyboard
G.input.is_key_pressed(key) -> boolean     -- Pressed this frame
G.input.is_key_down(key) -> boolean        -- Currently held
G.input.is_key_released(key) -> boolean    -- Released this frame

-- Mouse
G.input.mouse_position() -> x, y
G.input.mouse_wheel() -> x, y
G.input.is_mouse_pressed(button) -> boolean
G.input.is_mouse_down(button) -> boolean
G.input.is_mouse_released(button) -> boolean

-- Controller (requires enable_joystick in conf.json)
G.input.is_controller_button_pressed(button) -> boolean
G.input.is_controller_button_down(button) -> boolean
G.input.is_controller_button_released(button) -> boolean
G.input.get_controller_axis(axis) -> number

-- Touch (multi-finger; positions in viewport coordinates)
G.input.touch_count() -> number
G.input.touches() -> {{id, x, y, pressure}, ...}
G.input.is_touch_pressed() -> boolean      -- Any finger began this frame
G.input.is_touch_down() -> boolean
G.input.is_touch_released() -> boolean

-- Actions: named inputs bound to any combination of sources. Binding
-- sources: "key:<name>", "mouse:<left|middle|right|0|1|2>",
-- "gamepad:<button>", "touch" (any finger).
G.input.bind(action, bindings)             -- e.g. bind("jump", {"key:space", "touch"})
G.input.is_action_pressed(action) -> boolean
G.input.is_action_down(action) -> boolean
G.input.is_action_released(action) -> boolean
G.input.action_time(action) -> number      -- Seconds held, 0 if up
G.input.get_bindings(action) -> {string}   -- For settings menus / G.save
```

Text input is delivered to the game module through the
`Game:textinput(input)` callback rather than a polling API.

### G.scene

Scene/state management with a stack-based model. Scenes are plain Lua
tables with optional lifecycle methods. Transitions are deferred to the
start of the next frame.

```lua
G.scene.register(name, table)              -- Register a scene by name
G.scene.switch(name, ...)                  -- Replace current scene
G.scene.push(name, ...)                    -- Push overlay scene
G.scene.pop(...)                           -- Pop top scene
G.scene.current() -> string | nil          -- Active scene name
G.scene.depth() -> integer                 -- Stack depth
G.scene.draw_below()                       -- Draw the scene below current
```

Scene lifecycle callbacks (all optional):

```lua
function Scene:init()                      -- Once, before first enter
function Scene:enter(prev_name, ...)       -- Each time scene becomes active
function Scene:leave()                     -- When transitioning away
function Scene:resume(...)                 -- When a pushed scene above is popped
function Scene:update(t, dt)               -- Per-frame update
function Scene:draw()                      -- Per-frame draw
```

Example with scenes:

```lua
-- main.lua
local Menu = require("menu")
local Game = require("game")

G.scene.register("menu", Menu)
G.scene.register("game", Game)
G.scene.switch("menu")

local Stub = {}
function Stub:init() end
function Stub:update() end
function Stub:draw() end
return Stub
```

### G.particles

CPU particle system with instanced GPU rendering. Emitters are configured
with a declarative table and support property ramps (constant, random range,
or N-stop interpolation) for size, speed, spin, and color over lifetime.

```lua
local emitter = G.particles.new_emitter({
    max_particles = 3000,
    emission_rate = 200,                -- particles/sec (0 = burst only)
    lifetime = {0.3, 0.8},             -- {min, max} seconds
    speed = {50, 150},                 -- {min, max} pixels/sec
    direction = -math.pi / 2,          -- upward
    spread = math.pi / 6,              -- 30-degree cone
    size = {4, 8},                     -- {min, max} half-extent
    size_over_life = {1.0, 0.5, 0.0},  -- ramp: full -> half -> zero
    color_over_life = {                 -- RGBA stops (0-1 floats)
        {1, 1, 0.6, 1},               -- bright yellow
        {1, 0.5, 0, 0.8},             -- orange
        {0.6, 0.1, 0, 0},             -- dark red, transparent
    },
    gravity = {0, -50},                -- {x, y} pixels/sec^2
    damping = 0.95,                    -- velocity retention per second
    blend_mode = "add",                -- "add", "alpha", "multiply", "replace"
    shape = "point",                   -- "point", "circle", "rect"
})
```

Emitter methods:

```lua
emitter:set_position(x, y)            -- Set emitter world position
emitter:start()                       -- Begin continuous emission
emitter:stop()                        -- Stop emission (particles finish)
emitter:burst(count [, x, y])         -- Spawn count particles immediately
emitter:update(dt)                    -- Advance simulation
emitter:draw()                        -- Render all live particles
emitter:particle_count() -> integer   -- Number of live particles
emitter:is_active() -> boolean        -- Whether emitting
emitter:set_emission_rate(rate)       -- Change particles/sec
emitter:set_direction(angle)          -- Change emission angle
emitter:set_spread(spread)            -- Change cone half-angle
emitter:set_gravity(gx, gy)           -- Change gravity force
```

### G.physics

Box2D-backed 2D physics. Top-down friction is simulated with a friction
joint to a static ground body, so `create_ground()` must be called before
adding any dynamic bodies.

```lua
-- Setup
G.physics.create_ground([walls])           -- walls=true adds screen-edge fixtures
G.physics.set_collision_categories({ "player", "enemy", ... })
G.physics.on_begin_contact(function(a, b) ... end)
G.physics.on_end_contact(function(a, b) ... end)

-- Bodies (options: density, friction, restitution, sensor, category, mask,
-- body_type = "dynamic"/"kinematic"/"static")
G.physics.add_box(tx, ty, bx, by, angle, userdata [, options]) -> physics_handle
G.physics.add_circle(cx, cy, radius, userdata [, options]) -> physics_handle
G.physics.destroy_handle(handle)

-- Position and rotation
G.physics.position(handle) -> x, y
G.physics.set_position(handle, x, y)
G.physics.angle(handle) -> radians
G.physics.rotate(handle, angle)
G.physics.set_fixed_rotation(handle, fixed)

-- Velocity
G.physics.linear_velocity(handle) -> vx, vy
G.physics.set_linear_velocity(handle, vx, vy)
G.physics.angular_velocity(handle) -> omega
G.physics.set_angular_velocity(handle, omega)

-- Forces and impulses
G.physics.apply_force(handle, x, y)              -- BODY-LOCAL coordinates
G.physics.apply_force_world(handle, x, y)        -- WORLD coordinates
G.physics.apply_linear_impulse(handle, x, y)     -- WORLD coordinates
G.physics.apply_torque(handle, torque)

-- Semantic helpers
G.physics.set_angle(handle, angle)               -- Set absolute rotation
G.physics.move_toward(handle, tx, ty, speed)     -- Chase a point at speed px/s
G.physics.look_at(handle, tx, ty)                -- Face toward a point

-- Body tuning
G.physics.set_linear_damping(handle, damping)
G.physics.set_angular_damping(handle, damping)
G.physics.set_gravity_scale(handle, scale)
G.physics.set_bullet(handle, bullet)              -- Continuous collision detection

-- Joints (all coordinates in pixels, angles in radians)
-- All create functions return a joint_handle. Joints are automatically
-- destroyed when either connected body is destroyed.
G.physics.create_revolute_joint(a, b, ax, ay [, opts])              -> joint_handle
G.physics.create_distance_joint(a, b, ax1, ay1, ax2, ay2 [, opts]) -> joint_handle
G.physics.create_weld_joint(a, b, ax, ay [, opts])                 -> joint_handle
G.physics.create_prismatic_joint(a, b, ax, ay, dx, dy [, opts])    -> joint_handle
G.physics.create_mouse_joint(body, tx, ty [, opts])                 -> joint_handle
G.physics.create_wheel_joint(a, b, ax, ay, dx, dy [, opts])        -> joint_handle

-- Revolute joint opts (hinge/pin — bodies rotate around anchor):
--   enable_limit       boolean (false)  constrain to [lower_angle, upper_angle]
--   lower_angle        number  (0)      min angle in radians
--   upper_angle        number  (0)      max angle in radians
--   enable_motor       boolean (false)  apply torque to reach motor_speed
--   motor_speed        number  (0)      target angular velocity, rad/s
--   max_motor_torque   number  (0)      max torque the motor can apply
--   collide_connected  boolean (false)  let the two bodies collide

-- Distance joint opts (spring/rod — maintains distance between anchors):
--   length             number  (auto)   rest length in px; omit to use initial anchor distance
--   frequency          number  (0)      spring Hz; 0=rigid rod, 1-5=soft spring
--   damping_ratio      number  (0)      0=oscillates forever, 1=critically damped
--   collide_connected  boolean (false)  let the two bodies collide

-- Weld joint opts (rigid lock — holds bodies at fixed relative transform):
--   frequency          number  (0)      softness Hz; 0=perfectly rigid
--   damping_ratio      number  (0)      0=no damping, 1=critically damped
--   collide_connected  boolean (false)  let the two bodies collide

-- Prismatic joint opts (slider/piston — body_b slides along axis):
--   enable_limit         boolean (false)  constrain to [lower, upper]
--   lower_translation    number  (0)      min slide distance in px
--   upper_translation    number  (0)      max slide distance in px
--   enable_motor         boolean (false)  apply force to reach motor_speed
--   motor_speed          number  (0)      target speed, px/s along axis
--   max_motor_force      number  (0)      max force the motor can apply
--   collide_connected    boolean (false)  let the two bodies collide

-- Mouse joint opts (drag — pulls body toward target; anchored to ground):
--   max_force          number  (1000)   max force; higher=snappier
--   frequency          number  (5.0)    spring Hz for tracking
--   damping_ratio      number  (0.7)    0.7=good default, 1=no overshoot

-- Wheel joint opts (vehicle suspension — revolute + prismatic along axis):
--   enable_motor       boolean (false)  spin the wheel
--   motor_speed        number  (0)      target angular velocity, rad/s
--   max_motor_torque   number  (0)      max torque the motor can apply
--   frequency          number  (2.0)    suspension spring Hz
--   damping_ratio      number  (0.7)    suspension damping; 1=critically damped
--   collide_connected  boolean (false)  let chassis and wheel collide

-- Joint handle methods
joint:is_valid() -> bool
joint:get_type() -> string                -- "revolute", "distance", etc.
joint:destroy()
joint:get_joint_angle() -> radians        -- revolute only
joint:get_joint_speed() -> number         -- revolute (rad/s) or prismatic (px/s)
joint:get_joint_translation() -> pixels   -- prismatic only
joint:get_current_length() -> pixels      -- distance only
joint:set_motor_speed(speed)              -- revolute, prismatic, wheel
joint:enable_motor(bool)                  -- revolute, prismatic, wheel
joint:enable_limit(bool)                  -- revolute, prismatic
joint:set_limits(lower, upper)            -- revolute (rad) or prismatic (px)
joint:set_max_motor_torque(torque)        -- revolute, wheel
joint:set_max_motor_force(force)          -- prismatic
joint:set_length(pixels)                  -- distance
joint:set_target(x, y)                    -- mouse
joint:set_max_force(force)                -- mouse
joint:set_frequency(hz)                   -- distance, weld, wheel
joint:set_damping_ratio(ratio)            -- distance, weld, wheel
```

### G.collision

Lightweight broad-phase collision world (separate from Box2D --- use this
for kinematic / character controllers and queries).

```lua
-- Shapes
G.collision.circle(radius) -> collision_shape
G.collision.aabb(w, h) -> collision_shape

-- One-shot test
G.collision.test(shape_a, ax, ay, shape_b, bx, by) -> hit, nx, ny, depth

-- World
local world = G.collision.new_world([cell_size])
world:add(shape, x, y [, options]) -> handle
world:remove(handle)
world:set_position(handle, x, y)
world:get_position(handle) -> x, y
world:set_shape(handle, shape)
world:set_filter(handle, category, mask)
world:get_userdata(handle) -> any
world:move_and_slide(handle, vx, vy)   -> nx, ny, hits
world:move_and_collide(handle, vx, vy) -> nx, ny, first_hit?
world:move_toward(handle, tx, ty, speed, dt) -> nx, ny, first_hit?
world:get_overlaps(handle) -> hits
world:raycast(ox, oy, dx, dy, max_dist [, mask]) -> hit?
```

### G.tilemap

2D tilemap with multi-layer rendering, AABB sweep collision, and Tiled
(TMX) import. Tiles are rendered with viewport culling and optional
per-layer parallax.

```lua
-- Create from code
local map = G.tilemap.new({
    tile_width = 16,
    tile_height = 16,
    tileset = "tilemap_packed",       -- spritesheet name
})
map:add_layer("ground", 40, 23)       -- name, width_in_tiles, height_in_tiles
map:set_tile("ground", 3, 5, 12)      -- layer, tile_x, tile_y, tile_id

-- Or load from Tiled TMX
local map = G.tilemap.load_tmx("level.tmx", gid_offset)

-- Rendering
map:draw()                             -- Draw all visible layers
map:draw_layer("ground")               -- Draw a single layer
map:draw_tile(tile_id, x, y)           -- Draw one tile at pixel position

-- Collision (AABB sweep, resolves X then Y)
local result = map:move(x, y, w, h, vx, vy)
-- result.x, result.y = resolved position
-- result.collisions = list of {nx, ny, tile_x, tile_y}

-- Queries
map:tile_at(px, py) -> tile_id         -- World coords to tile ID
map:is_solid(px, py) -> boolean        -- Any collision layer has a tile here
map:world_to_tile(px, py) -> tx, ty
map:tile_to_world(tx, ty) -> px, py

-- Layer control
map:set_parallax("bg", 0.5, 0.5)      -- Scroll factor per layer
map:set_visible("fg", false)
map:set_collision("ground", true)      -- Mark layer as collidable
map:set_tileset("other_sheet")

-- Object layers (from TMX)
local objects = map:get_objects("spawns")
-- Each object: {name, type, x, y, width, height, properties}

-- Info
map:dimensions() -> width, height      -- In tiles
map:layer_count() -> integer
```

### G.camera

```lua
G.camera.set(x, y)            G.camera.get() -> x, y
G.camera.move(dx, dy)
G.camera.set_zoom(z)          G.camera.get_zoom() -> z
G.camera.set_rotation(a)      G.camera.get_rotation() -> a

-- Following
G.camera.follow(x, y)         G.camera.unfollow()
G.camera.set_lerp(lx, ly)
G.camera.set_deadzone(half_w, half_h)
G.camera.clear_deadzone()
G.camera.set_bounds(x, y, w, h)
G.camera.clear_bounds()

-- Effects and coordinate conversion
G.camera.shake(intensity, duration [, frequency])
G.camera.to_world(sx, sy)  -> wx, wy
G.camera.to_screen(wx, wy) -> sx, sy
G.camera.mouse_world() -> wx, wy

-- Manual transform integration
G.camera.attach([parallax_x, parallax_y])
G.camera.detach()
```

### G.sound

```lua
-- Streaming sources (music, long clips)
G.sound.add_source(name) -> source_id
G.sound.play_source(source_id)
G.sound.stop_source(source_id)
G.sound.pause(source_id)
G.sound.resume(source_id)
G.sound.is_playing(source_id) -> boolean
G.sound.set_loop(source_id, loop)
G.sound.set_volume(source_id, gain)        -- 0.0 to 1.0
G.sound.set_pitch(source_id, pitch)        -- 0.25 to 4.0
G.sound.set_pan(source_id, pan)            -- -1 left, 1 right

-- Decoded effects (short, fire-and-forget)
G.sound.add_effect(name) -> effect_id
G.sound.play_effect(name)

-- Convenience and master
G.sound.play(name)                         -- Load + play immediately
G.sound.set_global_volume(gain)
```

### G.timer

Tag-based timer/tween system.

```lua
G.timer.after(delay, action [, tag]) -> id
G.timer.every(delay, action [, times, tag]) -> id
G.timer.during(duration, action [, after, tag]) -> id
G.timer.tween(duration, subject, target [, easing, after, tag]) -> id
G.timer.cooldown(delay, condition, action [, times, tag]) -> id
G.timer.cancel(tag)
G.timer.cancel_all()
G.timer.exists(tag) -> boolean
G.timer.set_real_time(tag, real_time)      -- Ignore time scale
```

Easing functions: `linear`, `quad`, `cubic`, `quart`, `quint`, `sine`,
`expo`, `circ`, `back`, `bounce`, `elastic`. Each supports `in-`, `out-`,
`in-out-`, `out-in-` prefixes (e.g. `"out-quad"`).

### G.network

ENet-based reliable UDP networking.

```lua
G.network.create_server(port, max_clients [, channels])
G.network.create_client([channels])
G.network.connect(host, port)
G.network.disconnect()
G.network.send(peer_id, data [, {channel=0, reliable=true}])
G.network.broadcast(data [, {channel=0, reliable=true}])
G.network.is_active() -> boolean
G.network.peer_count() -> integer
G.network.connected_peers() -> { peer_id, ... }
```

Incoming events are delivered via callbacks on the game module:
`on_connect(peer_id)`, `on_disconnect(peer_id)`,
`on_receive(peer_id, data, channel)`.

### G.json

```lua
G.json.encode(value) -> string
G.json.decode(string) -> value
```

### G.math

```lua
-- Direction constants (Y-down screen space)
G.math.UP                                      -- v2(0, -1)
G.math.DOWN                                    -- v2(0, 1)
G.math.LEFT                                    -- v2(-1, 0)
G.math.RIGHT                                   -- v2(1, 0)

-- Scalar utilities
G.math.clamp(x, low, high) -> number
G.math.lerp(a, b, t) -> number
G.math.inverse_lerp(a, b, x) -> number
G.math.remap(x, a1, b1, a2, b2) -> number
G.math.smoothstep(edge0, edge1, x) -> number
G.math.sign(x) -> number                      -- -1, 0, or 1
G.math.round(x) -> number

-- 2D point utilities
G.math.distance(x1, y1, x2, y2) -> number
G.math.distance2(x1, y1, x2, y2) -> number    -- Squared (no sqrt)
G.math.angle(x1, y1, x2, y2) -> number        -- Radians (atan2)
G.math.direction(angle [, magnitude]) -> x, y  -- Angle to components

-- Angle conversion
G.math.radians(degrees) -> number
G.math.degrees(radians) -> number

-- Constructors
G.math.v2(x, y) -> vec2
G.math.v3(x, y, z) -> vec3
G.math.v4(x, y, z, w) -> vec4
G.math.m2x2(v1..v4)  -> mat2x2                -- Row-major order
G.math.m3x3(v1..v9)  -> mat3x3
G.math.m4x4(v1..v16) -> mat4x4
```

Vector methods (shared by `vec2`, `vec3`, `vec4`):

```lua
v:dot(other) -> number
v:len2() -> number                             -- Squared length
v:length() -> number
v:normalized() -> vec
v:lerp(other, t) -> vec
v:unpack() -> x, y [, z [, w]]
v:send_as_uniform(name)
```

Additional `vec2`-only methods:

```lua
v:distance(other) -> number
v:distance2(other) -> number
v:angle() -> number                            -- atan2(y, x)
v:angle_between(other) -> number
v:rotate(angle) -> vec2
v:perpendicular() -> vec2                      -- (-y, x)
v:reflect(normal) -> vec2
v:project(onto) -> vec2
```

Operators (all vectors): `+`, `-`, `* (scalar)`, unary `-`, `tostring`.

Matrix methods (`mat2x2`, `mat3x3`, `mat4x4`): `send_as_uniform(name)`.

### G.system

```lua
G.system.quit()
G.system.operating_system() -> string
G.system.cpu_count() -> integer
G.system.set_clipboard(text)
G.system.get_clipboard() -> string
G.system.open_url(url) -> error
G.system.cli_arguments() -> table

-- Time scaling
G.system.set_time_scale(scale)
G.system.get_time_scale() -> number
G.system.get_real_dt() -> seconds          -- Unscaled
G.system.get_real_time() -> seconds        -- Unscaled
```

### G.clock

```lua
G.clock.walltime() -> seconds
G.clock.gametime() -> seconds              -- Time-scaled
G.clock.gamedelta() -> seconds             -- Time-scaled
G.clock.sleep_ms(ms)
```

### G.filesystem

```lua
G.filesystem.slurp(name) -> error, byte_buffer
G.filesystem.spit(name, str) -> error
G.filesystem.load_json(name) -> error, table
G.filesystem.save_json(name, table) -> error
G.filesystem.list_directory(name) -> files
G.filesystem.exists(name) -> boolean
```

### G.assets

```lua
G.assets.sprite(name) -> sprite_asset
G.assets.sprite_info(name) -> { width, height }
G.assets.list_images() -> table
G.assets.list_sprites() -> table
```

### G.random

```lua
G.random.from_seed(seed) -> rng
G.random.non_deterministic() -> rng
G.random.sample(rng [, start, end]) -> number
G.random.pick(rng, list) -> element
```

### G.data

```lua
G.data.hash(data) -> number       -- Hash a string or byte_buffer

-- Protobuf serialization (requires .proto schema loaded as asset)
G.data.load_schema(name)                       -- Load a .proto schema
G.data.encode(typename, table) -> string       -- Encode table to binary
G.data.decode(typename, bytes) -> table        -- Decode binary to table
G.data.types() -> iterator                     -- Iterate registered message types
G.data.fields(typename) -> iterator            -- Iterate fields of a message type
```

### G.save

Persistent key-value store backed by a separate SQLite database in the
platform save directory. Values are organized by namespace and serialized as
JSON, so any Lua value that `G.json.encode` can handle (nil, boolean, number,
string, table) is supported.

```lua
G.save.set(namespace, key, value)
G.save.get(namespace, key) -> value | nil
G.save.has(namespace, key) -> boolean
G.save.delete(namespace, key)
G.save.list(namespace) -> { key = value, ... }
G.save.keys(namespace) -> { key1, key2, ... }
G.save.clear(namespace)
G.save.namespaces() -> { ns1, ns2, ... }
G.save.flush()                                 -- Checkpoint WAL to disk
```

Example:

```lua
-- Save player progress
G.save.set("save", "high_score", 42000)
G.save.set("settings", "volume", 0.8)

-- Load it back
local score = G.save.get("save", "high_score")  -- 42000
local vol   = G.save.get("settings", "volume")  -- 0.8
```

### G.log

```lua
G.log.set_level(channel, level)
G.log.get_level(channel) -> string
G.log.info(message)
G.log.warn(message)
G.log.error(message)
```

Channels: `general`, `graphics`, `physics`, `audio`, `input`, `assets`,
`lua`. Levels: `fatal`, `error`, `warn`, `info`, `debug`, `trace`.

### G.test

Available only when the engine is launched with `--test`. Drives synthetic
input and yields a coroutine across frames so headless integration tests
can be written in Lua.

```lua
G.test.key_down(key)              G.test.key_up(key)
G.test.mouse_down(button)         G.test.mouse_up(button)
G.test.mouse_move(x, y)           G.test.mouse_wheel(dx, dy)
G.test.controller_down(button)    G.test.controller_up(button)
G.test.controller_axis(axis, value)
G.test.wait_frames(n)
G.test.wait_seconds(seconds)
G.test.is_active() -> boolean
G.test.assert_true(cond [, msg])
```

### Userdata types

| Type | Description |
|------|-------------|
| `vec2` | 2D vector with `dot`, `length`, `lerp`, `distance`, `angle`, `rotate`, `reflect`, `project`, etc. |
| `vec3`, `vec4` | 3D/4D vectors with `dot`, `length`, `lerp`, `normalized`, `unpack`, `send_as_uniform` |
| `mat2x2`, `mat3x3`, `mat4x4` | Floating-point matrices with `send_as_uniform` |
| `byte_buffer` | Binary data buffer (supports `#` length and `..` concat) |
| `physics_handle` | Opaque handle to a Box2D body |
| `collision_shape` | Reusable shape (circle / aabb) for `G.collision` |
| `collision_world` | Spatial-hash collision world |
| `collision_handle` | Handle to a collider inside a `collision_world` |
| `canvas` | Off-screen render target with `dimensions`, `width`, `height` |
| `tilemap` | Multi-layer tilemap with rendering, collision, and TMX loading |
| `rng` | Random number generator state |
| `sprite_asset` | Reference to a loaded sprite |

## Asset pipeline

Place assets in your project directory. They are packed into an SQLite
database automatically when running or packaging.

### Supported formats

| Extension | Type |
|-----------|------|
| `.lua`, `.fnl` | Scripts |
| `.png`, `.qoi` | Images (PNG is converted to QOI) |
| `.sprites.json`, `.sprites.xml` | Spritesheet definitions |
| `.ogg`, `.wav` | Audio |
| `.ttf` | Fonts |
| `.vert`, `.frag` | Shaders (GLSL) |
| `.tmx` | Tiled tilemaps (XML) |
| `.tsx` | Tiled tilesets (XML) |
| `.json`, `.txt` | Text files |

### Spritesheet format

```json
{
  "atlas": "image.png",
  "width": 256,
  "height": 256,
  "sprites": [
    { "name": "player", "x": 0, "y": 0, "width": 64, "height": 64 }
  ]
}
```

XML spritesheets using the TextureAtlas format are also supported.

### Hot-reload

In development mode (`game run`), the engine watches the project directory
for changes using inotify. When a file changes, assets are repacked and
scripts are reloaded automatically. Disable with `--no-hotreload`.

## Example games

The `games/` directory contains example projects:

- **space-garbage/** --- Asteroids-style shooter with menus, waves of
  splitting meteors, three enemy types (chaser, turret, bomber), powerups,
  high scores, CRT shader, parallax starfield. Demonstrates the scene
  system, FSM-based AI, and steering behaviors.
- **platformer/** --- Tile-based platformer loaded from a Tiled TMX map.
  Demonstrates the tilemap system with AABB collision, animated player,
  gravity, and camera following.
- **flappybird/** --- Classic Flappy Bird clone written in Fennel.

The `assets/` directory contains additional test programs (testdrawing,
pong, etc.) used during engine development.

## Building from source

Requires CMake 3.21+, a C++17 compiler, and SDL3 (vendored). OpenGL
headers are required from the system.

```bash
cmake -G Ninja -B build
cmake --build build
```

### devenv (recommended)

The [devenv](https://devenv.sh/) development environment provides wrapped
build commands and all required tooling. After entering the dev shell, the
`game` binary is on `$PATH` from `build/`.

| Command | Description |
|---------|-------------|
| `game-build` | CMake configure + Ninja build (incremental) |
| `game-test` | Build and run tests (GoogleTest, always with sanitizers) |
| `game-run` | Build and run the engine against `assets/` |
| `game-clean` | Wipe `build/` directory |
| `game-format` | clang-format all source files |
| `game-tidy` | Run clang-tidy (findings are errors) |
| `game-include-cleaner` | Detect unused `#include` directives |
| `game-debug` | Launch binary under gf2 graphical debugger |
| `game-sanitize` | Build with AddressSanitizer + UBSan |
| `game-profile` | Build with Chrome Tracing profiler instrumentation |
| `game-samply` | CPU sampling profiler (opens Firefox Profiler) |
| `game-build-win64` | MinGW cross-compilation to Windows |

### Bundled libraries

All dependencies are vendored --- no system packages required beyond a C++17
compiler, OpenGL headers, and standard platform libraries.

Box2D, Lua 5.1, SDL3, PhysFS, ENet, mimalloc, glad, GoogleTest,
stb_truetype, stb_rect_pack, stb_image, stb_image_write, stb_vorbis,
dr_wav, pugixml, sqlite3, double-conversion, yyjson, backward-cpp,
pcg-random, Dear ImGui.

## Project structure

```
src/                 C++ engine source
libraries/           Vendored third-party libraries
games/               Example game projects
assets/              Test programs, sprites, shaders, audio
tests/               GoogleTest suite (272 tests, 14 files)
design/              Design documents and architecture notes
scripts/             Build and development helper scripts
cmake/               CMake toolchain files (MinGW, osxcross)
.github/workflows/   CI configuration (Linux, Windows, macOS)
```

### Source organization

All engine code lives in `src/` under namespace `G`. Major subsystems:

| Subsystem | Files | Description |
|-----------|-------|-------------|
| Core | `game.cc`, `engine.cc`, `config.cc`, `clock.cc`, `platform.cc` | Main loop, SDL init, hot-reload |
| Graphics | `renderer.cc`, `shaders.cc`, `image.cc`, `color.cc` | Batch renderer, textures, shaders |
| Audio | `sound.cc`, `qoa.cc` | SDL audio callback, QOA codec |
| Physics | `physics.cc`, `collision.cc`, `collision_world.cc` | Box2D wrapper, spatial hash |
| Input | `input.cc` | Keyboard, mouse, gamepad |
| Tilemap | `tilemap.cc` | Multi-layer tilemap with TMX loading, collision |
| Assets | `assets.cc`, `packer.cc`, `filesystem.cc` | SQLite asset DB, packing pipeline |
| Lua bindings | `lua_*.cc` (23 files) | `G.*` API exposed to scripts |
| CLI | `cmd_*.cc` (10 files) | Subcommand implementations |
| Data structures | `vec.h`, `mat.h`, `array.h`, `dictionary.h`, `allocators.h` | Header-only containers |
| Threading | `executor.cc` | Thread pool with work stealing |
| Debug | `debug_ui.cc`, `logging.cc`, `stats.cc`, `profiler.cc` | ImGui overlay, tracing |

## Static analysis

These tools require the [devenv](https://devenv.sh/) development
environment, which provides wrapped versions of clang-tidy and
clang-include-cleaner with the correct Nix include paths.

### clang-tidy

```bash
game-tidy
```

Builds the project with clang-tidy enabled. Findings are treated as errors.
Configuration lives in `.clang-tidy`.

### Unused include detection

```bash
game-include-cleaner
```

Runs `clang-include-cleaner` on all engine sources, reporting only unused
`#include` directives (insertions are disabled).

### Sanitizers (ASan + UBSan)

```bash
game-sanitize
```

Builds a Debug binary with AddressSanitizer and UndefinedBehaviorSanitizer
enabled. The test binary always has sanitizers on.

## Asset credits

- `music.ogg` - "Cyber City Detectives" by Eric Matyas ([soundimage.org](https://www.soundimage.org))
- `sheet.xml` / `sheet.qoi` - [Space Shooter Redux](https://www.kenney.nl/assets/space-shooter-redux) by Kenney
- `game-over.ogg` - [Game Over Arcade](https://pixabay.com/sound-effects/game-over-arcade-6435) from Pixabay
- `pong-blip1.wav` / `pong-blip2.wav` - [NoiseCollector](https://freesound.org/people/NoiseCollector/packs/254/) on Freesound
- `pong-score.wav` - [KSAplay](https://freesound.org/people/KSAplay/sounds/758958/) on Freesound
