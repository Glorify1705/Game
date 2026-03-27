# Animation System

A sprite animation system for frame-based animations from spritesheets, with
named sequences, per-frame timing, and playback control.

## Glossary

**Spritesheet (atlas)**: A single texture containing multiple sprite frames
arranged in a grid or packed layout. Instead of loading 60 separate images for a
walk cycle, you load one image and draw rectangular sub-regions of it. Our
engine already supports spritesheets via JSON/XML metadata in the asset DB.

**Frame**: One image in an animation sequence. Defined by a rectangular region
in the spritesheet (x, y, width, height in pixels). Each frame has its own
display duration.

**Sequence (tag, clip)**: A named series of frames that form a complete
animation. "idle" might be frames 0-3, "walk" might be frames 4-9. The name
comes from Aseprite's "frame tag" concept, which is the de facto standard for
pixel art animation authoring.

**Playback mode**: How the sequence behaves when it reaches the end. *Loop*
repeats from the beginning. *Once* stops at the last frame. *Bounce* (pingpong)
reverses direction at both ends: 0,1,2,3,2,1,0,1,2,...

**Animation definition**: The immutable data describing an animation: which
spritesheet, frame rectangles, durations, playback mode. Shared across all
instances. Analogous to high_impact's `anim_def_t`.

**Animation instance**: The mutable runtime state for one playing animation:
current time, current frame, flip flags. Each game entity has its own instance
pointing to a shared definition. Analogous to high_impact's `anim_t`.

**Flip**: Mirroring the sprite horizontally or vertically. Horizontal flip is
essential for characters that face left/right using the same art. Implemented by
negating the source rectangle width (GPU-side, no extra textures needed).

**Frame callback**: A function called when a specific event occurs during
playback, such as reaching the last frame or completing a loop. Used for
gameplay logic: spawn a projectile on the "attack" frame, play a footstep sound
on specific walk frames, transition to another animation on completion.

## Engine survey

| Feature | anim8 (Love2D) | high_impact | Anchor | peachy (Love2D) | Godot AnimatedSprite2D | GameMaker |
|---|---|---|---|---|---|---|
| **Data model** | Grid + Animation objects | anim_def_t (shared) + anim_t (instance) | Animation object | Parses Aseprite JSON | SpriteFrames resource + node | Built-in sprite asset |
| **Frame source** | Grid of Quads from atlas | Tile indices into spritesheet | Grid quads | Aseprite atlas rects | Individual textures or atlas | Strip of sub-images |
| **Per-frame duration** | Yes (seconds, per-frame table) | No (uniform frame_time) | No (uniform delay) | Yes (milliseconds, from Aseprite) | Yes (relative multiplier on base FPS) | No (uniform image_speed) |
| **Timing method** | Accumulator + binary search | Absolute time (stateless) | Accumulator | Accumulator (ms) | FPS + duration multiplier | image_speed added per step |
| **Loop** | Via onLoop callback (default) | Yes (default) | loop=0 (infinite) | From Aseprite tag direction | set_animation_loop | Default (wraps image_index) |
| **Once** | onLoop = pauseAtEnd | ANIM_STOP sentinel in sequence | loop=1 | From Aseprite tag direction | set_animation_loop(false) | Catch Animation End event |
| **Bounce/pingpong** | Manual frame sequence | Manual frame sequence | bounce=true | From Aseprite tag direction | Not built-in | Manual |
| **Reverse** | Manual frame sequence | Manual frame sequence | reverse=true | From Aseprite tag direction | play_backwards() | Negative image_speed |
| **Flip X/Y** | flipH/flipV booleans | flip_x/flip_y on instance | Not documented | Via sx/sy in draw() | flip_h/flip_v properties | Negative image_xscale/yscale |
| **Callbacks** | onLoop(self, count) | None (poll anim_looped) | onLoop, onFrame | onLoop(fn, ...) | animation_finished, frame_changed, animation_looped signals | Animation End event |
| **State machine** | Manual (check current name) | Manual | Manual | setTag is no-op if same | play(name) switches | Manual sprite_index changes |
| **Hot-reload** | Lua state lost, re-register | Def survives, instance needs start_time | Lua state lost | Lua state lost | Scene tree preserves | N/A |
| **Memory** | Quads cached per grid key | bump_alloc for defs, inline for instances | Lua tables | Lua tables | Godot resource system | Engine-managed |

