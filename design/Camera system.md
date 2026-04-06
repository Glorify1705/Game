---
status: implemented
tags: [camera, lua-api]
---

# Camera System

A 2D camera abstraction for scrolling, following targets, and screen effects.

## Glossary

**Lerp (linear interpolation)**: A way to smoothly move a value toward a
target over time. Instead of snapping the camera instantly to the player, we
move it a fraction of the remaining distance each frame. With a lerp factor of
0.1, the camera closes 10% of the gap per frame -- fast when far away, slow
when close, creating a smooth "ease out" feel. A factor of 1.0 means instant
(no smoothing), 0.0 means the camera never moves.

**Deadzone**: A rectangular region in the center of the screen where the
player can move freely without the camera following. Think of it as "slack" in
the camera. In a platformer, a wide horizontal deadzone lets the player walk
left and right a bit without the camera jerking back and forth. The camera
only starts moving when the player reaches the edge of this rectangle.
Without a deadzone, the camera tracks every pixel of movement, which can
feel twitchy.

**Bounds (clamping)**: The world rectangle that the camera is not allowed to
see outside of. If the game world is 2000x1000 pixels, setting bounds to that
rectangle prevents the camera from scrolling past the edges and showing empty
space. The camera position is "clamped" (restricted) so the visible viewport
always stays inside the bounds.

**Screen shake**: A visual effect where the camera rapidly oscillates back and
forth for a short duration. Used for explosions, damage hits, landing impacts.
The shake offset is temporary and decays to zero -- it does not affect the
camera's actual logical position.

**Parallax**: A depth illusion created by moving background layers at different
speeds. Far-away layers (sky, mountains) move slowly relative to the camera,
nearby layers (trees, buildings) move faster. A parallax factor of 0.3 means
that layer moves at 30% of the camera speed, making it feel distant.

**View matrix**: A 4x4 matrix that transforms world coordinates into screen
coordinates. It encodes the camera's position, zoom, and rotation into a
single matrix that the GPU uses to position everything on screen. "Attaching"
the camera means pushing this matrix onto the transform stack so all
subsequent drawing is offset by the camera.

**Coordinate conversion (world/screen)**: Translating between two coordinate
systems. "World coordinates" are positions in the game world (e.g., the player
is at world position 500, 300). "Screen coordinates" are pixel positions on
the monitor (e.g., the mouse is at screen pixel 400, 250). The camera
determines the mapping: `to_world()` converts a screen position (like a mouse
click) into where that click landed in the game world. `to_screen()` does the
reverse.

**Zoom**: Scaling the camera's view. Zoom > 1 magnifies (shows less of the
world, everything appears bigger). Zoom < 1 shows more of the world,
everything appears smaller. Zoom affects the visible area: at 2x zoom, you see
half as much world in each direction.

**Framerate-independent**: Making behavior consistent regardless of how fast
the game runs. A naive lerp of 10% per frame moves faster at 120fps (120
small steps) than at 30fps (30 larger steps). Framerate-independent lerp
adjusts the factor based on delta time so the camera reaches the same position
after the same real-world time, regardless of framerate.

## Engine survey

| Feature | LOVE 2D | STALKER-X | high_impact | Godot Camera2D | GameMaker | MonoGame/Nez |
|---|---|---|---|---|---|---|
| **Built-in?** | No (primitives) | Library (Lua) | Yes (C struct) | Yes (node) | Yes (first-class) | No (pattern/lib) |
| **Follow** | Manual | 6 named presets | Entity ref | Parent node | Target object | ECS component |
| **Smoothing** | Manual lerp | Per-axis lerp 0-1 | Speed factor + min_vel | px/sec exponential | px/frame linear | Lerp factor 0-1 |
| **Dead zone** | Manual | 6 preset shapes | Rectangular + look-ahead | Drag margins 0-1 | Border pixels | RectangleF |
| **Bounds** | Manual | setBounds(x,y,w,h) | Collision map auto | 4 pixel limits + smooth | Manual | EnableWorldBounds(rect) |
| **Shake** | Manual | Perlin noise / impulse | Manual | Manual (via offset) | Manual | Oscillating decay |
| **Coord convert** | Transform:inverseTransformPoint | toWorldCoords/toCameraCoords | Viewport addition | Viewport transform | Manual math | ScreenToWorld/WorldToScreen |
| **Zoom** | scale() | zoom_to(sx,sy) | None | Vector2 zoom | View size change | Zoom property |
| **Parallax** | Manual | camera_attach(px,py) | Manual | ParallaxBackground | Manual | GetViewMatrix(factor) |

