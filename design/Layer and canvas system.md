# Layer and Canvas System

This document describes the current framebuffer architecture, compares canvas/render-target approaches across 2D engines, identifies issues and missing capabilities, and proposes a layer and canvas system for the engine.

## Current framebuffer architecture

The engine has a two-pass rendering pipeline hardcoded in `BatchRenderer::Render()` (`renderer.cc:300–549`):

```
Pass 1 (pre_pass):
  Bind MSAA FBO (render_target_)
  Clear to black
  Iterate command buffer → build vertex/index arrays
  Batch draw calls by shader/texture/transform
  glDrawElementsInstanced into MSAA texture

Resolve:
  glBlitFramebuffer(render_target_ → downsampled_target_)
  MSAA → non-MSAA via GL_NEAREST

Pass 2 (post_pass):
  Bind screen FBO (0)
  Draw fullscreen quad with downsampled_texture_
  post_pass shader samples screen_texture
```

### OpenGL objects

| Object | Type | Purpose |
|---|---|---|
| `render_target_` | FBO | MSAA render target |
| `render_texture_` | `GL_TEXTURE_2D_MULTISAMPLE` | MSAA color attachment |
| `downsampled_target_` | FBO | Resolved non-MSAA target |
| `downsampled_texture_` | `GL_TEXTURE_2D` | Resolved color attachment |
| `depth_buffer_` | Renderbuffer `GL_DEPTH24_STENCIL8` | Attached to downsampled FBO (unused) |
| `noop_texture_` | `GL_TEXTURE_2D` | 32x32 white pixels for untextured draws |
| `screen_quad_vao_/vbo_` | VAO/VBO | Fullscreen NDC quad for post-pass |

Viewport resizes delete and recreate both FBOs (`SetViewport`, `renderer.cc:217–229`).

### What exists today

The Lua API has three stubbed-out functions (`lua_graphics.cc:519–538`):

```cpp
{"new_canvas",  "Unimplemented.", {}, {}, [](lua_State* state) { LUA_ERROR(state, "Unimplemented"); return 0; }},
{"set_canvas",  "Unimplemented.", {}, {}, [](lua_State* state) { LUA_ERROR(state, "Unimplemented"); return 0; }},
{"draw_canvas", "Unimplemented.", {}, {}, [](lua_State* state) { LUA_ERROR(state, "Unimplemented"); return 0; }},
```

`TASKS.md:65–70` describes the planned API:
- `G.graphics.new_canvas(width, height)` — create off-screen FBO with color texture
- `G.graphics.set_canvas(canvas)` — redirect drawing; `set_canvas()` resets to screen
- `G.graphics.draw_canvas(canvas, x, y [, angle])` — draw canvas as textured quad
- Use cases: post-processing shaders, pixel art scaling, UI layers, minimap
- Should respect existing transform stack; `take_screenshot` should work with active canvas

The `Graphics system.md` design doc also notes this as a TODO.

### Custom shaders today

Custom fragment shaders already work through the `effect()` function pattern (`shaders.cc:94–111`). Users call `graphics.new_shader()`, `graphics.attach_shader()`, and `graphics.send_uniform()`. Shaders are linked with the shared `pre_pass.vert` vertex shader. The batch renderer inserts `kSetShader` commands to switch programs mid-frame. This system composes naturally with canvases: a canvas can be drawn with a shader applied.

## Issues with the current architecture

### Issue 1: No way to render to an offscreen target

All draw calls go to the single hardcoded MSAA FBO. There is no mechanism to redirect rendering to a user-created texture. This prevents post-processing chains, UI layering, pixel art scaling, lighting compositing, and many other common 2D techniques.

### Issue 2: Depth/stencil buffer is created but never used

`renderer.cc:203–209` creates a `GL_DEPTH24_STENCIL8` renderbuffer and attaches it to `downsampled_target_`, but `Render()` disables depth testing (`glDisable(GL_DEPTH_TEST)`, line 309) and never touches the stencil buffer. This wastes GPU memory.

### Issue 3: Post-processing is single-pass only

The post-pass renders `downsampled_texture_` to the screen with a single shader. Multi-pass effects (blur, bloom, color grading chains) require ping-ponging between framebuffers, which is impossible without a canvas system.

### Issue 4: Screenshot reads from the wrong framebuffer