### Key takeaways

1. **anim8** is the most flexible Lua animation library. Per-frame durations via
   table syntax, binary search for frame lookup (handles dt spikes correctly),
   and the Grid abstraction for atlas frame selection. The `onLoop` callback
   pattern for implementing once/bounce modes is clever but unintuitive --
   dedicated mode flags are better.

2. **high_impact** has the cleanest C implementation. The def/instance split is
   perfect for our engine: definitions are allocated once from an arena and
   shared, instances are small value types (40 bytes) stored inline in entities.
   The absolute-time approach (frame derived from `engine.time - start_time`)
   is stateless and hot-reload friendly, but only works with uniform frame
   durations.

3. **peachy** demonstrates Aseprite integration well. Aseprite's JSON export
   is the de facto standard for pixel art games. Supporting it natively
   eliminates manual frame rectangle specification.

4. **Godot** separates the resource (SpriteFrames, immutable) from the node
   (AnimatedSprite2D, mutable). Signals for animation events are powerful.
   The relative duration multiplier per frame (on top of base FPS) is a nice
   compromise between uniform timing and full per-frame control.

5. **GameMaker**'s `image_index` as a float with `image_speed` as delta-per-step
   is the simplest possible model. No objects, no callbacks, just two numbers.
   Limited but extremely easy to understand.

6. **Common pattern**: Every engine that supports "switch animation" makes it a
   no-op if the requested animation is already playing. This prevents restarting
   an animation every frame when the game code naively calls
   `play("walk")` in the update loop.

## Current engine state

Our spritesheet system (from `renderer.h` / `assets.h`):

```cpp
struct Sprite {
  std::string_view name;
  std::string_view spritesheet;  // References parent sheet
  size_t x, y, width, height;    // Rect within spritesheet
};

struct Spritesheet {
  std::string_view name;
  std::string_view image;  // References texture image
  size_t width, height;
};
```

Drawing a sprite:
```cpp
ErrorOr<void> DrawSprite(std::string_view sprite_name, FVec2 position, float angle);
```

From Lua:
```lua
G.graphics.draw_sprite("player_idle_0", x, y)
```

Currently, animation is done manually in Lua:

```lua
local frames = {"player_idle_0", "player_idle_1", "player_idle_2"}
local frame_time = 0.15
local timer = 0
local current = 1

function g.update(t, dt)
  timer = timer + dt
  if timer >= frame_time then
    timer = timer - frame_time
    current = current % #frames + 1
  end
end

function g.draw()
  G.graphics.draw_sprite(frames[current], player.x, player.y)
end
```

This works but requires every game to reimplement animation timing, looping,
flip, and sequence management.

## Proposed design

### Where to implement

**C++ with Lua bindings** (like camera, physics, sound). Reasons:

- Animation definitions are asset data -- they should be loaded from the asset
  DB alongside spritesheets, not constructed in Lua each frame
- Frame timing with accumulator needs correct dt handling (while-loop for
  frame skipping), which is error-prone in Lua
- The def/instance split maps naturally to C++ (shared const data + small
  mutable state)
- Animation instances are small fixed-size structs, ideal for our allocator
  model (no dynamic allocation per instance)
- Keeps Lua focused on gameplay: "play walk animation" not "manage frame
  rectangles and timers"

The system would be exposed as `G.animation`.

### Data model

**Animation definition** (shared, immutable after load):

```cpp
struct AnimFrame {
  std::string_view sprite_name;  // Name in asset DB (resolves to atlas rect)
  float duration;                // Seconds this frame is displayed
};

enum PlaybackMode : uint8_t {
  kLoop,       // Repeat from start
  kOnce,       // Stop at last frame
  kBounce,     // Reverse at endpoints (pingpong)
};

struct AnimDef {
  std::string_view name;         // "player_walk", "explosion", etc.
  PlaybackMode mode;
  uint16_t frame_count;
  AnimFrame* frames;             // Allocated from arena, contiguous
  float total_duration;          // Precomputed sum of all frame durations
  float* intervals;              // Cumulative durations for binary search
};
```

**Animation instance** (per-entity, mutable):