### Key takeaways

1. **LOVE** provides only primitives (push/pop/translate/scale). Camera is a
   user-space pattern. Flexible but boilerplatey. Our engine is currently at
   this level.

2. **STALKER-X** (LOVE library by a327ex) is the most user-friendly: 6 named
   follow presets (LOCKON, PLATFORMER, TOPDOWN, TOPDOWN_TIGHT,
   SCREEN_BY_SCREEN, NO_DEADZONE) that encode genre-specific camera wisdom.
   Perlin-noise-based shake with configurable axes and exponential decay.

3. **high_impact** is the most pragmatic C implementation: a small struct with
   speed, deadzone, look-ahead, and a `snap_to_platform` flag for platformers.
   Minimum velocity threshold prevents sub-pixel jitter.

4. **Godot Camera2D** is the most integrated: camera follows its parent node
   automatically, drag margins as 0-1 fractions of viewport (resolution
   independent), `limit_smoothed` for gentle stops at world boundaries.

5. **GameMaker** separates camera (what to show) from viewport (where to show
   it). Border-based deadzone doubles as follow margin. Linear speed feels
   less smooth than lerp.

6. **Nez** separates Camera, FollowCamera, and CameraShake into composable ECS
   components. Oscillating-decay shake (`intensity *= -degradation` each
   frame). `ScaledCameraBounds` mode makes deadzones zoom-aware.

## Current engine state

Our transform system (`renderer.cc`) provides:

- `push()` / `pop()` transform stack (128 deep)
- `translate(x, y)`, `rotate(angle)`, `scale(sx, sy)` as incremental
  left-multiplied matrix ops
- Orthographic projection per viewport: `Ortho(0, w, 0, h)` where origin is
  top-left and Y increases downward
- Canvas system with separate FBO and viewport, transforms persist across
  canvas switches

A camera today requires manual Lua code:

```lua
function draw()
  G.graphics.push()
  G.graphics.translate(-cam_x + screen_w/2, -cam_y + screen_h/2)
  -- draw world --
  G.graphics.pop()
  -- draw HUD (no transform) --
end
```

This works but lacks smoothing, bounds, shake, zoom, and coordinate conversion.

## Proposed design

### Where to implement

**C++ with Lua bindings** (like our physics, sound, and collision systems).
Reasons:

- Camera update runs every frame and benefits from being part of the engine
  tick
- Coordinate conversion needs access to the projection/transform matrices
- Shake needs a good noise source (we already have PCG)
- Keeps Lua scripts focused on gameplay, not camera math

The camera would live alongside the renderer and be exposed as `G.camera`.

### Data model

```cpp
struct Camera {
  FVec2 position;           // World position (center of view)
  float zoom = 1.0f;        // Zoom factor (>1 = zoom in)
  float rotation = 0.0f;    // Rotation in radians

  // Follow
  FVec2 follow_target;      // Target position to follow
  bool following = false;    // Whether actively following
  FVec2 lerp = {1.0f, 1.0f}; // Per-axis smoothing (0 = frozen, 1 = instant)

  // Deadzone (fraction of viewport, 0-1)
  FVec2 deadzone;           // Half-size of deadzone rectangle
  bool deadzone_enabled = false;

  // Bounds
  FVec2 bounds_start;       // Top-left corner of world bounds
  FVec2 bounds_size;        // Width and height of world bounds
  bool bounds_enabled = false;

  // Shake
  float shake_intensity = 0.0f;
  float shake_duration = 0.0f;
  float shake_timer = 0.0f;
  float shake_frequency = 8.0f;  // oscillations per second
  FVec2 shake_offset;            // current frame offset
};
```

