# Draw Call Optimization

## Problem Statement

The testgame1 demo (an Asteroids-style shooter) reports ~193 draw calls per
frame in a moderate gameplay scenario. For a 2D game drawing ~150-250 visible
elements, this means the batch renderer is flushing nearly once per visible
element -- the batching is providing almost no benefit.

The root cause is **redundant state changes**: the renderer flushes the current
batch unconditionally on every texture, transform, shader, blend mode, and
line-width command, even when the new value is identical to the current one.

## How the Batch Renderer Works (Background)

The `BatchRenderer` records draw commands (quads, triangles, line points) and
state-change commands (texture, transform, shader, blend mode) into a command
buffer during the frame. At the end of the frame, `Render()` iterates the
command buffer and builds vertex data. It accumulates geometry until it hits a
state-change command, at which point it issues a `glDrawElementsInstanced` call
(a "flush") for the accumulated geometry, applies the state change, and starts
accumulating again.

Each flush has overhead: the CPU must set up the GL call, the driver validates
state, and the GPU may stall waiting for the previous draw to complete. At 193
flushes, this overhead dominates. The goal of batching is to minimize flushes
by grouping geometry that shares the same state.

## What Triggers a Flush

Every `case` in the `Render()` switch that calls `flush()`, with whether it
checks for redundancy:

| Command | Flushes? | Checks if value changed? |
|---------|----------|--------------------------|
| `kSetTransform` | Always | No |
| `kSetTexture` | Always | No |
| `kSetShader` | Always | No |
| `kSetLineWidth` | Always | No |
| `kSetCanvas` | Always | No |
| `kSetBlendMode` | Always | No |
| `kClearColor` | Always | N/A (has side effect: glClear) |
| `kSetSDFOutline` | Always | No |
| `kSetScissor` | Always | No |
| `kClearScissor` | Always | No |
| `kBeginStencilWrite` | Always | No |
| `kEndStencilWrite` | Always | No |
| `kSetStencilTest` | Always | No |
| `kClearStencilTest` | Always | No |
| `kEndLine` | Always | N/A (ends a line strip) |
| `kDone` | Always | N/A (end of frame) |
| `kSetColor` | **No** | N/A (per-vertex, no GL state) |
| `kRenderQuad` | Only if primitive type changes | Yes |
| `kRenderTrig` | Only if primitive type changes | Yes |
| `kStartLine` | Only if primitive type changes | Yes |

**Key observation:** `kSetColor` is the *only* state command that doesn't
flush. Every other state command flushes unconditionally. The geometry commands
(`kRenderQuad`, `kRenderTrig`, `kStartLine`) correctly check primitive type
before flushing, but no state command checks whether the new value differs from
the current one.

## What Each Draw Method Emits

Each high-level draw method in the `Renderer` class emits state-change
commands before its geometry. This is what determines the flush pattern:

| Draw method | Commands before geometry |
|-------------|------------------------|
| `DrawSprite(name, pos, angle)` | `kSetTexture(spritesheet_tex)` |
| `DrawImage(name, pos, angle)` | `kSetTexture(image_tex)` |
| `DrawRect(...)` | `kSetTexture(noop_tex)` (via ClearTexture) |
| `DrawCircle(center, r)` | `kSetTexture(noop_tex)` (via ClearTexture) |
| `DrawTriangle(...)` | `kSetTexture(noop_tex)` (via ClearTexture) |
| `DrawRectOutline(...)` | `kSetTexture(noop_tex)` + `kStartLine` + `kEndLine` |
| `DrawCircleOutline(...)` | `kSetTexture(noop_tex)` + `kStartLine` + `kEndLine` |
| `DrawText(font, size, str, pos)` | `kSetShader("sdf")` + `kSetSDFOutline` + `kSetTexture(font_tex)` ... then at end: `kSetShader("pre_pass")` |
| `DrawCanvas(canvas, pos)` | `kSetTexture(canvas_tex)` |
| `G.graphics.push()` | (nothing -- just duplicates transform stack) |
| `G.graphics.translate(x,y)` | `kSetTransform(new_matrix)` |
| `G.graphics.pop()` | `kSetTransform(restored_matrix)` |
| `G.camera.attach()` | `kSetTransform(view_matrix)` |
| `G.camera.detach()` | `kSetTransform(restored_matrix)` |
| `G.graphics.set_blend_mode(m)` | `kSetBlendMode(m)` |
| `G.graphics.set_color(r,g,b,a)` | `kSetColor` (**no flush**) |
| `G.graphics.attach_shader(s)` | `kSetShader(s)` |

