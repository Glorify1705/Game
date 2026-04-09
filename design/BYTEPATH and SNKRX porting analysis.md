---
status: proposed
tags: [reference, lua-api, love2d, gap-analysis, porting]
---

# BYTEPATH and SNKRX Porting Analysis

This document inventories the Love2D APIs used by two published a327ex games —
**BYTEPATH** (2018, ~20k LOC Lua) and **SNKRX** (2021, ~25k LOC Lua) — and
maps each usage to our engine's current `G.*` API. The goal is to identify
which features we would need to build in order to port either game, rank those
features by implementation effort and importance, and propose an
implementation order.

Both games are stored under `workbench/BYTEPATH/` and `workbench/SNKRX/` as
upstream source drops. Neither is expected to be ported 1:1 — the question is
*what APIs does our engine still need* so that games of this complexity are
realistically in reach.

## Method

For each game we walked every `love.*` call site and grouped them by module
(`love.graphics`, `love.physics`, `love.audio`, `love.filesystem`,
`love.image`, etc.). For each distinct call we asked three questions:

1. Do we already have a direct equivalent in `G.*`?
2. Do we have an adjacent feature that could be adapted without engine
   changes (e.g. `G.collision` instead of `love.physics` sensors)?
3. If neither, what is the effort to add it, and how load-bearing is it in
   the original game?

Effort estimates assume the wrappers live in existing `lua_*.cc` files; they
do not include C++ subsystem work unless noted. "Critical" means the game
cannot boot without it; "important" means the game runs but a significant
feature is missing; "nice" means aesthetics or polish.

## Game summaries

**BYTEPATH** is a twin-stick shmup/RPG hybrid. Its Love2D surface is
medium-sized but unusually *shader-heavy*: 7 fullscreen canvases driving a
post-processing chain (RGB shift, distortion, bloom, grain, shockwave), 8
custom shaders, and an in-game passive skill tree drawn with primitive lines
and text. It uses **Windfield** (a Love2D Box2D wrapper) for all physics and
**HC** (a spatial-hash library) for non-physics overlap queries. Saves use
`bitser` plus `love.filesystem.setIdentity`. A custom `love.run` implements a
fixed-timestep loop at 60Hz.

**SNKRX** is a snake-like auto-battler where the player character is a chain
of revolute-jointed circles. Its physics use is the most advanced of the two:
polygon shapes, edge shapes, chain shapes, revolute joints, and sensor
begin/end callbacks. Its rendering is stencil-heavy (masks for health bars,
damage flashes, arena walls). It also manipulates procedural textures via
`love.image.newImageData` and sets a custom mouse cursor from an in-memory
image. Saves use `binser`. A custom in-house engine layer lives in
`engine/` and largely duplicates Love's modules with extra helpers.

## Per-module gap tables

Legend for **Status**:
- **OK** — direct equivalent exists, usable as-is.
- **Adapt** — adjacent API exists; game code would need minor rewriting.
- **Gap** — no equivalent; engine work required.

### love.graphics

| Love2D call | Used by | Our equivalent | Status | Notes |
|---|---|---|---|---|
| `newImage`, `newQuad`, `draw` | Both | `G.graphics.draw_sprite`, sprite atlas | OK | Sprites use the asset DB; quads are implicit. |
| `newCanvas`, `setCanvas` | Both (heavily) | `G.graphics.new_canvas`, `set_canvas` | OK | BYTEPATH uses 7 canvases for its post chain; validate that cost is acceptable. |
| `newShader`, `setShader`, `send` | Both (heavily) | `G.graphics.attach_shader` + `send_uniform` | OK | BYTEPATH: 8 shaders; SNKRX: 6. Most ports of these shaders will be 1:1 GLSL. |
| `newFont(path, size)` | Both | `G.graphics.draw_text(font, size, …)` | OK | We already load faces at multiple sizes on demand. |
| `printf` (aligned/wrapped text) | Both | — | **Gap** | BYTEPATH menus and SNKRX tooltips need wrapped/aligned text. Effort: M (word-wrap + alignment pass over existing font rasteriser). |
| `setColor`, `setLineWidth`, `setLineStyle` | Both | `G.graphics.set_color` / `set_line_width` | OK | Line style ("smooth"/"rough") can be ignored for now. |
| `rectangle`, `circle`, `polygon`, `line`, `arc`, `points` | Both | `draw_rect`, `draw_circle`, `draw_line` | Adapt | We lack `polygon` fill/outline, `arc`, and multi-point `points`. Effort: S for each primitive. BYTEPATH's skill tree and SNKRX UI both draw arcs. |
| `push`, `pop`, `translate`, `rotate`, `scale`, `shear` | Both | `G.graphics.push/pop/translate/rotate/scale` | OK | `shear` is rare and skippable. |
| `setScissor` | Both | — | **Gap** | SNKRX uses scissor for clipped UI panels. Effort: S (glScissor passthrough). |
| `stencil`, `setStencilTest` | SNKRX (heavy) | — | **Gap** | SNKRX's damage flashes and arena masks rely on stencils. Effort: M (requires stencil buffer on FBO, stencil op state). |
| `setBlendMode("add"/"alpha"/"multiply"/"subtract")` | Both | `G.graphics.set_blend_mode("add"/"alpha"/"premultiplied")` | Adapt | We're missing "multiply" and "subtract". Effort: S. |
| `newImageData`, `ImageData:setPixel`, `newImage(imagedata)` | SNKRX | — | **Gap** | SNKRX synthesises a mouse cursor and a few particle textures at runtime. Effort: M (requires an `ImageData` Lua object + CPU-side texture upload). |
| `setDefaultFilter`, `Image:setFilter` | Both | pixel filtering is global in our renderer | Adapt | Both games use nearest filter throughout; our default matches. |
| `getWidth`, `getHeight`, `getDimensions` on images | Both | `G.assets.sprite_info` | OK | |
| `captureScreenshot` | BYTEPATH | — | Gap (nice) | Only used for the F12 debug screenshot. Low priority. |

