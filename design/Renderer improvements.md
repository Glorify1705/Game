---
status: partially-implemented
tags: [renderer, graphics]
---

# Renderer Improvements

## Background: Scissor and Stencil

Both are GPU-level mechanisms for **preventing pixels from being drawn**. They
happen after the fragment shader runs but before the pixel lands in the
framebuffer, so rejected pixels cost almost nothing.

### Scissor Test

The simplest clipping mechanism. You give the GPU an axis-aligned rectangle in
screen coordinates. Every pixel outside that rectangle is discarded -- no
exceptions, no per-pixel logic, just a hardware bounds check.

Think of it as cropping the entire render output to a rectangle. It applies to
everything: `clear`, `draw_rect`, `draw_sprite`, text -- all of it. It does not
interact with the camera or transform stack; it's always in screen pixels.

Typical uses: scroll regions in a UI, chat windows, inventory panels, HUD
viewports, split-screen. Anything where you want to guarantee "nothing draws
outside this box."

```
set_scissor(100, 100, 400, 300)   -- only this 400x300 rect receives draws
draw_lots_of_stuff()              -- anything outside the rect is silently dropped
clear_scissor()                   -- full screen again
```

### Stencil Buffer

A per-pixel integer buffer (usually 8 bits, so values 0-255) that sits alongside
the color buffer. It has no visual meaning -- it's scratch space. You use it in
two phases:

**Phase 1 -- Write.** Draw geometry that fills stencil values instead of (or in
addition to) color. For example, draw a circle and write `1` into every pixel
the circle covers. The color buffer is typically masked off during this step so
nothing visible happens.

**Phase 2 -- Test.** Set a comparison rule like "only draw where stencil == 1".
Now all subsequent draws are masked to exactly the shape you drew in phase 1.

The key difference from scissor: the mask shape can be **arbitrary geometry**,
not just an axis-aligned rectangle. Circles, polygons, text outlines, sprite
silhouettes -- anything you can draw can become a mask.

```
stencil_write("replace", 1):      -- phase 1: draw a circle into stencil
    draw_circle(x, y, radius)     --   every pixel in this circle gets stencil=1
set_stencil_test("equal", 1)      -- phase 2: only draw where stencil is 1
    draw_fog_overlay()            --   overlay only appears inside the circle
clear_stencil_test()              -- back to normal, everything draws
```

**Why not just use a texture as a mask?** You can, but stencil is:
- Free in VRAM (the buffer already exists -- we allocate it at startup)
- Per-pixel exact with no filtering artifacts
- Faster than a texture sample + discard in the fragment shader
- Composable: you can increment/decrement stencil values to nest masks or
  combine shapes with boolean operations (union, intersection, subtraction)

Typical uses:

| Use case | How |
|----------|-----|
| Fog of war | Write explored region into stencil, draw dark overlay only where stencil is 0 |
| Portal / window | Write portal rect into stencil, draw the "other side" scene only inside it |
| Minimap mask | Write circular mask, draw the minimap clipped to that circle |
| UI clipping (non-rect) | Write rounded-rect or arbitrary shape, clip child widgets to it |
| Mirror | Write mirror surface, draw reflected scene only within it |
| Spotlight reveal | Write light cone geometry, draw lit scene inside, dark outside |

### Our situation

We already create a `GL_DEPTH24_STENCIL8` renderbuffer (`renderer.cc:248`), so
the buffer exists and costs us memory regardless. We just never call the GL
functions that use it. Wiring it up is a small amount of engine code for a large
increase in what game scripts can express.

---

## Current State

The engine runs **OpenGL 4.6 Core Profile** with a command-queue batch renderer.
What we already have:

- Quad/triangle/line batching with automatic state-change grouping
- 5 blend modes (alpha, add, multiply, replace, premultiplied)
- Canvas (FBO) for off-screen rendering
- MSAA with resolve blit
- SDF text with outline support
- Transform stack (translate, rotate, scale)
- Hot-reloadable fragment shaders
- Depth/stencil renderbuffer **attached but never exposed**

What we don't have (but other engines do):

| Feature | Love2D | Raylib | Godot 2D | Defold | MonoGame |
|---------|--------|--------|----------|--------|----------|
| Stencil ops | yes | no | partial | yes | yes |
| Custom blend equations | no | yes | no | limited | yes |
| Post-processing chain | via canvas | yes | yes | yes | yes |
| Instanced rendering | yes | yes | yes | yes | yes |
| Scissor rect | yes | yes | yes | yes | yes |
| 2D lighting | community | no | built-in | sample | community |
| Color grading / LUT | no | no | partial | tutorial | no |
| GPU particles | no | no | yes | no | no |
| Texture arrays | no | no | internal | no | no |