## Testgame1 Draw Call Breakdown

Here is an exhaustive accounting of what testgame1 draws each frame in a
typical mid-game scenario (wave 3-4, ~15 meteors, ~10 bullets, 2 powerups,
~12 particles, player alive):

### Phase 1: Canvas setup (2 flushes)

```
set_canvas(game_canvas)     -> kSetCanvas           -> FLUSH
clear()                     -> kClearColor           -> FLUSH
```

### Phase 2: Attach CRT shader (1 flush)

```
attach_shader("crt.frag")  -> kSetShader("crt")     -> FLUSH
```

### Phase 3: Starfield (≥107 flushes)

**Layer 1: 60 tiny circles** (each star is a draw_circle)
```
set_color(255,255,255,120)  -> kSetColor             -> no flush
draw_circle(x, y, 1)       -> kSetTexture(noop)      -> FLUSH (first one only;
draw_circle(x, y, 1)       -> kSetTexture(noop)      -> FLUSH (same noop!)
... (60 times total)
```

Each `DrawCircle` calls `ClearTexture()` which emits `kSetTexture(noop_tex)`.
Even though the texture is already `noop_tex`, the renderer flushes because it
doesn't check. **60 redundant flushes.**

**Layer 2: 30 sprites** ("star1")
```
set_color(255,255,255,150)  -> kSetColor             -> no flush
draw_sprite("star1", ...)   -> kSetTexture(sheet)     -> FLUSH (first changes noop->sheet)
draw_sprite("star1", ...)   -> kSetTexture(sheet)     -> FLUSH (same sheet!)
... (30 times total)
```

All 30 sprites are from the same spritesheet, but each `DrawSprite` emits
`kSetTexture` unconditionally. **29 redundant flushes.**

**Layer 3: 15 sprites** ("star2")
```
draw_sprite("star2", ...)   -> kSetTexture(sheet)     -> FLUSH (same sheet as star1!)
... (15 times total)
```

If "star1" and "star2" are on the same spritesheet (likely, since they're both
star assets), these are also redundant. **14-15 redundant flushes.**

### Phase 4: Camera attach (1 flush)

```
G.camera.attach()           -> kSetTransform(view)    -> FLUSH
```

### Phase 5: Entity drawing with wrapping (~35-80 flushes)

Each non-bullet entity draws with wrapping: push/translate/draw/pop. Each
translate and pop emits a kSetTransform, causing 2 flushes per wrap offset.

**Bullets (no wrapping):** ~10 bullets
```
draw_sprite("laserGreen11", x, y, angle) -> kSetTexture(sheet) -> FLUSH per bullet
```

If all bullets use the same spritesheet (they do), only the first changes
state. **9 redundant flushes.**

**Meteors (with wrapping):** ~15 meteors, typically 1-2 wrap offsets each
```
push()                        -> (nothing)
translate(ox, oy)             -> kSetTransform         -> FLUSH
draw_sprite("meteorBrown_big1", x, y, angle) -> kSetTexture(sheet) -> FLUSH
pop()                         -> kSetTransform         -> FLUSH
```

That's 3 flushes per meteor per wrap offset. With 15 meteors averaging ~1.3
offsets: ~58 flushes. The texture flush is often redundant (same spritesheet),
but the transform flushes are necessary since each meteor has a different
wrapping offset.

**However:** the Pop at the end of one entity and the Translate at the start
of the next entity both emit kSetTransform. If the next entity has offset
(0,0) and the camera view hasn't changed, the Pop restores the same transform
that the next Translate would set -- redundant.

**Player:** 1-2 sprites (ship + optional shield), similar pattern.

**Powerups:** ~2 sprites, with wrapping.

### Phase 6: Particles (~12-50 flushes)

```
set_color(r, g, b, alpha)    -> kSetColor             -> no flush
draw_circle(x, y, radius)    -> kSetTexture(noop)     -> FLUSH
```

Each particle is a circle. After the entity sprites, the first circle switches
from the sprite texture to `noop_tex` (1 real flush). The remaining 11-49
circles all set `noop_tex` again. **All but the first are redundant.**

### Phase 7: Aim line (~25-200 flushes)

25 circles per wrap position. Same pattern as particles -- all but the first
set `noop_tex` redundantly. With wrapping, each wrap offset also has
push/translate/pop adding 2 transform flushes.

### Phase 8: Camera detach (1 flush)

```
G.camera.detach()             -> kSetTransform         -> FLUSH
```

### Phase 9: HUD (~15-20 flushes)

**Health bar:** 2 rects (2 `kSetTexture(noop)`, first is real, second redundant).