### love.physics

| Love2D call | Used by | Our equivalent | Status | Notes |
|---|---|---|---|---|
| `newWorld(gx, gy, sleep)` | Both (via Windfield) | Fixed world, implicit zero-G with friction joints | Adapt | Covered by the existing [[Physics system expansion]] plan. |
| `newBody("dynamic"/"static"/"kinematic")` | Both | `G.physics.add_box` / `add_circle` only | **Gap** | No kinematic, no arbitrary bodies. |
| `newRectangleShape`, `newCircleShape` | Both | `add_box` / `add_circle` | OK (partial) | But material properties are hardcoded. |
| `newPolygonShape` (convex ≤8 verts) | SNKRX | — | **Gap** | SNKRX uses convex polygons for arena walls. Effort: S once the body/shape split lands. |
| `newChainShape`, `newEdgeShape` | SNKRX | — | **Gap** | SNKRX's arena borders are chain shapes. Effort: S. |
| `Body:setLinearVelocity`, `getLinearVelocity`, `setAngularVelocity` | Both | `linear_velocity`, `set_linear_velocity`, `set_angular_velocity` | OK | (Shipped in the space-garbage work.) |
| `Body:applyForce`, `applyLinearImpulse`, `applyTorque` | Both | `apply_force`, `apply_linear_impulse`, `apply_torque` | OK | |
| `Body:setPosition`, `getPosition`, `getAngle` | Both | `set_position`, `position`, `angle` | OK | |
| `Body:setFixedRotation`, `setLinearDamping`, `setAngularDamping`, `setBullet`, `setGravityScale`, `setMass` | Both | only `set_fixed_rotation` | **Gap** | SNKRX's chained snake needs damping tuning. Effort: S. |
| `Fixture:setCategory`, `setMask`, `setGroupIndex`, `setSensor` | Both (via Windfield `setCollisionClass`) | named category registry; no sensor | Adapt/Gap | We have named categories; sensors are missing. Effort: M. |
| `newRevoluteJoint`, `newDistanceJoint`, `newWeldJoint`, `newRopeJoint`, `newMouseJoint` | SNKRX (revolute), BYTEPATH (distance, rope) | — | **Gap** | Joints are the single largest missing area. Covered by physics expansion phases. |
| World callbacks (`beginContact`, `endContact`, `preSolve`, `postSolve`) | Both | single `set_collision_callback` (begin only) | **Gap** | SNKRX needs `endContact` for sensor exit. `preSolve`/`postSolve` are skippable for these two games. Effort: S for `endContact`, M for pre/postSolve. |
| `World:rayCast`, `queryBoundingBox` | BYTEPATH (target-finding) | `G.collision.raycast` exists, but not against Box2D bodies | Adapt | The games use raycasts on the physics world, not on a separate collision system. Effort: S to expose Box2D's raycast. |

### love.audio / love.sound

| Love2D call | Used by | Our equivalent | Status | Notes |
|---|---|---|---|---|
| `newSource(path, "stream"/"static")` | Both | `G.sound.add_source` | OK | |
| `Source:play`, `stop`, `pause`, `setVolume`, `setPitch`, `setLooping` | Both | `G.sound.play_source`, `stop_source`, `set_volume`, `set_loop` | Adapt | Missing `pause`, `setPitch`. Effort: S. |
| `Source:setPosition`, `setAttenuation` (positional audio) | Not used in either game | — | Gap (nice) | Both games are 2D flat-mix, no positional audio. Skip. |
| `love.audio.setVolume` (master) | Both | — | Gap | BYTEPATH has master volume sliders. Effort: S. |
| Ripple audio library (SNKRX) | SNKRX | — | Adapt | Ripple is a tag-based mixer on top of love.audio. SNKRX uses it for music ducking and tag-based volume. Would need to be reimplemented in Lua against our API. Not an engine change. |