```cpp
struct Anim {
  AnimDef* def;                  // Shared definition (not owned)
  float timer;                   // Elapsed time in current cycle
  int16_t frame;                 // Current frame index
  int8_t direction;              // +1 forward, -1 reverse (for bounce)
  bool playing;
  bool flip_x;
  bool flip_y;
  float speed;                   // Playback speed multiplier (1.0 = normal)

  // Lua callback references (LUA_NOREF when unset)
  int on_loop_ref;               // Called when animation loops/completes
  int on_frame_ref;              // Called when frame changes
};
```

Instance size: ~40 bytes. No heap allocation. Stored as Lua userdata.

### How definitions are loaded

Animation definitions come from the asset pipeline, not from Lua code.
Two sources:

**1. Aseprite JSON** (preferred for pixel art):

The artist exports from Aseprite with `--format json-array --list-tags`. The
packer parses the JSON and stores animation definitions in the asset DB:

```json
{
  "frames": [
    {"filename": "player 0", "frame": {"x":0,"y":0,"w":32,"h":32}, "duration": 100},
    {"filename": "player 1", "frame": {"x":32,"y":0,"w":32,"h":32}, "duration": 100},
    {"filename": "player 2", "frame": {"x":64,"y":0,"w":32,"h":32}, "duration": 150}
  ],
  "meta": {
    "frameTags": [
      {"name": "idle", "from": 0, "to": 1, "direction": "forward"},
      {"name": "walk", "from": 0, "to": 2, "direction": "forward"},
      {"name": "hurt", "from": 2, "to": 2, "direction": "forward"}
    ]
  }
}
```

The packer converts this into `AnimDef` entries:
- Frame tags become animation definitions
- `direction` maps to PlaybackMode (forward=kLoop, pingpong=kBounce)
- Per-frame `duration` (milliseconds) is preserved
- Sprite names are auto-generated from spritesheet name + frame index

**2. Lua definition** (for procedural or runtime animations):

```lua
G.animation.new_def("sparkle", {
  sprites = {"sparkle_0", "sparkle_1", "sparkle_2", "sparkle_3"},
  durations = {0.1, 0.1, 0.15, 0.2},  -- per-frame, in seconds
  mode = "loop",  -- "loop", "once", or "bounce"
})
```

Or with uniform timing:

```lua
G.animation.new_def("coin_spin", {
  sprites = {"coin_0", "coin_1", "coin_2", "coin_3"},
  duration = 0.08,  -- uniform for all frames
  mode = "loop",
})
```

Definitions from Lua are allocated from the frame allocator's parent arena.
They persist until hot-reload, at which point Lua re-registers them.

### Lua API

```lua
-- Create an animation instance from a definition
G.animation.new(def_name)  --> anim (userdata)

-- Playback control
anim:play()                -- start/resume playback
anim:play(def_name)        -- switch to a different animation (no-op if same)
anim:pause()               -- freeze at current frame
anim:stop()                -- pause and rewind to first frame
anim:rewind()              -- go to first frame, keep playing

-- State queries
anim:is_playing()          --> bool
anim:frame()               --> int (1-based frame index)
anim:set_frame(n)          -- jump to frame n (1-based)
anim:finished()            --> bool (true if once-mode and reached end)
anim:looped()              --> int (number of completed loops)

-- Properties
anim:set_speed(s)          -- playback speed multiplier (default 1.0)
anim:speed()               --> number
anim:set_flip_x(bool)
anim:set_flip_y(bool)
anim:flip_x()              --> bool
anim:flip_y()              --> bool

-- Drawing
anim:draw(x, y)
anim:draw(x, y, angle)
anim:draw(x, y, angle, sx, sy)
anim:draw(x, y, angle, sx, sy, ox, oy)

-- Current frame info (for custom drawing)
anim:sprite_name()         --> string (current frame's sprite name)
anim:dimensions()          --> w, h

-- Callbacks
anim:on_loop(fn)           -- fn(anim, loop_count) called on loop/completion
anim:on_frame(fn)          -- fn(anim, frame_index) called on every frame change

-- Definition management
G.animation.new_def(name, opts)  -- register from Lua (see above)
G.animation.has_def(name)        --> bool
```

### Usage example