**Score digits:** each `draw_sprite` emits `kSetTexture(sheet)`. If digits are
on the same sheet, only the first is real. ~4-6 digits = 3-5 redundant.

**Lives:** 1-3 `draw_sprite` calls, same sheet = 0-2 redundant.

**Wave text:** `draw_text` emits `kSetShader("sdf")` + `kSetSDFOutline` +
`kSetTexture(font)` = 3 flushes. Then at the end, `kSetShader("pre_pass")` =
1 flush. **4 flushes per text call.**

**Powerup icon:** 1 draw_sprite (1 flush for texture change from font to
spritesheet).

### Phase 10: Minimap (~20 flushes)

2 rects + ~15-20 circles. After the first rect sets `noop_tex`, all subsequent
rects and circles set `noop_tex` redundantly. **~17 redundant flushes.**

### Phase 11: Messages (0-4 flushes per message)

Each `draw_text` = 4 flushes (shader switch + outline + texture + shader
restore).

### Phase 12: Canvas compositing (~4 flushes)

```
detach_shader()                -> kSetShader("pre_pass") -> FLUSH
set_canvas(screen)             -> kSetCanvas              -> FLUSH
set_blend_mode("premultiplied") -> kSetBlendMode          -> FLUSH
draw_canvas(game_canvas, 0, 0) -> kSetTexture(canvas)    -> FLUSH
set_blend_mode("alpha")        -> kSetBlendMode           -> FLUSH
draw_canvas(vignette, 0, 0)   -> kSetTexture(vignette)   -> FLUSH
```

### Total Estimate

| Source | Necessary flushes | Redundant flushes |
|--------|------------------|-------------------|
| Canvas/shader setup | ~4 | 0 |
| Starfield circles (60) | 1 | **59** |
| Starfield sprites (45) | 1-2 | **43-44** |
| Camera attach/detach | 2 | 0 |
| Entity transforms (push/translate/pop) | ~40 | ~15 |
| Entity sprites (bullets/meteors/player/powerups) | ~5 | ~23 |
| Particles (circles) | 1 | **11-49** |
| Aim line (circles) | 1 | **24-199** |
| HUD rects/sprites | ~5 | ~5 |
| HUD/message text | ~8-16 | 0 |
| Minimap circles | 1 | **17** |
| Canvas compositing | ~6 | 0 |
| **Total** | **~75-95** | **~100-400** |

**Conclusion:** Roughly 50-80% of all draw calls are redundant flushes caused
by setting state to the same value it already has. The single biggest
contributor is `kSetTexture(noop_tex)` from `ClearTexture()` in consecutive
primitive draws (circles, rects).

---

## Improving Observability

Before optimizing, we need better tools to see what's happening. Right now the
only rendering metrics are:

1. **Perfetto counters** (`PROFILE_COUNTER`): "Draw Calls" and "Vertices" per
   frame, emitted at the end of `Render()`. These appear as line graphs in
   Perfetto but provide no breakdown of *why* flushes happen.
2. **No debug UI display**: the Tab debug overlay shows FPS, frame time stats,
   and Lua memory, but nothing about rendering.

### Proposal 1: Expose frame stats in the debug UI

Add a `FrameStats` struct to `BatchRenderer` that's populated during
`Render()` and readable after:

```cpp
struct FrameStats {
    int draw_calls;    // Number of glDrawElements/glDrawArrays calls.
    int vertices;      // Total vertices submitted.
    int commands;      // Number of batch commands queued.
};
```

Display in the Tab debug overlay alongside the existing FPS/stats line. This
gives immediate feedback when testing: you can see draw calls spike in real
time as you play.

### Proposal 2: Per-reason flush counters in Perfetto

Add counters that break down *why* flushes happen. Inside the flush lambda,
before calling `flush()` for each command type, increment a per-reason counter:

```cpp
int flush_texture = 0, flush_transform = 0, flush_shader = 0;
int flush_blend = 0, flush_canvas = 0, flush_line_end = 0, flush_other = 0;
```

Emit them as Perfetto counters at frame end:

```cpp
PROFILE_COUNTER("Flush: Texture", flush_texture);
PROFILE_COUNTER("Flush: Transform", flush_transform);
PROFILE_COUNTER("Flush: Shader", flush_shader);
PROFILE_COUNTER("Flush: Blend Mode", flush_blend);
PROFILE_COUNTER("Flush: Canvas", flush_canvas);
PROFILE_COUNTER("Flush: Line End", flush_line_end);
PROFILE_COUNTER("Flush: Other", flush_other);
```