---

## Proposed Improvements

Ordered roughly by effort (low to high) and grouped into tiers.

### Tier 1 -- Low Effort, High Value

#### 1.1 Stencil Buffer Operations

We already create a `GL_DEPTH24_STENCIL8` renderbuffer (`renderer.cc:248`) but
never call `glStencilFunc` / `glStencilOp`. Exposing stencil is almost free.

**Engine API:**

```cpp
// renderer.h
enum StencilAction { STENCIL_REPLACE, STENCIL_INCREMENT, STENCIL_DECREMENT, STENCIL_INVERT };
enum StencilCompare { COMPARE_ALWAYS, COMPARE_EQUAL, COMPARE_NOT_EQUAL,
                      COMPARE_LESS, COMPARE_LEQUAL, COMPARE_GREATER, COMPARE_GEQUAL };

void BeginStencilWrite(StencilAction action, int value);   // draw geometry into stencil
void EndStencilWrite();
void SetStencilTest(StencilCompare compare, int ref);      // subsequent draws pass/fail
void ClearStencilTest();
void ClearStencilBuffer();
```

**Lua API:**

```lua
G.graphics.stencil_write("replace", 1, function()
    G.graphics.draw_circle(x, y, radius)        -- writes 1 into stencil
end)
G.graphics.set_stencil_test("equal", 1)          -- only draw where stencil == 1
-- draw fog-of-war overlay, minimap mask, portal view, etc.
G.graphics.clear_stencil_test()
```

**Use cases:** fog of war, shaped masks, portal rendering, UI clipping, mirrors,
minimap viewports, spotlight reveal.