### Lua API

```lua
-- Set / get position
G.camera.set(x, y)
G.camera.get()  --> x, y
G.camera.move(dx, dy)

-- Zoom and rotation
G.camera.set_zoom(z)
G.camera.get_zoom()  --> z
G.camera.set_rotation(angle)
G.camera.get_rotation()  --> angle

-- Follow a target (called every frame with target's position)
G.camera.follow(x, y)
G.camera.set_lerp(lx, ly)   -- smoothing per axis (0-1, default 1 = instant)
G.camera.unfollow()

-- Dead zone (fraction of viewport, Godot-style)
G.camera.set_deadzone(half_w, half_h)  -- e.g. 0.1, 0.15
G.camera.clear_deadzone()

-- World bounds
G.camera.set_bounds(x, y, w, h)
G.camera.clear_bounds()

-- Shake
G.camera.shake(intensity, duration)
G.camera.shake(intensity, duration, frequency)

-- Coordinate conversion
G.camera.to_world(screen_x, screen_y)  --> world_x, world_y
G.camera.to_screen(world_x, world_y)   --> screen_x, screen_y
G.camera.mouse_world()                  --> world_x, world_y

-- Rendering integration
G.camera.attach()   -- push transform (call before drawing world)
G.camera.detach()   -- pop transform (call before drawing HUD)

-- Parallax (optional multiplier for attach)
G.camera.attach(parallax_x, parallax_y)
```

### Usage example

```lua
local g = {}

function g.init()
  G.camera.set_lerp(0.08, 0.08)
  G.camera.set_deadzone(0.1, 0.15)
  G.camera.set_bounds(0, 0, world_w, world_h)
end

function g.update(t, dt)
  G.camera.follow(player.x, player.y)
  -- shake on hit:
  if player.took_damage then
    G.camera.shake(6, 0.3)
  end
end

function g.draw()
  G.camera.attach()
    -- draw world, enemies, tiles, etc.
  G.camera.detach()

  -- draw HUD (screen space, unaffected by camera)
  G.graphics.draw_text(font, "HP: " .. player.hp, 10, 10)
end

return g
```

### Camera update logic

Called once per frame by the engine (after `update`, before `draw`):

```
1. If following:
   a. If deadzone enabled:
      - Only move camera when target exits deadzone rectangle
      - Push deadzone boundary to target position
   b. Lerp camera position toward follow_target:
      camera.x += (target.x - camera.x) * lerp.x
      camera.y += (target.y - camera.y) * lerp.y
      (framerate-independent: use 1 - (1-lerp)^dt, not lerp*dt)

2. If bounds enabled:
   - Clamp so viewport stays within bounds
   - Account for zoom (visible area = viewport_size / zoom)

3. If shaking:
   - Decrement timer
   - Generate offset using sine wave at frequency, scaled by
     intensity * (remaining / duration) for linear decay
   - Store in shake_offset (applied during attach, not to position)

4. Compute view matrix for attach():
   translate(viewport_w/2, viewport_h/2)   -- center of screen
   * scale(zoom, zoom)
   * rotate(rotation)
   * translate(-position.x, -position.y)   -- world offset
   * translate(-shake_offset.x, -shake_offset.y)
```

### Coordinate conversion

`to_world(sx, sy)`: apply the inverse of the view matrix to a screen point.
Since our matrix is a composition of simple transforms, the inverse is:

```
translate(position + shake_offset)
* rotate(-rotation)
* scale(1/zoom, 1/zoom)
* translate(-viewport_w/2, -viewport_h/2)
```

`to_screen(wx, wy)`: apply the view matrix directly.

`mouse_world()`: shorthand for `to_world(G.input.mouse_position())`.