In Perfetto, these render as stacked line graphs, immediately showing which
state changes dominate. With the testgame1 workload, we'd expect to see
`Flush: Texture` towering over everything else.

### Proposal 3: Redundant flush counters

To confirm the redundancy hypothesis, add counters that specifically track
*redundant* flushes -- cases where the new value equals the current value:

```cpp
int redundant_texture = 0, redundant_transform = 0, redundant_shader = 0;

// In kSetTexture handler:
if (c.set_texture.texture_unit == texture_unit) {
    redundant_texture++;
    break;  // Skip the flush (the optimization itself)
}
flush();
texture_unit = c.set_texture.texture_unit;
```

Emit as a Perfetto counter: `PROFILE_COUNTER("Redundant: Texture", ...)`.
Initially, implement the counting *without* the skip, to measure the problem
before fixing it. Then enable the skip and confirm the counter drops to zero.

### Proposal 4: Batch size histogram

Track how many primitives (quads/triangles) are in each batch when it flushes.
A healthy batching system should have large batches (hundreds of primitives per
flush). If most batches contain 1-2 primitives, the batching is failing.

```cpp
int batch_size_1 = 0;      // Flushes with exactly 1 primitive.
int batch_size_2_10 = 0;   // Flushes with 2-10 primitives.
int batch_size_10plus = 0; // Flushes with 10+ primitives.
```

This tells you whether the redundant flush elimination actually leads to
larger batches (it should).

---

## Optimization Proposals (Low to High Effort)

### Tier 1: Redundant State Filtering (low effort, high impact)

Skip the flush when the new value equals the current value. This requires one
comparison per state-change command, which is negligible cost.

**kSetTexture:**
```cpp
case kSetTexture:
    if (c.set_texture.texture_unit != texture_unit) {
        flush();
        texture_unit = c.set_texture.texture_unit;
    }
    break;
```

**kSetShader:** Requires tracking a `current_shader` handle in the Render()
method. `set_program_state()` is expensive (queries attribute locations,
binds VBO offsets), so skipping redundant shader switches is high value.

```cpp
case kSetShader:
    if (c.set_shader.shader_handle != current_shader_handle) {
        flush();
        current_shader_handle = c.set_shader.shader_handle;
        set_program_state(StringByHandle(c.set_shader.shader_handle));
    }
    break;
```

**kSetTransform:** Uses `FMat4x4::operator==` (epsilon comparison of 16
floats). This is ~64 bytes of comparison per transform command, but transforms
are only emitted on push/pop/translate/rotate/scale -- much less frequent than
texture commands. Worth doing.

```cpp
case kSetTransform:
    if (!(c.set_transform.transform == transform)) {
        flush();
        transform = c.set_transform.transform;
    }
    break;
```

**kSetBlendMode, kSetLineWidth:** Simple integer/float comparison.

**kSetSDFOutline:** Compare thickness and color. Less frequent (only in text
draws), but free to add.

**Expected impact:** From the testgame1 analysis, this should eliminate
~100-200 redundant flushes per frame, cutting draw calls from ~193 to ~60-80.
The starfield alone should drop from ~105 flushes to ~3 (one per texture
change: noop -> star1_sheet -> star2_sheet, or just 2 if they share a sheet).

**Verification:** After implementing, check the Perfetto "Redundant: Texture"
counter (Proposal 3 above) and the "Draw Calls" counter. The debug UI draw
call display gives instant feedback.

### Tier 2: Command-Level Deduplication (low effort, modest impact)

Instead of filtering at flush time, prevent redundant commands from being
emitted in the first place. Track the last-emitted state in the `Renderer`
wrapper class and skip commands when the value hasn't changed:

```cpp
// In Renderer class (not BatchRenderer):
void Renderer::DrawCircle(FVec2 center, float radius) {
    if (last_texture_ != noop_texture_) {
        renderer_->ClearTexture();
        last_texture_ = noop_texture_;
    }
    // ... push triangles
}
```

This is complementary to Tier 1. Tier 1 filters at the batch renderer level
(catches everything). Tier 2 filters at the source (reduces command buffer
bloat, which matters when the buffer is large).

**The simplest version:** just track `last_texture_` in `Renderer` and skip
redundant `SetActiveTexture` / `ClearTexture` calls. This single change
eliminates the biggest source of redundant commands.

### Tier 3: DrawText Shader Caching (low effort, moderate impact)

Every `DrawText` call switches to the `"sdf"` shader and back. If you draw
two text strings in a row (common in HUD: score, wave, messages), the sequence
is:

```
SetShader("sdf") -> draw glyphs -> SetShader("pre_pass")
SetShader("sdf") -> draw glyphs -> SetShader("pre_pass")
```