### love.filesystem

| Love2D call | Used by | Our equivalent | Status | Notes |
|---|---|---|---|---|
| `setIdentity`, `getSaveDirectory` | Both | — | **Gap** | Both games persist save data. This is the motivation for [[Save and persistence]]. Effort: covered there. |
| `write`, `read`, `getInfo`, `remove` | Both | `G.filesystem.*` read-only | **Gap** | We can read assets but not write. Effort: S on top of the save-dir work. |
| `load` (load and run a Lua file) | BYTEPATH skill tree | `loadstring` / `dofile` via the asset DB | OK | |
| `createDirectory` | BYTEPATH | — | Gap | One-liner on top of save dir. Effort: S. |

### love.keyboard / love.mouse / love.joystick

| Love2D call | Used by | Our equivalent | Status | Notes |
|---|---|---|---|---|
| `isDown`, `keypressed`, `keyreleased` | Both | `G.input.is_key_down`, `is_key_pressed` | OK | |
| `textinput` (for name entry) | BYTEPATH | — | Gap | Text input for save slot names. Effort: S (SDL_TEXTINPUT passthrough). |
| Mouse position, buttons, wheel | Both | `G.input.mouse_position`, `mouse_wheel`, `is_mouse_pressed` | OK | |
| `love.mouse.setCursor`, `newCursor` | SNKRX | — | **Gap** | SNKRX swaps cursor to a custom crosshair. Depends on `ImageData` gap. Effort: S once ImageData exists. |
| `love.mouse.setVisible` | Both | — | Gap (nice) | Effort: S. |
| `love.joystick.*`, gamepad rebinding | BYTEPATH | — | Gap | Neither game is primarily gamepad-driven; BYTEPATH supports it. Skippable. Effort: L if ever needed. |

### love.window

| Love2D call | Used by | Our equivalent | Status | Notes |
|---|---|---|---|---|
| `setMode`, `setFullscreen`, `setTitle`, `getDimensions` | Both | `G.window.*` | OK | |
| `showMessageBox` | BYTEPATH (save corruption) | — | Gap (nice) | Could just log. Skip. |
| `setIcon` | Both | — | Gap (nice) | Effort: S. |

### love.timer / love.math

| Love2D call | Used by | Our equivalent | Status | Notes |
|---|---|---|---|---|
| `getTime`, `getFPS`, `getDelta` | Both | `G.clock.*` | OK | |
| `love.math.random`, `newRandomGenerator`, `setRandomSeed` | Both | `G.random.*` | OK | |
| `love.math.noise` (Perlin/Simplex) | BYTEPATH (shader backgrounds) | — | Gap | Shader backgrounds could be baked; code paths that call it at runtime are few. Effort: S. |

### Third-party Lua libraries

Neither game uses these libraries through Love2D — they are pure Lua and
ship in the game repo — but they shape the expected API style:

| Library | Used by | Purpose | Porting note |
|---|---|---|---|
| Windfield | BYTEPATH | Box2D wrapper with named collision classes | Our `G.physics` already uses named categories, so idioms carry over. |
| HC (Hardon Collider) | BYTEPATH | Spatial-hash, non-physics overlap queries | `G.collision` covers the same use case. |
| classic | BYTEPATH | Tiny OOP | Ships verbatim in space-garbage already. |
| hump.camera, hump.timer | Both | Camera + tweens/after | `G.camera`, `G.timer` cover this. |
| boipushy | BYTEPATH | Input rebinding wrapper | Pure Lua, ports as-is. |
| bitser / binser | BYTEPATH / SNKRX | Save serialization | Both are pure Lua; would need `love.filesystem.write`. |
| ripple | SNKRX | Tag-based audio mixer | Pure Lua on top of love.audio; ports once pitch/pause land. |

## Prioritization

The table below groups gaps by (importance × effort). Effort is rough:
**S** = hours, **M** = a day, **L** = multiple days, **XL** = weeks (crosses
into a separate design doc).

### Tier 1 — Critical, low effort (do first)