### Framerate-independent lerp

Naive `pos += (target - pos) * lerp` is framerate-dependent. The correct
formula is:

```
pos += (target - pos) * (1 - pow(1 - lerp, dt * 60))
```

Where `lerp` is tuned assuming 60fps. At 60fps this equals the naive version.
At 30fps it moves more per frame to compensate. This is what Anchor/Nez use.

### Parallax support

`attach(px, py)` applies the camera transform with position scaled by the
parallax factor:

```
translate(viewport_w/2, viewport_h/2)
* scale(zoom, zoom)
* rotate(rotation)
* translate(-position.x * px, -position.y * py)
```

Usage:

```lua
-- Far background (moves slowly)
G.camera.attach(0.3, 0.3)
  draw_clouds()
G.camera.detach()

-- Near background
G.camera.attach(0.7, 0.7)
  draw_trees()
G.camera.detach()

-- Main layer
G.camera.attach()  -- defaults to 1.0, 1.0
  draw_world()
G.camera.detach()
```

### Deadzone behavior

The deadzone is a rectangle centered on the camera, sized as a fraction of the
viewport. When the follow target is inside the deadzone, the camera does not
move. When the target crosses the edge, the camera moves just enough to keep
the target at the deadzone boundary, then lerps as usual.

Deadzone fractions (like Godot's drag margins) are resolution-independent and
zoom-aware: the actual pixel size = `deadzone * viewport_size / zoom`.

Preset helpers could be added later:

```lua
G.camera.set_deadzone_preset("platformer")  -- wide horizontal, tight vertical
G.camera.set_deadzone_preset("topdown")     -- square
G.camera.set_deadzone_preset("lockon")      -- tiny (nearly centered)
```

### Shake design

Using **sine-wave with linear decay** rather than Perlin noise. Simpler, no
noise table needed, and gives a predictable oscillation that looks good for
impact effects.

```
offset.x = sin(timer * frequency * 2pi) * intensity * (remaining / duration)
offset.y = cos(timer * frequency * 2pi * 1.3) * intensity * (remaining / duration)
```

The 1.3 multiplier on Y frequency prevents X and Y from being in phase,
creating a more organic wobble. The offset is applied during `attach()` only,
so the logical camera position stays clean.

Multiple shakes: new `shake()` call replaces the current one if stronger, or
is ignored if weaker. No accumulation (prevents runaway shake).

### What this does NOT include

- **Multiple cameras / split screen**: Single global camera. Could be extended
  later but not needed for most 2D games.
- **Camera stacking / blending**: No transition blending between cameras.
- **Look-ahead**: Could be added as a follow option later (track target
  velocity and offset toward movement direction).
- **Snap-to-platform**: high_impact's platformer-specific feature. Can be
  emulated with per-axis lerp (fast horizontal, slow vertical + snap on land).

## Implementation plan

### Phase 1: Core camera
1. Create `camera.h` / `camera.cc` with the Camera struct.
2. `Update(float dt)` method: follow logic with framerate-independent lerp,
   bounds clamping.
3. `GetViewMatrix()` returns the composed FMat4x4.
4. `ToWorld(FVec2 screen)` / `ToScreen(FVec2 world)` conversion.
5. Lua bindings in `lua_graphics.cc` (or a new `lua_camera.cc`):
   `set`, `get`, `follow`, `set_lerp`, `unfollow`, `set_bounds`,
   `clear_bounds`, `attach`, `detach`, `to_world`, `to_screen`,
   `mouse_world`.
6. Engine calls `camera.Update(dt)` each frame after `update()`.

### Phase 2: Shake and zoom
1. Add shake with sine-wave decay.
2. Add zoom and rotation support to the view matrix and coordinate conversion.
3. Update bounds clamping to account for zoom.

### Phase 3: Deadzone and parallax
1. Deadzone logic in the follow update.
2. Parallax factor parameter on `attach()`.
3. Optional: named deadzone presets.