`TakeScreenshot` (`renderer.cc:552–575`) calls `glReadnPixels` without binding a specific framebuffer first. After `Render()`, framebuffer 0 is bound, so it reads the post-pass output. If a canvas were active, this would read the wrong target. The function should explicitly bind the intended source.

### Issue 5: Texture unit exhaustion

Each `LoadTexture`/`LoadFontTexture` call permanently occupies a texture unit (up to 256). Canvases would need texture units too. The current approach of `glActiveTexture(GL_TEXTURE0 + index)` with monotonically increasing indices will eventually run out. Most GPUs support only 16–32 texture units in a fragment shader. The engine should bind textures on demand rather than permanently occupying units.

### Issue 6: Hardcoded MSAA on all rendering

MSAA is always on and always at `GL_MAX_SAMPLES`. Some use cases (pixel art, intentionally aliased rendering) want no anti-aliasing. Canvases should let users opt out.

## Comparison with other engines

### Love2D Canvas

Love2D's Canvas is the closest model for this engine. It wraps OpenGL framebuffers with a simple API:

```lua
local canvas = love.graphics.newCanvas(800, 600)
love.graphics.setCanvas(canvas)      -- redirect
love.graphics.clear(0, 0, 0, 0)
drawScene()
love.graphics.setCanvas()            -- back to screen
love.graphics.draw(canvas, 0, 0)     -- draw as texture
```

| Feature | Love2D | This Engine (proposed) |
|---|---|---|
| Creation | `newCanvas(w, h, {format, msaa, ...})` | `new_canvas(w, h)` |
| Activation | `setCanvas(c)` / `setCanvas()` | `set_canvas(c)` / `set_canvas()` |
| Drawing as texture | `love.graphics.draw(canvas, x, y)` | `draw_canvas(c, x, y)` |
| Scoped helper | `canvas:renderTo(fn)` | Not planned (easy to add) |
| MRT | Yes, up to 4 targets | Not needed initially |
| MSAA per canvas | Yes | Not needed initially |
| Pixel formats | 20+ including HDR | RGBA8 only |
| Stencil/depth | Optional, explicit | Not needed initially |
| Mipmap support | Yes | Not needed initially |

**Key Love2D gotcha — premultiplied alpha**: Drawing semi-transparent content to a canvas and then drawing that canvas to the screen causes double-darkening unless the blend mode is switched to `("alpha", "premultiplied")` when drawing the canvas. This is the single most common source of canvas-related bugs in Love2D. The engine should handle this correctly from the start.

### Raylib RenderTexture2D

Raylib wraps framebuffers with a Begin/End scoping pattern:

```c
RenderTexture2D target = LoadRenderTexture(800, 600);
BeginTextureMode(target);
  ClearBackground(BLACK);
  DrawCircle(400, 300, 100, RED);
EndTextureMode();
// Must flip Y when drawing
DrawTextureRec(target.texture, (Rectangle){0,0,800,-600}, (Vector2){0,0}, WHITE);
```

Raylib is simpler but more limited: no MSAA on render textures, no pixel format choice, no MRT. A notable annoyance is the Y-flip requirement — OpenGL framebuffer textures have flipped Y coordinates relative to screen rendering. The engine should handle this transparently.

### Godot SubViewport / CanvasLayer

Godot separates two concepts:
- **CanvasLayer**: purely ordering — assigns a layer index for draw order. No framebuffer involved.
- **SubViewport**: a full render target node in the scene tree. Can have its own MSAA settings, clear mode, update mode, and child scene.

The SubViewport is far heavier than what this engine needs. But the CanvasLayer concept (draw ordering without framebuffers) is useful and could be a separate, lighter feature.

### MonoGame RenderTarget2D

MonoGame has the most explicit API: full control over surface format, depth format, MSAA count, and preserve/discard semantics. It extends `Texture2D`, so render targets can be used anywhere a texture is expected. This is the gold standard for feature completeness but is more verbose than needed for a Lua-scripted 2D engine.

### Pixi.js RenderTexture

Pixi.js has an interesting `cacheAsTexture()` method that automatically renders a display object's children to a texture and reuses it until invalidated. This is useful for static UI or backgrounds. The engine could add something similar as a higher-level convenience.

### Summary matrix