```lua
local g = {}
local player = {}

function g.init()
  -- Definitions auto-loaded from Aseprite JSON in asset DB.
  -- Create an instance:
  player.x, player.y = 100, 100
  player.anim = G.animation.new("player_idle")
  player.anim:on_loop(function(anim, count)
    -- Could trigger particles, sound, etc.
  end)
  player.facing = 1  -- 1 = right, -1 = left
end

function g.update(t, dt)
  local vx = 0
  if G.input.is_down("left")  then vx = -1; player.facing = -1 end
  if G.input.is_down("right") then vx =  1; player.facing =  1 end

  player.x = player.x + vx * 120 * dt

  -- Switch animation (no-op if already playing the same one)
  if vx ~= 0 then
    player.anim:play("player_walk")
  else
    player.anim:play("player_idle")
  end
  player.anim:set_flip_x(player.facing == -1)
end

function g.draw()
  player.anim:draw(player.x, player.y)
end

return g
```

### One-shot animation (attack, explosion)

```lua
function player_attack()
  player.anim:play("player_attack")
  player.anim:on_loop(function(anim)
    -- Attack animation is "once" mode, so this fires at the end
    anim:play("player_idle")
    anim:on_loop(nil)  -- clear callback
  end)
end
```

### Animation update logic

Called by the engine once per frame, after `Lua.Update()`:

```
1. If not playing, return.

2. Advance timer:
   timer += dt * speed * direction

3. If timer is within [0, total_duration]:
   - Binary search intervals[] to find current frame index
   - If frame changed from previous, invoke on_frame callback
   - Return

4. Timer exceeded bounds (loop/completion):
   a. mode == kOnce:
      - Clamp to last frame (or first if reversed)
      - Set playing = false
      - Invoke on_loop callback with loop_count
   b. mode == kLoop:
      - Wrap timer: timer = fmod(timer, total_duration)
      - Increment loop_count
      - Binary search for new frame
      - Invoke on_loop callback
   c. mode == kBounce:
      - Reverse direction
      - Reflect timer off the boundary
      - Binary search for new frame
      - Invoke on_loop callback every full cycle (forward+back)
```

### Binary search for frame lookup

With per-frame durations, we need to find which frame corresponds to the current
timer value. Precompute cumulative intervals at definition time:

```
frames:     [0.1, 0.1, 0.15, 0.2]
intervals:  [0.1, 0.2, 0.35, 0.55]
```

Binary search `intervals` for the smallest value >= timer. This is O(log n) and
correctly handles dt spikes that skip multiple frames (unlike incrementing
frame-by-frame in a while loop).

For typical animations (4-12 frames), binary search and linear scan perform
similarly, but binary search is correct for all cases with no extra code.

### Frame change detection

Track `prev_frame` alongside `frame`. After updating the timer and computing the
new frame index, if `frame != prev_frame`, invoke the `on_frame` callback. This
catches both normal frame advances and jumps from `set_frame()`.

### Flip implementation

Flip is applied at draw time by negating the source rectangle dimension in the
draw command, which the GPU handles for free. No extra textures or sprite data
needed.

```cpp
float draw_w = def->frames[frame].width;
float draw_h = def->frames[frame].height;
if (flip_x) draw_w = -draw_w;
if (flip_y) draw_h = -draw_h;
```

This is the same approach as Raylib (negate source rect) and GameMaker (negate
scale). The origin point must account for the flip to prevent position shifting.

### play(name) as animation switch

The most common pattern in game code is calling `play("walk")` every frame
while the character is walking. This must be a no-op if the animation is already
playing "walk" -- otherwise it restarts every frame.

```cpp
void Play(std::string_view name) {
  AnimDef* new_def = LookupDef(name);
  if (new_def == def && playing) return;  // Already playing this animation
  def = new_def;
  timer = 0;
  frame = 0;
  direction = 1;
  playing = true;
  loop_count = 0;
}
```

This is what peachy, sodapop, and Godot all do. It makes animation state
machines trivial to write without explicit "am I already playing this?" checks.

### Interaction with hot-reload

On hot-reload:
1. All Lua state is destroyed, including animation userdata
2. Asset DB is reloaded, including animation definitions
3. Lua `init()` re-runs, re-creating animation instances

Animation instances are Lua userdata, so they are garbage collected with the Lua
state. Animation definitions are loaded from the asset DB, so they are
reconstructed from the (potentially modified) asset data.