That's 4 shader flushes for 2 text draws. With Tier 1 deduplication, the
second `SetShader("sdf")` would be caught as redundant... but only if
`DrawText` didn't restore the shader at the end.

**Proposal:** Make `DrawText` lazily restore the shader. Track a "dirty shader"
flag and only restore when a non-text draw happens. Or better: with Tier 1 in
place, simply let the redundant switch get filtered out for free.

### Tier 4: Draw Call Reordering / Sort Keys (medium effort, high impact)

The game interleaves sprite draws and primitive draws:

```
draw_circle (noop texture)
draw_sprite (spritesheet A)
draw_circle (noop texture)
draw_sprite (spritesheet A)
```

Each transition flushes even after Tier 1 deduplication (because the texture
*does* change each time). The fix is to reorder draws so all same-state draws
are adjacent:

```
draw_circle (noop texture)    \  batched
draw_circle (noop texture)    /
draw_sprite (spritesheet A)   \  batched
draw_sprite (spritesheet A)   /
```

This halves the flush count for interleaved workloads.

**Implementation:** Assign each command a sort key that encodes
`(layer, texture, shader, blend_mode)`. Before flushing, sort the command
buffer by this key (stable sort to preserve order within a group). Draws within
the same sort key batch perfectly.

**Caveat:** Reordering changes draw order, which matters for overlapping
transparent geometry. This needs a user-visible layer system (see the Renderer
Improvements doc) so scripts can control what gets reordered together.

This is the approach used by Godot 2D, Love2D (partial), and most production
2D renderers.

### Tier 5: Atlas Consolidation (medium effort, high impact)

If all game sprites are packed into a single texture atlas (single
spritesheet), then `kSetTexture` never needs to change between sprite draws.
Combined with Tier 1, this means all sprite draws in a frame batch into one
draw call regardless of interleaving with other draw types.

Currently, different sprite assets may be on different spritesheets. The
asset pipeline (`cmd_package.cc`) could be extended to pack all sprites into
one atlas (or as few as possible, respecting max texture size).

**Verification:** Check how many distinct spritesheets testgame1 uses. If
it's already just one, this tier is free. If it's several, consolidation would
eliminate cross-sheet texture flushes.

### Tier 6: Primitive Draw Batching (medium effort, moderate impact)

Currently, `DrawCircle` emits 22 `kRenderTrig` commands (one per triangle in
the circle fan). Each circle also emits `kSetTexture(noop)`. If you draw 60
circles in a row (starfield layer 1), that's 60 texture commands + 1320
triangle commands.

With Tier 1, the 59 redundant texture commands are eliminated, and the 1320
triangles batch into one draw call. This is already a big win.

But a further optimization: introduce a `DrawCircleBatch` method that takes an
array of (center, radius) pairs and emits all triangles without any
intervening state commands. The starfield could use this to draw all 60 circles
in one call with zero state changes.

### Tier 7: Entity Transform Batching (high effort, moderate impact)

The entity wrapping pattern (push/translate/draw/pop per entity) emits 2
transform commands per entity per wrap offset. With 15 meteors at ~1.3 offsets
= ~40 transform flushes. These are *not* redundant (each entity has a
different offset), so Tier 1 doesn't help.

**Approach A: Bake transforms into vertex positions.** Instead of using the
transform stack, compute the final world position on the CPU and pass it
directly to `PushQuad`. This eliminates all transform commands for entity
drawing. The `DrawSprite` method would need a variant that accepts a
pre-transformed position without going through the transform stack.

**Approach B: Instanced rendering for entities.** Same idea as the particle
system design: upload per-entity data (position, rotation, sprite UV rect) to
an instance buffer and draw all entities of the same type in one instanced
call. This is a bigger change but scales to many more entities.

---

## Implementation Order

1. **Observability first** (Proposals 1-3): Add `FrameStats` to debug UI,
   per-reason flush counters, and redundant flush counters to Perfetto. This
   lets us measure the baseline and verify each optimization.

2. **Tier 1: Redundant state filtering.** The single highest-impact change.
   Verify via debug UI that draw calls drop significantly.

3. **Tier 2: Command-level dedup in Renderer.** Reduces command buffer size.

4. **Tier 3: DrawText shader caching.** Falls out naturally from Tier 1 but
   can be made explicit for clarity.

5. **Tiers 4-7** are larger architectural changes that should wait until the
   low-hanging fruit is measured. Tier 4 (sort keys) is the most impactful
   long-term change but requires the layer system from the Renderer
   Improvements design doc.