**Implementation notes:**
- Add stencil state to `BatchRenderer` command stream (new command types)
- Clear stencil buffer alongside color buffer in `Clear()`
- Flush batch before stencil state changes (like blend mode changes)
- Ensure canvas FBOs also get stencil attachment (currently they don't)

#### 1.2 Scissor Rect

Axis-aligned rectangular clipping. One GL call, no shader changes.

```cpp
void SetScissor(int x, int y, int w, int h);
void ClearScissor();
```

```lua
G.graphics.set_scissor(x, y, w, h)
-- draws here are clipped to the rectangle
G.graphics.clear_scissor()
```

**Use cases:** UI panels, scroll views, HUD regions, split-screen, debug
overlays. Scissors don't interact with the transform stack (screen-space only),
which matches Love2D's behavior and avoids confusion.

**Implementation notes:**
- New batch command `CMD_SET_SCISSOR` / `CMD_CLEAR_SCISSOR`
- Maps directly to `glScissor` / `glDisable(GL_SCISSOR_TEST)`

#### 1.3 Custom Blend Equations

Extend the blend system beyond the five presets to allow user-defined
`glBlendFuncSeparate` + `glBlendEquationSeparate`.

```lua
G.graphics.set_blend_mode("custom", {
    src_color = "src_alpha",
    dst_color = "one",
    src_alpha = "one",
    dst_alpha = "one_minus_src_alpha",
    equation_color = "add",
    equation_alpha = "add",
})
```

This lets users implement soft-light, overlay, screen, and other Photoshop-style
blend modes without a shader.

**Implementation notes:**
- Add a `BlendState` struct with src/dst/equation for color and alpha
- Existing five presets become named instances of `BlendState`
- One new batch command `CMD_SET_BLEND_STATE`

---

### Tier 2 -- Medium Effort, High Value

#### 2.1 Post-Processing Pipeline

We have canvases. What's missing is a convenient way to chain full-screen
passes with ping-pong buffers.

**Engine API:**

```cpp
// A post-processing pass: a shader + uniforms applied to a full-screen quad.
struct PostPass {
    ShaderProgram shader;
    // Uniforms set before draw
};

// Managed chain that handles ping-pong internally.
struct PostProcessChain {
    void AddPass(PostPass pass);
    void Apply(Canvas* source, Canvas* destination);
};
```

**Lua API:**

```lua
local blur_h = G.graphics.new_shader("blur_h.glsl")
local blur_v = G.graphics.new_shader("blur_v.glsl")
local bloom  = G.graphics.new_shader("bloom_combine.glsl")

-- In draw:
G.graphics.set_canvas(scene_canvas)
    draw_scene()
G.graphics.set_canvas()

G.graphics.apply_post_process(scene_canvas, {blur_h, blur_v, bloom})
G.graphics.draw_canvas(scene_canvas)
```

**Bundled effects (optional, as example shaders):**

| Effect | Passes | Notes |
|--------|--------|-------|
| Gaussian blur | 2 (H + V separable) | Kawase blur is cheaper for large radii |
| Bloom | 3 (threshold + blur + combine) | Downsample first for performance |
| CRT scanlines | 1 | Scanline + curvature + chromatic aberration |
| Vignette | 1 | Darken edges, radial gradient |
| Color grading / LUT | 1 | Sample a 3D LUT texture; near-zero cost |
| Screen transition | 1 | Dissolve/wipe/pixelate between two canvases |

**Implementation notes:**
- Internal pair of ping-pong canvases at screen resolution (lazy-allocated)
- `apply_post_process` flushes the batch, then for each shader: bind source as
  texture, draw full-screen quad into the other canvas, swap
- Expose `time`, `resolution`, `texel_size` as automatic uniforms

#### 2.2 Texture Compression (BC Formats)

The GPU can sample from pre-compressed textures directly -- no CPU-side
decompression. You compress offline in `game package`, store the compressed
blocks in the asset DB, and upload them via `glCompressedTexImage2D`. The GPU's
fixed-function texture unit decompresses blocks on the fly during sampling.

**Why bother for a 2D engine?**

- A 1024x1024 RGBA8 texture is 4 MB in VRAM. BC3 makes it 1 MB, BC1 makes it
  512 KB. A game with 64 atlases goes from 256 MB to 64 MB (BC3) or 32 MB
  (BC1). That matters on integrated GPUs and laptops.
- Compressed textures are *faster* to sample, not slower. A 64-byte cache line
  holds 16 texels of BC3 vs 4 texels of RGBA8. Better cache utilization = fewer
  misses.
- Load times drop proportionally -- less data to read from disk and upload.

**Which formats (all universally supported on desktop since GL 4.2):**

| Format | Bits/px | Ratio | Alpha | Use |
|--------|---------|-------|-------|-----|
| BC1 | 4 | 8:1 | 1-bit only | Opaque sprites, tilemaps, backgrounds |
| BC3 | 8 | 4:1 | Smooth 8-bit | General-purpose sprites with transparency |
| BC7 | 8 | 4:1 | High-quality | UI, painted art, anything where BC3 artifacts show |
| BC4 | 4 | 4:1 (vs R8) | Single channel | SDF font atlases, grayscale masks |

BC7 is the quality king (near-lossless for most content) but is ~50x slower to
compress than BC1/BC3. That's fine -- compression happens once at pack time.

**When NOT to compress:**

- Pixel art at native resolution. Block compression smears single-pixel color
  transitions in 4x4 blocks. Pixel art textures are small anyway.
- Color LUTs, palette textures, data textures -- anything needing exact values.
- Textures smaller than 4x4.

**Compression tools (ranked by integration ease):**

1. **stb_dxt** -- single header, BC1/BC3 only, decent quality, trivial to add
2. **ISPC Texture Compressor** (Intel, MIT) -- static lib, BC1-BC7, extremely
   fast BC7 via SIMD. Best quality-to-speed ratio.
3. **Basis Universal transcoder** -- single .cpp file. Compress to UASTC at
   pack time, transcode to native BC7 at load time (~1-3 ms per texture).
   Smaller on-disk size via Zstandard supercompression. Future-proofs for
   multi-platform.
4. **etcpak** -- small lib, very fast, used by Godot internally.

**Integration with our asset pipeline:**

Since we already use SQLite for assets, skip container formats (DDS, KTX).
Store compressed mip chains as blobs with metadata columns (format enum, width,
height, mip count, per-level offsets). The loader reads the blob and calls
`glCompressedTexImage2D` for each mip level -- minimal change to the existing
texture upload path.

**Mipmapping gotchas:**

- Generate mips from the *uncompressed* source, then compress each level
  independently. Never downsample compressed data.
- `glGenerateMipmap` does not work on compressed textures. Each level must be
  uploaded individually. The packer must store the full mip chain.
- Each mip must be a multiple of 4 pixels (the block size). Smaller mips get
  padded to one block automatically.

**Per-texture format selection in the packer:**

```
assets/
  sprites/       -> BC3 (default, has alpha)
  backgrounds/   -> BC1 (opaque, maximum compression)
  ui/            -> BC7 (sharp edges, thin lines)
  pixel-art/     -> none (uncompressed RGBA8)
  fonts/         -> BC4 (single-channel SDF atlas)
```

A sensible default is BC3 with opt-out per directory or per file.

**How other engines do it:** Godot compresses at import time to BC formats on
desktop (uses etcpak). Defold uses Basis Universal with per-texture quality
settings. Love2D and Raylib can *load* pre-compressed DDS/KTX but don't
compress for you. All follow the same pattern: compress offline, store
compressed, upload directly.

**Implementation plan:**

1. Add stb_dxt to `libraries/` (one header). Wire it into `cmd_package.cc` to
   produce BC1/BC3 blocks. Store in SQLite with format metadata.
2. Modify the texture upload path in `renderer.cc` to detect compressed format
   and call `glCompressedTexImage2D` instead of `glTexImage2D`.
3. Add a per-texture/per-directory compression config (probably in the asset
   manifest or a `compression.toml`).
4. Later: swap stb_dxt for ISPC Texture Compressor to get BC7 support.
5. Later still: adopt Basis Universal / KTX2 for on-disk supercompression.

#### 2.3 Texture Arrays for Sprite Atlases

`GL_TEXTURE_2D_ARRAY` eliminates atlas UV bleeding and simplifies sprite lookup.
Each sprite occupies its own layer with clean 0-1 UVs.

```cpp
TextureArray CreateTextureArray(int width, int height, int layers, FilterMode filter);
void UploadLayer(TextureArray* arr, int layer, const uint8_t* pixels);
```

**Implementation notes:**
- All sprites in an atlas must share the same dimensions (standard for
  spritesheets with uniform cell size)
- Non-uniform sprites still use the existing atlas path
- Vertex attribute gains a `layer` float passed to the fragment shader
- Fragment shader samples `texture(u_array, vec3(uv, layer))`
- Falls back gracefully: if sprites are different sizes, keep using UV atlas

---

### Tier 3 -- Higher Effort, Big Payoff

#### 3.1 2D Lighting System

Godot is the only engine with built-in 2D lighting. It's a differentiator.

**Approach: lightmap compositing (simplest viable)**

1. Render the scene normally to `scene_canvas`
2. Render lights to `light_canvas` (black background, additive blend):
   - Point lights: textured quads with radial falloff
   - Directional lights: full-screen tinted quads
   - Spot lights: angular falloff variant
3. Multiply `scene_canvas * light_canvas` for the final image
4. Emissive sprites bypass the lightmap (drawn after multiply)

```lua
local light = G.graphics.new_light({
    type = "point",
    color = {1, 0.9, 0.7},
    radius = 200,
    intensity = 1.5,
    falloff = "quadratic",   -- or "linear", "smooth"
})

function draw()
    G.graphics.begin_lighting()
        G.graphics.set_ambient(0.05, 0.05, 0.1)  -- dark ambient
        G.graphics.add_light(light, player.x, player.y)
    G.graphics.end_lighting()

    -- scene is automatically lit
    draw_scene()
    G.graphics.apply_lighting()
end
```

**Advanced (optional later):**
- Shadow casting via 1D shadow maps (radial occlusion) or SDF-based shadows
  (Godot's approach)
- Normal maps on sprites for directional response

#### 3.2 GPU Particle System

CPU particle systems cap out around 10-50k particles. A GPU system using
instanced rendering (we already call `glDrawElementsInstanced`) can handle
100k+ easily.

**Architecture:**

- Particle state stored in a vertex buffer with per-instance attributes:
  position, velocity, color, size, age, lifetime
- **Update on CPU** (simpler, still fast for 100k with SoA layout) or
  **update via transform feedback / compute shader** (GL 4.3+, unlimited scale)
- Single instanced draw call per emitter

**Lua API:**

```lua
local emitter = G.particles.new_emitter({
    texture = "spark.png",
    max_particles = 10000,
    emission_rate = 500,          -- particles/sec
    lifetime = {0.5, 1.5},       -- random range
    speed = {50, 150},
    direction = {0, -1},
    spread = math.pi / 4,
    color_over_life = {{1,1,1,1}, {1,0.5,0,0}},
    size_over_life = {4, 0},
    gravity = {0, 200},
    blend_mode = "add",
})

function update(t, dt)
    emitter:update(dt)
    emitter:emit_at(player.x, player.y)
end

function draw()
    emitter:draw()
end
```

**Phase 1:** CPU update + instanced draw (medium effort, big improvement)
**Phase 2:** Compute shader update (high effort, massive scale)

#### 3.3 Render Layers / Sort Order (Sort Keys)

##### The Problem

The current renderer draws everything in submission order. If a game draws
background tiles, then entities, then UI, the script author must ensure those
calls happen in exactly that order. Interleaving breaks visual layering *and*
batching: a background circle drawn between two entity sprites forces a texture
flush even though the sprites share a spritesheet.

There is no depth buffer (we call `glDisable(GL_DEPTH_TEST)`) because 2D games
rely on painter's algorithm -- draw back-to-front. What we need is a way for
scripts to declare *intent* ("this belongs on the background layer") and let the
renderer sort for optimal batching within that constraint.

##### Sort Key Design

Each draw command gets a 64-bit sort key that encodes draw order priority:

```
 63        48 47      32 31    24 23    16 15              0
 ┌──────────┬──────────┬────────┬────────┬─────────────────┐
 │  layer   │  depth   │ shader │texture │  sequence       │
 │ (16 bit) │ (16 bit) │ (8 bit)│ (8 bit)│  (16 bit)       │
 └──────────┴──────────┴────────┴────────┴─────────────────┘
```

**layer** (bits 63-48, 16 bits): User-assigned render layer. Higher layers
draw on top. Range 0-65535, default 0. This is the primary sort axis -- draws
on different layers never reorder relative to each other.

**depth** (bits 47-32, 16 bits): Sub-layer ordering within a layer. Defaults
to a monotonically increasing counter (preserving submission order). Scripts
can override this for explicit front-to-back or back-to-front control within
a layer. Range 0-65535.

**shader** (bits 31-24, 8 bits): Shader program index. Grouping by shader
minimizes the most expensive state change (`set_program_state` queries
attribute locations and binds VBO offsets). Supports up to 256 distinct
shaders, which is well beyond what a 2D engine needs.

**texture** (bits 23-16, 8 bits): Texture unit index. Grouping by texture
after shader eliminates most remaining flushes. 256 slots covers the
spritesheet + font textures + canvas textures comfortably.

**sequence** (bits 15-0, 16 bits): Submission order within the same
layer/depth/shader/texture group. Preserves relative draw order for
overlapping transparent geometry that shares all other key fields. This is
what makes the sort stable.

**Why this bit layout:** Layers dominate because they encode the user's
explicit visual ordering. Within a layer, depth lets scripts control overlap.
Within the same depth, we sort by GPU state (shader > texture) to minimize
flushes. Shader switches are more expensive than texture binds, so shader
gets higher bits. Sequence is the tiebreaker that preserves submission order.

##### Lua API

```lua
-- Layer control
G.graphics.set_layer(layer)         -- set current layer (0-65535)
G.graphics.set_depth(depth)         -- set depth within layer (0-65535)

-- Convenience constants (games define their own)
LAYER_BG     = 0
LAYER_WORLD  = 100
LAYER_FX     = 200
LAYER_UI     = 1000
```

Typical usage in a game:

```lua
function draw()
    G.graphics.set_layer(LAYER_BG)
    draw_starfield()         -- circles + sprites, freely interleaved

    G.graphics.set_layer(LAYER_WORLD)
    draw_entities()          -- meteors, bullets, player

    G.graphics.set_layer(LAYER_FX)
    draw_particles()         -- explosions, thrust

    G.graphics.set_layer(LAYER_UI)
    draw_hud()               -- health bar, score, minimap
end
```

The game can draw in any order -- `draw_hud()` before `draw_starfield()` would
produce the same visual result. The renderer sorts by sort key before flushing.

`set_depth()` is for fine control within a layer:

```lua
G.graphics.set_layer(LAYER_WORLD)
for _, entity in ipairs(entities) do
    G.graphics.set_depth(entity.y)   -- y-sort for top-down games
    entity:draw()
end
```

##### C++ Implementation

**Step 1: Add sort key state to Renderer.**

```cpp
// In Renderer class:
uint16_t current_layer_ = 0;
uint16_t current_depth_ = 0;
uint16_t sequence_ = 0;   // auto-incrementing per frame
```

**Step 2: Stamp each QueueEntry with a sort key.**

Extend `QueueEntry` from 32 bits to 96 bits (or use a parallel array):

```cpp
struct QueueEntry {
    uint32_t type : 12;
    uint32_t count : 20;
    uint64_t sort_key;
};
```

When a draw command (kRenderQuad, kRenderTrig, kStartLine) is queued, compute:

```cpp
uint64_t sort_key =
    (uint64_t(current_layer_)  << 48) |
    (uint64_t(current_depth_)  << 32) |
    (uint64_t(shader_index)    << 24) |
    (uint64_t(texture_index)   << 16) |
    (uint64_t(sequence_++));
```

State-change commands (kSetTexture, kSetShader, etc.) don't get independent
sort keys -- they are *attached* to the draw commands they precede. This is
important: we don't sort individual state changes, we sort draw groups.

**Step 3: Group commands into sortable draw groups.**

A "draw group" is a run of state-change commands followed by one or more
geometry commands. During recording, track group boundaries:

```cpp
struct DrawGroup {
    uint64_t sort_key;      // key of the first geometry command
    uint32_t cmd_start;     // index into command buffer
    uint32_t cmd_count;     // number of commands in this group
};
```

At frame end, before `Render()`, sort `DrawGroup`s by `sort_key` and replay
commands in sorted order. State changes within a group stay in their original
relative order.

**Step 4: Sort and render.**

```cpp
void BatchRenderer::Render() {
    // 1. Build draw groups from command buffer
    // 2. Sort groups by sort_key
    // 3. Iterate sorted groups, executing commands in order
    //    (with Tier 1 redundant state filtering applied)
}
```

The sort is `std::stable_sort` on draw groups, not individual commands. A
typical frame has ~50-200 draw groups (one per distinct draw call site), so
the sort is negligible cost.

##### Interaction with Existing Features

**Transforms:** Transform commands (push/pop/translate) are per-group state.
When draw groups are reordered, their associated transforms move with them.
This means each group must capture its full transform state, not rely on
incremental push/pop. Two approaches:

- **Eager:** When recording a draw group, snapshot the current transform
  matrix into the group. Replace push/pop/translate within the group with a
  single kSetTransform using the snapshot. This simplifies sorting but
  changes how transforms compose.
- **Lazy (recommended initially):** Keep push/pop as-is. Only sort draw
  groups that are within the *same* transform scope (no push/pop crossings).
  Groups separated by push/pop are in different scopes and maintain their
  relative order within a scope. This is less optimal for batching but is
  safe and preserves existing transform behavior.

**Scissor and stencil:** These are "barrier" commands -- they define regions
where reordering is not safe. Draw groups before a scissor/stencil change
must not reorder past it. Implementation: assign barrier commands a sort key
that forces them to their original position (e.g., set layer = current layer,
depth = current sequence, to prevent reordering across barriers).

**Canvas switches:** `kSetCanvas` is a hard barrier. No draw group should
sort across a canvas boundary. Each canvas scope is sorted independently.

**Blend mode:** Currently in the sort key as part of shader/texture grouping.
If two draw groups within the same layer use different blend modes, they sort
by blend mode (could be added as bits 15-8, shifting sequence down to 7-0, or
kept as a secondary sort after texture). For most 2D games, blend mode
changes are rare within a layer.

##### Comparison with Other Engines

**Godot 2D:** Uses a `canvas_item` system where each node has a z-index
(-4096 to 4096) and optional z-relative flag. Items are sorted by z-index,
then by tree order within the same z. Y-sort is an opt-in mode per node.
Godot batches by material (shader + texture + blend) within the sorted order.
Our layer/depth system is similar but simpler -- no scene tree, just flat
numeric keys.

**Love2D:** No built-in sort. Love2D relies on the user to submit draws in
order. The `SpriteBatch` class lets you manually batch same-texture sprites.
Our approach is strictly better -- automatic batching with user-controlled
layering.

**Raylib:** No sort or layer system. Pure immediate mode, draws in submission
order. No batching across draw calls (each `DrawTexture*` is its own draw
call unless using `rlDrawRenderBatchActive`).

**Cocos2d-x:** Uses `globalZOrder` (float) for sorting. Nodes with the same
z-order render in tree order. Auto-batching groups consecutive draws with the
same texture/shader/blend into one call. Very similar to our proposal.

**MonoGame/XNA:** `SpriteBatch.Begin()` accepts a `SpriteSortMode`:
`Deferred` (submission order), `BackToFront`, `FrontToBack`, `Texture`
(sort by texture for batching). Our sort key effectively combines `Texture`
sorting with layer-based ordering -- the best of both modes.

##### Phased Implementation

**Phase 1 (low effort):** Add `set_layer()` and `set_depth()` to the Lua API.
Store layer/depth in the Renderer. Stamp sort keys on draw commands. Do NOT
sort yet -- just record the keys. This lets games start using the API
immediately with no rendering behavior change.

**Phase 2 (medium effort):** Implement draw group extraction and sorting.
Initially, only sort within "safe" regions (between canvas/stencil/scissor
barriers). Verify that Tier 1 redundant state filtering still works after
reordering (it should, since filtering happens at render time, after sorting).

**Phase 3 (medium effort):** Handle transform scopes. Implement eager
transform snapshotting for draw groups, eliminating push/pop overhead and
enabling full cross-scope reordering. This is where the biggest batching
wins come from -- entity sprites from different transform scopes can now
batch together if they share the same texture.

##### Expected Impact

Using the testgame1 (Space Garbage!) draw call breakdown as a reference:

- **Starfield:** 3 sub-layers (circles, star1 sprites, star2 sprites)
  currently interleaved with the entity layer. With sort keys, all 60
  circles batch into 1 draw call, all 45 star sprites batch into 1-2 draw
  calls. Saves ~100 flushes.
- **Entities:** All meteor sprites on the same spritesheet batch together
  regardless of wrapping transforms (Phase 3). ~15 meteors -> 1 draw call
  instead of ~45.
- **Particles + aim line:** All circles batch together across both systems.
  Saves ~30-200 flushes depending on particle count.
- **HUD:** Score digits, lives icons batch into 1 draw call. Text calls
  group their shader switches.

Combined with Tier 1 redundant state filtering, sort keys should reduce the
~193 draw calls to approximately 10-15.

---

### Tier 4 -- Future / Exploratory

| Feature | Why | Effort |
|---------|-----|--------|
| Compute shaders | GPU physics, procedural textures, advanced particles | High - new shader pipeline |
| Multi-channel SDF (MSDF) text | Sharper corners than single-channel SDF | Medium - swap atlas generator |
| Geometry shader debug viz | Expand points to crosses/arrows for physics debug | Low-medium, but perf varies by GPU |
| SDF shape rendering | Analytically smooth circles, rects, rounded rects in fragment shader | Medium - new draw path |
| Render graph / command reordering | Minimize state changes automatically | High - architectural change |

---

## Priority Recommendation

**Quick wins to land first (1-2 days each):**

1. **Stencil ops** -- the buffer is already there, just wire it up
2. **Scissor rect** -- one GL call, huge utility for UI
3. **Custom blend equations** -- small extension of existing blend system

**Next sprint (1-2 weeks):**

4. **Texture compression** -- stb_dxt for BC1/BC3 in the packer, then BC7 via ISPC
5. **Post-processing pipeline** -- enables bloom, CRT, LUT, transitions
6. **Texture arrays** -- eliminates atlas bleeding, simplifies sprite batching

**Stretch goals:**

7. **2D lighting** -- big visual upgrade, builds on post-processing
8. **GPU particles** -- builds on instanced rendering we already have
9. **Render layers** -- quality-of-life for game scripts

## Particle System

See [[Particle system]] for the full design: engine comparison survey (Love2D,
BYTEPATH, Godot, Unity, Impact.js), SoA data model, instanced rendering,
PropertyRamp/ColorRamp, Lua API, hot reload integration, memory budget, and
phased implementation plan.

---

## Appendix: Engine Comparison Summary

The engine's current renderer is comparable to **Raylib** in features: solid
batching, basic blend modes, off-screen canvases, good text rendering. It's
ahead of Raylib on SDF text, shader hot-reload, and the custom shader uniform
API (`send_uniform`, `has_uniform`, auto-uniforms `g_Time`/`g_ScreenSize`).

To reach **Love2D** parity, we need stencil and scissor. To approach **Godot
2D**, we'd additionally need lighting, GPU particles, and a post-processing
chain.

The gap is not large -- most of these features layer on top of the existing
architecture without requiring a rewrite.
