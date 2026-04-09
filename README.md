# Game Engine

A Lua 5.1 game engine built with C++17, SDL2, and OpenGL. Write games in Lua (or [Fennel](https://fennel-lang.org/)) with built-in physics, audio, asset management, and hot-reloading.

## Quick start

```bash
# Create a new project
game init my-game
cd my-game

# Run in development mode (with hot-reload)
game run
```

This creates the following project structure:

```
my-game/
├── conf.json              # Window and project configuration
├── main.lua               # Entry point (must return a game module)
├── game.lua               # Game module with init/update/draw
├── .luarc.json            # LuaLS editor configuration
└── definitions/
    └── game.lua           # Auto-generated type stubs for IDE support
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

## CLI reference

### `game init [directory]`

Create a new project with scaffold files. Uses the current directory if none is specified. Fails if `conf.json` already exists.

### `game run [directory] [flags] [-- game-args...]`

Run a project in development mode.

| Flag | Description |
|------|-------------|
| `--no-hotreload` | Disable file watching and hot-reload |
| `--clean` | Delete cached database and repack all assets |
| `--` | Everything after this is forwarded to `G.system.cli_arguments()` |

Assets are cached in `~/.cache/game/<project-hash>/assets.sqlite3`.

### `game package [directory] [options]`

Bundle a project for distribution.

| Option | Description |
|--------|-------------|
| `-o, --output <dir>` | Output directory (default: `dist`) |
| `--name <name>` | Override binary name (default: `app_name` from conf.json) |
| `--strip` | Strip debug symbols from the binary |

### `game stubs [--output <path>]`

Regenerate LuaLS type stubs. Default output: `definitions/game.lua`.

### `game version`

Print engine version and build date.

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
G.graphics.set_color(r, g, b, a)

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
```

Text input is delivered to the game module through the
`Game:textinput(input)` callback rather than a polling API.

### G.math

```lua
G.math.clamp(x, low, high) -> number

-- Vectors
G.math.v2(x, y) -> vec2
G.math.v3(x, y, z) -> vec3
G.math.v4(x, y, z, w) -> vec4

-- Matrices (row-major order)
G.math.m2x2(v1..v4)  -> mat2x2
G.math.m3x3(v1..v9)  -> mat3x3
G.math.m4x4(v1..v16) -> mat4x4
```

Vector methods (`vec2`, `vec3`, `vec4`): `dot(other)`, `len2()`,
`normalized()`, `send_as_uniform(name)`. Operators: `+`, `-`, `* (scalar)`.

Matrix methods (`mat2x2`, `mat3x3`, `mat4x4`): `send_as_uniform(name)`.

### G.physics

Box2D-backed 2D physics. Top-down friction is simulated with a friction
joint to a static ground body, so `create_ground()` must be called before
adding any dynamic bodies.

```lua
-- Setup
G.physics.create_ground([walls])           -- walls=true adds screen-edge fixtures
G.physics.set_collision_categories({ "player", "enemy", ... })
G.physics.set_collision_callback(function(a, b) ... end)

-- Bodies (options table is optional: density, friction, restitution,
-- sensor, category, mask)
G.physics.add_box(tx, ty, bx, by, angle, userdata [, options]) -> physics_handle
G.physics.add_circle(cx, cy, radius, userdata [, options]) -> physics_handle
G.physics.destroy_handle(handle)

-- Position and rotation
G.physics.position(handle) -> x, y
G.physics.set_position(handle, x, y)
G.physics.angle(handle) -> radians
G.physics.rotate(handle, angle)
G.physics.set_fixed_rotation(handle, fixed)
G.physics.get_fixed_rotation(handle) -> boolean

-- Velocity
G.physics.linear_velocity(handle) -> vx, vy
G.physics.set_linear_velocity(handle, vx, vy)
G.physics.angular_velocity(handle) -> omega
G.physics.set_angular_velocity(handle, omega)

-- Forces and impulses
G.physics.apply_force(handle, x, y)
G.physics.apply_linear_impulse(handle, x, y)
G.physics.apply_torque(handle, torque)

-- Body tuning
G.physics.set_linear_damping(handle, damping)
G.physics.set_angular_damping(handle, damping)
G.physics.set_gravity_scale(handle, scale)
G.physics.set_bullet(handle, bullet)        -- Continuous collision detection
```

### G.collision

Lightweight broad-phase collision world (separate from Box2D — use this
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
world:get_overlaps(handle) -> hits
world:raycast(ox, oy, dx, dy, max_dist [, mask]) -> hit?
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
```

### G.log

```lua
G.log.set_level(channel, level)
G.log.get_level(channel) -> string
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
| `vec2`, `vec3`, `vec4` | Floating-point vectors with `dot`, `len2`, `normalized`, `send_as_uniform` |
| `mat2x2`, `mat3x3`, `mat4x4` | Floating-point matrices with `send_as_uniform` |
| `byte_buffer` | Binary data buffer (supports `#` length and `..` concat) |
| `physics_handle` | Opaque handle to a Box2D body |
| `collision_shape` | Reusable shape (circle / aabb) for `G.collision` |
| `collision_world` | Spatial-hash collision world |
| `collision_handle` | Handle to a collider inside a `collision_world` |
| `canvas` | Off-screen render target with `dimensions`, `width`, `height` |
| `rng` | Random number generator state |
| `sprite_asset` | Reference to a loaded sprite |

## Asset pipeline

Place assets in an `assets/` directory in your project. They are packed into an SQLite database automatically when running or packaging.

### Supported formats

| Extension | Type |
|-----------|------|
| `.lua`, `.fnl` | Scripts |
| `.png`, `.qoi` | Images (PNG is converted to QOI) |
| `.sprites.json`, `.sprites.xml` | Spritesheet definitions |
| `.ogg`, `.wav` | Audio |
| `.ttf` | Fonts |
| `.vert`, `.frag` | Shaders (GLSL) |
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

In development mode (`game run`), the engine watches the project directory for changes using inotify. When a file changes, assets are repacked and scripts are reloaded automatically. Disable with `--no-hotreload`.

## Building from source

Requires CMake 3.21+, a C++17 compiler, SDL2, and OpenGL.

```bash
cmake -B build
cmake --build build
```

### System dependencies

- **SDL2** - install via your package manager (`libsdl2-dev`, `sdl2`, etc.)
- **OpenGL** - typically provided by your graphics driver

### Bundled libraries

Box2D, Lua 5.1, PhysFS, mimalloc, glad, stb_truetype, stb_rect_pack, stb_vorbis, dr_wav, pugixml, sqlite3, double-conversion.

## Static analysis

These tools require the [devenv](https://devenv.sh/) development environment, which provides wrapped versions of clang-tidy and clang-include-cleaner with the correct Nix include paths.

### clang-tidy

```bash
game-tidy
```

Builds the project with clang-tidy enabled. Findings are treated as errors. Configuration lives in `.clang-tidy`.

### Unused include detection

```bash
game-include-cleaner
```

Runs `clang-include-cleaner` on all engine sources, reporting only unused `#include` directives (insertions are disabled). Requires a compile database in `build/` (generated automatically).

### Sanitizers (ASan + UBSan)

```bash
game-sanitize
```

Builds a Debug binary with AddressSanitizer and UndefinedBehaviorSanitizer enabled. The test binary always has sanitizers on.

## Asset credits

- `music.ogg` - "Cyber City Detectives" by Eric Matyas ([soundimage.org](https://www.soundimage.org))
- `sheet.xml` / `sheet.qoi` - [Space Shooter Redux](https://www.kenney.nl/assets/space-shooter-redux) by Kenney
- `game-over.ogg` - [Game Over Arcade](https://pixabay.com/sound-effects/game-over-arcade-6435) from Pixabay
- `pong-blip1.wav` / `pong-blip2.wav` - [NoiseCollector](https://freesound.org/people/NoiseCollector/packs/254/) on Freesound
- `pong-score.wav` - [KSAplay](https://freesound.org/people/KSAplay/sounds/758958/) on Freesound