| Gap | Effort | Justification |
|---|---|---|
| Scissor rect (`setScissor`) | S | SNKRX UI, BYTEPATH menus. Direct `glScissor` passthrough. |
| `endContact` callback | S | SNKRX sensors leak without this; also a correctness bug we already want fixed. |
| Blend modes: multiply, subtract | S | Both games use them for damage flashes and backgrounds. |
| `Body:setLinearDamping` / `setAngularDamping` / `setGravityScale` / `setMass` | S | Tiny passthroughs; unblocks the snake chain in SNKRX and BYTEPATH ship handling. |
| Polygon, chain, edge shapes | S (each) | Additive once the body/shape split from [[Physics system expansion]] Phase 1 lands. |
| Raycast against physics world | S | Target-finding in BYTEPATH. We already have raycast in `G.collision`; this is an additional Box2D backend. |
| `Source:pause`, `setPitch` | S | BYTEPATH ducks music on pause; SNKRX pitches SFX. |
| Master audio volume | S | Options menu staple. |

### Tier 2 — Critical, medium effort

| Gap | Effort | Justification |
|---|---|---|
| Save directory + `filesystem.write`/`read`/`remove` | M | Prerequisite for any persistent game. Tracked by [[Save and persistence]]. |
| Joint types: revolute, distance, weld, rope, mouse | M–L | SNKRX snake is revolute-only, but the physics doc plan ships the whole set together. |
| Sensors (non-colliding fixtures) | M | SNKRX pickups, BYTEPATH aggro zones. |
| Fixture collision filter per-shape | M | Our category registry is per-body; games set per-fixture filters for compound bodies. |
| `love.graphics.printf` (wrap + align) | M | BYTEPATH skill tree tooltips and SNKRX cards are unreadable without it. |
| Stencil buffer + `setStencilTest` | M | SNKRX damage masks. Without stencils the visual identity changes significantly, though gameplay survives. |
| `love.graphics.polygon`, `arc`, `points` | S×3 | Skill tree and UI drawing. |

### Tier 3 — Important, medium effort

| Gap | Effort | Justification |
|---|---|---|
| `ImageData` object + CPU texture upload | M | Needed for `setCursor` from image and procedural particle textures. Not critical — a static PNG cursor would also work. |
| `setCursor` / `setVisible` | S | Depends on ImageData unless we accept a pre-baked cursor asset. |
| `textinput` event | S | Name entry for saves. Fall back to hardcoded name if skipped. |
| `love.math.noise` | S | Could be baked at asset-pipeline time. |
| `createDirectory` | S | Trivial after save-dir work. |

### Tier 4 — Skip or defer

- Joystick / gamepad rebinding (L, both games playable on KB+M).
- `showMessageBox`, `setIcon`, `captureScreenshot` (cosmetic).
- Positional audio (unused).
- Shear transform (unused).
- Full manifold access in contact callbacks (first-point is enough).

## Recommended implementation order

Assuming we want to unblock *both* games in roughly the right order:

1. **Physics expansion Phase 1** (body/shape split, material properties,
   polygon/edge/chain, damping/mass, world config). Covered by
   [[Physics system expansion]]. This is the single biggest prerequisite.
2. **Physics expansion Phase 2** (joint types, sensors, `endContact`,
   per-fixture filters). Unblocks SNKRX's snake and BYTEPATH's tethered
   attacks.
3. **Save persistence** (save dir + `filesystem.write`). Required for any
   real run of either game. Covered by [[Save and persistence]].
4. **Rendering gap-fill**: scissor, polygon/arc/points primitives, stencil,
   `printf`, remaining blend modes. These are all independent and can land
   in any order; SNKRX's stencil work is the one that unlocks the most
   visible polish.
5. **Audio polish**: pause, pitch, master volume.
6. **Input polish**: `textinput`, cursor set/hide.
7. **`ImageData` + procedural textures.** Lowest priority — neither game
   would *break* with static cursor assets.

After steps 1–3, BYTEPATH is realistically portable with the remaining work
confined to Lua-level reimplementations of bitser and boipushy. SNKRX needs
step 4 (stencils in particular) to match its intended look.

## Out of scope

- Porting either game 1:1 to our asset pipeline. The asset DB would need the
  upstream PNGs re-imported through `packer`, and shaders would need GLSL
  dialect fixes.
- Windfield/HC/ripple API parity. We expect Lua-level shims, not C++ ports.
- Matching Love2D's exact event/callback shape. Our existing update/draw
  pattern is the target idiom.

## References

- `workbench/BYTEPATH/` — upstream source.
- `workbench/SNKRX/` — upstream source.
- [[Physics system expansion]] — all physics gaps route through here.
- [[Save and persistence]] — filesystem write + save dir.
- [[Engine comparison]] — broader feature comparison against Love2D et al.
- [[Shader API Redesign]] — shader authoring path, relevant to BYTEPATH's
  8-shader post chain.