| Capability | Love2D | Raylib | Godot | MonoGame | Proposed |
|---|---|---|---|---|---|
| Create render target | `newCanvas(w,h)` | `LoadRenderTexture(w,h)` | SubViewport node | `new RenderTarget2D(...)` | `new_canvas(w,h)` |
| Redirect drawing | `setCanvas(c)` | `BeginTextureMode(t)` | Scene tree | `SetRenderTarget(t)` | `set_canvas(c)` |
| Draw as texture | `draw(canvas)` | `DrawTexture(t.texture)` | ViewportTexture | `SpriteBatch.Draw(t)` | `draw_canvas(c,x,y)` |
| Y-flip needed | No | Yes | No | No | No (handled internally) |
| Premultiplied alpha | Manual | N/A | Automatic | N/A | Automatic |
| Scoped helper | `renderTo(fn)` | Begin/End | N/A | N/A | Proposed |
| Nesting | Yes | Yes | Yes | Yes | Yes |
| Clear control | `love.graphics.clear()` | `ClearBackground()` | Property | Property | `clear()` while active |
| Shader compositing | `setShader` + `draw` | `BeginShaderMode` | Material | SpriteBatch + Effect | `attach_shader` + `draw_canvas` |

## Proposed design

### Core API

```lua
-- Create an offscreen canvas. Returns a canvas userdata object.
local c = G.graphics.new_canvas(width, height)

-- Redirect all subsequent drawing to the canvas.
-- Calling with no arguments resets to the screen.
G.graphics.set_canvas(c)
G.graphics.set_canvas()

-- Draw the canvas contents as a textured quad at (x, y) with optional rotation.
G.graphics.draw_canvas(c, x, y)
G.graphics.draw_canvas(c, x, y, angle)

-- Query canvas dimensions.
local w, h = c:dimensions()

-- Clear the active canvas (or screen if no canvas is set).
-- This already exists as G.graphics.clear().
G.graphics.clear()
```

### C++ data structure

```cpp
struct Canvas {
  GLuint fbo;           // Framebuffer object
  GLuint texture;       // Color attachment (GL_TEXTURE_2D)
  size_t texture_unit;  // Index in BatchRenderer::tex_
  int width, height;
};
```

Canvases are stored as Lua userdata with a metatable providing the `dimensions` method. The `Renderer` owns a `FixedArray<Canvas>` for lifecycle management.

### How set_canvas works

When `set_canvas(c)` is called mid-frame:

1. Insert a new command `kSetCanvas` into the command buffer with the canvas FBO handle and dimensions.
2. During `Render()`, when the iterator hits `kSetCanvas`:
   - Flush pending geometry (draw everything queued so far to the current target).
   - Bind the canvas FBO.
   - Set the viewport to the canvas dimensions.
   - Update the projection matrix to `Ortho(0, canvas.width, 0, canvas.height)`.
3. All subsequent draw commands render into the canvas FBO.

When `set_canvas()` is called (no arguments):

1. Insert `kSetCanvas` with the main `render_target_` FBO and the screen viewport dimensions.
2. During `Render()`, restore the original FBO, viewport, and projection.

This keeps canvas switching within the existing command-buffer architecture. The batch renderer already flushes on state changes (texture, shader, transform), so adding FBO changes is a natural extension.

### How draw_canvas works

A canvas texture is registered in the `tex_` array like any other texture. `draw_canvas(c, x, y, angle)`:

1. Calls `SetActiveTexture(c.texture_unit)`.
2. Calls `PushQuad` with position `(x, y)` to `(x + c.width, y + c.height)` and UVs `(0, 1)` to `(1, 0)` (Y-flipped to correct OpenGL's bottom-up texture origin).

This makes canvas drawing go through the same batch pipeline as sprites and images, so it automatically respects the transform stack, color, and active shader.

### Premultiplied alpha handling

When drawing to a canvas, the standard `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` blend mode produces premultiplied-alpha content in the canvas texture. When the canvas is later drawn to the screen (or another canvas), the blend mode must change to `GL_ONE, GL_ONE_MINUS_SRC_ALPHA` to avoid double-darkening.

The engine should handle this automatically: `draw_canvas` switches to premultiplied-alpha blending for the canvas quad, then restores the previous blend mode. This avoids the Love2D footgun entirely.

One way to implement this: add a `kSetBlendMode` command. When `draw_canvas` is called, emit `kSetBlendMode(premultiplied)`, then the quad, then `kSetBlendMode(normal)`. The `Render()` loop handles this by calling `glBlendFunc` on state change.

### Nesting

Canvases can be nested: `set_canvas(a)` then `set_canvas(b)` draws into `b`. Calling `set_canvas()` returns to the screen, not to `a`. This matches Love2D behavior. A stack-based approach where `set_canvas()` pops back to the previous target is also viable but adds complexity. The simpler "always reset to screen" approach is recommended for the initial implementation.

### Canvas and the MSAA pipeline

User canvases should be non-MSAA (`GL_TEXTURE_2D`, not `GL_TEXTURE_2D_MULTISAMPLE`). Reasons:

1. MSAA textures cannot be directly sampled in shaders — they must be resolved first, which adds complexity.
2. Many canvas use cases (pixel art scaling, lighting masks) actively do not want anti-aliasing.
3. The main scene can still render to the MSAA FBO; canvases are a separate concern.

If a user wants MSAA on a canvas, they can render at 2x resolution and downscale — this is simpler and more portable than per-canvas MSAA.

### Canvas creation (OpenGL)

```cpp
Canvas Renderer::CreateCanvas(int width, int height) {
  Canvas c;
  c.width = width;
  c.height = height;

  glGenFramebuffers(1, &c.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, c.fbo);

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, tex, 0);

  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "Canvas framebuffer incomplete");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Register texture in the BatchRenderer's texture array.
  c.texture_unit = renderer_->RegisterTexture(tex);
  c.texture = tex;
  return c;
}
```

Uses `GL_CLAMP_TO_EDGE` (not `GL_REPEAT`) to avoid edge bleeding when drawing canvases.

## Composing with existing features

### Canvas + custom shaders (post-processing)

The most important composition. Render the scene to a canvas, then draw the canvas with a shader applied:

```lua
local scene = G.graphics.new_canvas(800, 600)

function G.draw()
  -- Render scene to canvas.
  G.graphics.set_canvas(scene)
  G.graphics.clear()
  draw_game_world()
  G.graphics.set_canvas()

  -- Draw canvas with CRT shader.
  G.graphics.attach_shader("crt")
  G.graphics.send_uniform("iResolution", G.math.v2(800, 600))
  G.graphics.draw_canvas(scene, 0, 0)
  G.graphics.attach_shader()  -- reset shader
end
```

This works because `draw_canvas` emits a textured quad through the batch renderer, and `attach_shader` sets the active shader via `kSetShader`. The CRT shader's `effect()` function receives the canvas texture as `tex`.

### Canvas + transform stack

Canvases respect the existing transform stack. Drawing inside a canvas uses the canvas's own coordinate system. Drawing a canvas quad to the screen uses the screen's transform stack:

```lua
G.graphics.set_canvas(minimap)
G.graphics.clear()
G.graphics.push()
G.graphics.scale(0.25, 0.25)   -- scale within the minimap
draw_world()
G.graphics.pop()
G.graphics.set_canvas()

-- Draw minimap in corner with rotation.
G.graphics.draw_canvas(minimap, 700, 10, 0)
```

### Canvas + canvas (multi-pass)

Canvases can be composed with each other for multi-pass effects like bloom:

```lua
local scene    = G.graphics.new_canvas(800, 600)
local bright   = G.graphics.new_canvas(400, 300)
local blur_h   = G.graphics.new_canvas(400, 300)
local blur_v   = G.graphics.new_canvas(400, 300)

function G.draw()
  -- Pass 1: render scene.
  G.graphics.set_canvas(scene)
  G.graphics.clear()
  draw_world()
  G.graphics.set_canvas()

  -- Pass 2: extract bright pixels (half resolution).
  G.graphics.set_canvas(bright)
  G.graphics.clear()
  G.graphics.attach_shader("threshold")
  G.graphics.draw_canvas(scene, 0, 0)
  G.graphics.attach_shader()
  G.graphics.set_canvas()

  -- Pass 3: horizontal blur.
  G.graphics.set_canvas(blur_h)
  G.graphics.clear()
  G.graphics.attach_shader("blur_horizontal")
  G.graphics.draw_canvas(bright, 0, 0)
  G.graphics.attach_shader()
  G.graphics.set_canvas()

  -- Pass 4: vertical blur.
  G.graphics.set_canvas(blur_v)
  G.graphics.clear()
  G.graphics.attach_shader("blur_vertical")
  G.graphics.draw_canvas(blur_h, 0, 0)
  G.graphics.attach_shader()
  G.graphics.set_canvas()

  -- Composite: scene + bloom.
  G.graphics.draw_canvas(scene, 0, 0)
  G.graphics.set_color(1, 1, 1, 0.5)
  G.graphics.draw_canvas(blur_v, 0, 0)  -- additive would be better
  G.graphics.set_color(1, 1, 1, 1)
end
```

### Canvas + text

Text rendering works inside canvases with no changes. `draw_text` emits textured quads through the batch renderer just like everything else. A canvas can hold pre-rendered text for a UI overlay:

```lua
local ui = G.graphics.new_canvas(800, 600)

function update_ui()
  G.graphics.set_canvas(ui)
  G.graphics.clear()
  G.graphics.draw_text("myfont.ttf", 24, "Score: " .. score, 10, 10)
  G.graphics.draw_text("myfont.ttf", 16, "HP: " .. hp, 10, 40)
  G.graphics.set_canvas()
end

function G.draw()
  draw_world()
  G.graphics.draw_canvas(ui, 0, 0)  -- overlay UI
end
```

### Canvas + screenshots

`take_screenshot` should capture whatever is currently visible. If a canvas is active, it should read from the canvas FBO. If no canvas is active, it should read from the screen. The implementation needs to track the currently bound FBO and pass it to `TakeScreenshot`.

## Proposed additional features

### 1. Blend mode control

**Problem**: The engine only supports `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` blending. Canvas compositing and lighting effects need other modes (additive, multiply, premultiplied alpha).

**Proposal**:

```lua
G.graphics.set_blend_mode("alpha")         -- default: src_alpha, 1-src_alpha
G.graphics.set_blend_mode("add")           -- additive: src_alpha, one
G.graphics.set_blend_mode("multiply")      -- multiply: dst_color, zero
G.graphics.set_blend_mode("replace")       -- no blending: one, zero
```

Implementation: add a `kSetBlendMode` command type. During `Render()`, call `glBlendFunc` with the appropriate parameters. The `draw_canvas` function automatically uses premultiplied-alpha blending without the user needing to set it.

Additive blending is essential for lighting and particle effects. Without it, the bloom example above cannot correctly composite the blur pass.

### 2. Canvas filtering mode

**Problem**: Canvases default to `GL_LINEAR` filtering. For pixel art scaling (a primary use case), `GL_NEAREST` is required to preserve sharp pixels.

**Proposal**: Add a filter parameter to `new_canvas`:

```lua
local pixel_canvas = G.graphics.new_canvas(160, 120, { filter = "nearest" })
local smooth_canvas = G.graphics.new_canvas(800, 600, { filter = "linear" })  -- default
```

Implementation: set `GL_TEXTURE_MIN_FILTER` and `GL_TEXTURE_MAG_FILTER` on the canvas texture at creation time. Alternatively, add a `canvas:set_filter(mode)` method to change it after creation.

### 3. Pixel art scaling pipeline

**Problem**: Pixel art games want to render at a low resolution (e.g. 320x180) and scale up to the window size with nearest-neighbor filtering. Currently there is no clean way to do this.

**Proposal**: This composes naturally from canvases + filtering:

```lua
local GAME_W, GAME_H = 320, 180
local game_canvas = G.graphics.new_canvas(GAME_W, GAME_H, { filter = "nearest" })

function G.draw()
  G.graphics.set_canvas(game_canvas)
  G.graphics.clear()
  draw_game()  -- all game rendering at 320x180
  G.graphics.set_canvas()

  -- Scale up to window size.
  local ww, wh = G.window.dimensions()
  local scale = math.min(ww / GAME_W, wh / GAME_H)
  local ox = (ww - GAME_W * scale) / 2
  local oy = (wh - GAME_H * scale) / 2
  G.graphics.push()
  G.graphics.translate(ox, oy)
  G.graphics.scale(scale, scale)
  G.graphics.draw_canvas(game_canvas, 0, 0)
  G.graphics.pop()
end
```

No special API needed — it falls out of the canvas + transform stack + filtering composition.

### 4. Scoped canvas helper

**Problem**: Forgetting to call `set_canvas()` to reset is a common bug (Love2D, MonoGame, and Raylib all document this).

**Proposal**: Add a `canvas:render_to(fn)` method that automatically scopes the canvas activation:

```lua
scene:render_to(function()
  G.graphics.clear()
  draw_world()
end)
-- canvas is automatically reset to previous target here
```

Implementation in Lua (can be done as a Lua-side wrapper rather than C++):

```lua
function Canvas:render_to(fn)
  G.graphics.set_canvas(self)
  fn()
  G.graphics.set_canvas()
end
```

Or as a C++ method on the canvas userdata if we want it built-in.

### 5. Canvas clear with color

**Problem**: `G.graphics.clear()` always clears to black with zero alpha. Canvases often need to be cleared to transparent (`0, 0, 0, 0`) or a specific color.

**Proposal**: Accept optional color arguments:

```lua
G.graphics.clear()                    -- black, zero alpha (current behavior)
G.graphics.clear(0.1, 0.1, 0.2, 1)   -- dark blue, fully opaque
```

Implementation: `glClearColor(r, g, b, a)` before `glClear(GL_COLOR_BUFFER_BIT)`.

### 6. Draw canvas with dimensions (scaling)

**Problem**: `draw_canvas(c, x, y)` always draws at 1:1 size. Scaling requires wrapping in push/scale/pop.

**Proposal**: Add optional width/height parameters:

```lua
G.graphics.draw_canvas(c, x, y)              -- 1:1 size
G.graphics.draw_canvas(c, x, y, angle)       -- with rotation
G.graphics.draw_canvas(c, x, y, angle, w, h) -- with rotation and scaling
```

When `w` and `h` are provided, the quad is sized to `(w, h)` instead of `(c.width, c.height)`. This avoids the push/scale/pop boilerplate for the common case of drawing a canvas at a different size.

### 7. Stencil buffer access

**Problem**: The engine creates a `GL_DEPTH24_STENCIL8` renderbuffer but never uses it. Stencil buffers enable masking effects (circular health bars, shaped UI panels, fog of war cutouts).

**Proposal** (future, not for initial canvas implementation):

```lua
G.graphics.stencil(function()
  G.graphics.draw_circle(400, 300, 100)  -- shape that defines the mask
end)
G.graphics.set_stencil_test("greater", 0)
draw_scene()  -- only draws where stencil was written
G.graphics.set_stencil_test()  -- disable
```

This mirrors Love2D's stencil API. Implementation: use `glStencilFunc`, `glStencilOp`, `glEnable(GL_STENCIL_TEST)`. The depth/stencil renderbuffer already exists on `downsampled_target_`; it just needs to be attached to canvas FBOs too when stencil is requested.

## Implementation order

1. **Core canvas API**: `new_canvas`, `set_canvas`, `draw_canvas` with correct Y-flip and premultiplied-alpha blending. This is the foundation everything else builds on.
2. **Canvas clear with color**: Small addition to `clear()`, needed for canvas workflows.
3. **Blend mode control**: `set_blend_mode` with alpha/add/multiply/replace. Required for correct canvas compositing and lighting.
4. **Canvas filtering**: `filter` parameter on `new_canvas`. Required for pixel art scaling.
5. **Scoped helper**: `render_to` method. Quality-of-life improvement.
6. **Draw with dimensions**: Width/height parameters on `draw_canvas`. Quality-of-life improvement.
7. **Stencil buffer**: Future feature, independent of the rest.

## Summary

The engine's rendering pipeline is well-structured for adding canvas support. The command-buffer architecture means canvas switching is just another state-change command alongside texture/shader/transform changes. The main implementation challenges are:

1. Handling the Y-flip transparently (UV inversion on canvas quads).
2. Getting premultiplied alpha blending right automatically.
3. Not exhausting texture units (needs a binding strategy change).

The proposed API follows Love2D closely — it is the best-proven design for a Lua-scripted 2D engine — while avoiding Love2D's main footgun (manual premultiplied-alpha blend mode) by handling it automatically in `draw_canvas`.