The Lua callback refs (`on_loop_ref`, `on_frame_ref`) become invalid on reload,
but since the entire Lua state is destroyed, this is not a leak --
`luaL_unref` is unnecessary when the state itself is closed.

**What this means for the user**: Animation state (current frame, timer) is lost
on hot-reload. The `init()` callback re-creates everything. This matches how
physics bodies and sound sources already work.

### Interaction with time scale

The animation system uses the engine's `dt` which is already affected by any
future time scale system (see [[Timer and tween system]]). When the game is in
slow-motion, animations automatically slow down.

If specific animations need to ignore time scale (e.g., UI animations during
pause), the `speed` multiplier on the instance can compensate:
`anim:set_speed(1.0 / time_scale)`. A dedicated `ignore_time_scale` flag could
be added later if this pattern is common.

### Memory model

- **AnimDef**: Allocated from the engine's main arena allocator. Contiguous
  `frames[]` and `intervals[]` arrays follow the struct. Freed on hot-reload
  (arena reset) and reloaded from assets.
- **Anim**: Lua userdata (~40 bytes). Allocated by Lua's allocator (which uses
  our arena). Garbage collected when the Lua reference is lost.
- **No per-frame heap allocation**: Update and draw are allocation-free.
- **Definition lookup**: `Dictionary<AnimDef*>` keyed by name, same pattern as
  `loaded_sprites_table_` in the renderer.

### What this does NOT include

- **Skeletal / bone animation**: Out of scope. This is frame-based sprite
  animation only.
- **Animation blending / crossfade**: Blending between two sprite animations
  doesn't make sense (unlike skeletal). Transitions are instant switches.
- **Built-in state machine**: Game code manages which animation to play.
  A state machine abstraction could be built in Lua on top of this system
  if needed.
- **Sprite trimming / pivot points**: Aseprite's trim data
  (`spriteSourceSize`) is useful for atlas packing efficiency but adds
  complexity. Can be added later if atlas size becomes a concern.
- **Animation events on specific frames**: The `on_frame` callback fires on
  every frame change. For "play footstep sound on frame 3", the callback checks
  `if frame_index == 3`. A dedicated per-frame event system (like Godot's
  AnimationPlayer markers) is overkill for our use case.

## Asset pipeline changes

The packer (`packer.cc`) needs to:

1. Detect Aseprite JSON files alongside spritesheet images
2. Parse `frameTags` and `frames` arrays
3. Create `AnimDef` entries in the asset DB with:
   - Name: `"{spritesheet}_{tag}"` (e.g., `"player_idle"`)
   - Frame references pointing to the spritesheet's sprite entries
   - Per-frame durations (converted from ms to seconds)
   - Playback mode from tag direction

The asset DB schema adds a table:

```sql
CREATE TABLE animations (
  name TEXT PRIMARY KEY,
  spritesheet TEXT NOT NULL,
  mode INTEGER NOT NULL,        -- 0=loop, 1=once, 2=bounce
  frame_count INTEGER NOT NULL,
  frame_data BLOB NOT NULL      -- packed array of {sprite_index, duration_ms}
);
```

## Implementation plan

### Phase 1: Core animation

1. Add `AnimDef` and `Anim` structs in `animation.h`.
2. `AnimDef` storage in `Renderer` (alongside existing sprite/spritesheet
   tables) or a new `Animation` module.
3. `Anim::Update(float dt)` with accumulator, binary search, mode handling.
4. `Anim::Draw()` delegates to `BatchRenderer::PushQuad` with the current
   frame's sprite rect.
5. Lua bindings: `G.animation.new(name)`, `anim:play()`, `anim:draw()`,
   `anim:pause()`, `anim:stop()`, `anim:set_flip_x()`, `anim:set_flip_y()`.
6. `G.animation.new_def()` for Lua-side definition creation.

### Phase 2: Aseprite pipeline

1. Packer detects and parses Aseprite JSON alongside spritesheet images.
2. AnimDef entries written to asset DB.
3. Hot-reload: animation definitions reloaded from updated assets.

### Phase 3: Callbacks and polish

1. `on_loop` and `on_frame` callbacks via `luaL_ref`.
2. `looped()` query (loop counter).
3. `finished()` query for once-mode animations.
4. Speed multiplier.
