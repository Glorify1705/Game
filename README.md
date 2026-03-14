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

All functions are accessed through the global `G` object.

### G.graphics

```lua
G.graphics.clear()
G.graphics.set_color(color, r, g, b, a)

-- Drawing
G.graphics.draw_sprite(sprite, x, y [, angle])
G.graphics.draw_image(sprite, x, y [, angle])
G.graphics.draw_rect(x1, y1, x2, y2 [, angle])
G.graphics.draw_circle(x, y, r)
G.graphics.draw_triangle(p1x, p1y, p2x, p2y, p3x, p3y)
G.graphics.draw_line(p1x, p1y, p2x, p2y)
G.graphics.draw_lines(points)

-- Text
G.graphics.print(text, x, y)                              -- Debug font
G.graphics.draw_text(font, text, size, x, y)               -- Named font
G.graphics.text_dimensions(font, size, text) -> width, height

-- Screenshots
G.graphics.take_screenshot([file]) -> byte_buffer | nil

-- Transform stack
G.graphics.push(transform)     -- Push a mat4x4
G.graphics.pop()
G.graphics.rotate(angle)       -- Radians, clockwise
G.graphics.scale(xf, yf)
G.graphics.translate(x, y)

-- Shaders
G.graphics.new_shader([source])
G.graphics.attach_shader([name])       -- nil resets to default
G.graphics.send_uniform(name, value)   -- Accepts vec/mat/float
```

### G.window

```lua
G.window.dimensions() -> width, height
G.window.set_dimensions(width, height)
G.window.set_fullscreen()
G.window.set_borderless()
G.window.set_windowed()
G.window.set_title(title)
G.window.get_title() -> title
G.window.has_input_focus() -> boolean
G.window.has_mouse_focus() -> boolean
```

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

### G.math

```lua
G.math.clamp(x, low, high) -> number

-- Vectors
G.math.v2(x, y) -> vec2
G.math.v3(x, y, z) -> vec3
G.math.v4(x, y, z, w) -> vec4

-- Matrices (row-major order)
G.math.m2x2(v1..v4) -> mat2x2
G.math.m3x3(v1..v9) -> mat3x3
G.math.m4x4(v1..v16) -> mat4x4
```

Vector methods (`vec2`, `vec3`, `vec4`): `dot(other)`, `len2()`, `normalized()`, `send_as_uniform(name)`.
Operators: `+`, `-`, `* (scalar)`.

Matrix methods (`mat2x2`, `mat3x3`, `mat4x4`): `send_as_uniform(name)`.

### G.physics

Box2D-backed 2D physics.

```lua
-- Bodies
G.physics.add_box(tx, ty, bx, by, angle, callback) -> physics_handle
G.physics.add_circle(cx, cy, radius, callback) -> physics_handle
G.physics.destroy_handle(handle)
G.physics.create_ground()

-- Queries
G.physics.position(handle) -> x, y
G.physics.angle(handle) -> radians

-- Forces
G.physics.rotate(handle, angle)
G.physics.apply_linear_impulse(handle, x, y)
G.physics.apply_force(handle, x, y)
G.physics.apply_torque(handle, torque)

-- Collisions
G.physics.set_collision_callback(callback)
```

### G.sound

```lua
G.sound.play(name)                          -- Load and play immediately
G.sound.add_source(name) -> source_id       -- Load without playing
G.sound.play_source(source_id)
G.sound.stop_source(source_id)
G.sound.set_volume(source_id, gain)         -- 0.0 to 1.0
G.sound.set_global_volume(gain)             -- 0.0 to 1.0
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

### G.system

```lua
G.system.quit()
G.system.operating_system() -> string
G.system.cpu_count() -> integer
G.system.set_clipboard(text)
G.system.get_clipboard() -> string
G.system.open_url(url) -> error
G.system.cli_arguments() -> table
```

### G.clock

```lua
G.clock.walltime() -> seconds
G.clock.gametime() -> seconds
G.clock.gamedelta() -> seconds
G.clock.sleep_ms(ms)
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

### Userdata types

| Type | Description |
|------|-------------|
| `vec2`, `vec3`, `vec4` | Floating-point vectors with `dot`, `len2`, `normalized`, `send_as_uniform` |
| `mat2x2`, `mat3x3`, `mat4x4` | Floating-point matrices with `send_as_uniform` |
| `byte_buffer` | Binary data buffer (supports `#` length and `..` concat) |
| `physics_handle` | Opaque handle to a Box2D body |
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

## Asset credits

- `music.ogg` - "Cyber City Detectives" by Eric Matyas ([soundimage.org](https://www.soundimage.org))
- `sheet.xml` / `sheet.qoi` - [Space Shooter Redux](https://www.kenney.nl/assets/space-shooter-redux) by Kenney
- `game-over.ogg` - [Game Over Arcade](https://pixabay.com/sound-effects/game-over-arcade-6435) from Pixabay
- `pong-blip1.wav` / `pong-blip2.wav` - [NoiseCollector](https://freesound.org/people/NoiseCollector/packs/254/) on Freesound
- `pong-score.wav` - [KSAplay](https://freesound.org/people/KSAplay/sounds/758958/) on Freesound
