---
status: implemented
tags: [reference, comparison]
---

# Engine Comparison: Feature Gap Analysis

This document compares our engine against five reference 2D game engines — **Love2D**, **high_impact**, **Anchor**, **Carimbo**, and **Raylib** — to identify missing features needed to ship complete videogames.

## Engine profiles

| | Our engine | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| **Language** | C++17 core, Lua 5.1 scripting | C++ core, Lua (LuaJIT) scripting | Pure C99, user code in C | C99 core, Lua 5.4 scripting | C++23 core, Lua scripting (sol2) | Pure C99, no built-in scripting |
| **Architecture** | Module-based, explicit allocators | Module-based | Flat C with X-macro entities | Object tree + mixins | ECS (EnTT) with declarative JSON scenes | Flat C procedural API (rlgl low-level layer) |
| **Size** | ~21,500 LOC | Large (~200K+ LOC) | ~4,800 LOC | ~10,000 LOC (C) + Lua framework | Small core, heavy dep tree (boost, EnTT, sol2, SDL, miniaudio, Box2D, PhysFS, yyjson, stb) | ~60,000 LOC |
| **Renderer** | OpenGL 3.3+, SDL2 | OpenGL/ES, SDL2 | OpenGL/Metal/Software, SDL2 or Sokol | OpenGL 3.3/WebGL 2, SDL2 | SDL_Renderer (SDL3) | OpenGL 1.1/2.1/3.3/ES2/ES3 via `rlgl` |
| **Physics** | Box2D | Box2D | Custom AABB + slopes | Box2D 3.x | Box2D (per-scene world) with raycast + category bitmasks | None in core (optional `physac` single-header) |
| **Audio** | SDL2 callback, dr_wav + stb_vorbis | OpenAL Soft | QOA + pl_synth | miniaudio + Vorbis | miniaudio + Opus | `raudio` (miniaudio) — WAV/OGG/MP3/FLAC/XM/MOD/QOA |
| **Asset format** | SQLite DB + hot-reload | PhysFS (zip-based) | QOI/QOA files | Files or zip appended to exe | PhysFS-backed `cartridge.rom` zip | Plain files (optional `rres` packer) |
| **Platforms** | Linux (Windows stubbed) | Win/Mac/Linux/Android/iOS | Win/Mac/Linux/Web (WASM) | Win/Mac/Linux/Web (WASM) | Linux/Win/Mac/Web (WASM-first), Android/iOS partial | Win/Mac/Linux/Web/Android/Raspberry Pi/DRM |
| **License** | Proprietary | zlib/libpng | MIT | MIT | Custom permissive (attribution) | zlib/libpng |

## Feature-by-feature comparison

### 1. Rendering

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Sprite/image drawing | Yes | Yes | Yes | Yes | Yes | Yes |
| Sprite atlas / spritesheet | Yes (JSON/XML) | Yes (Quad-based) | Yes (tileset) | Yes (spritesheet) | Yes (JSON object descriptors) | No built-in (manual source rects) |
| Batch rendering | Yes (command buffer) | Yes (auto + SpriteBatch) | Yes (texture atlas batching) | Yes (deferred command queue) | SDL_Renderer-driven | Yes (auto via `rlgl`) |
| Transform stack | Yes (push/pop, mat4) | Yes (push/pop) | Yes (push/pop, mat3) | Yes (push/pop) | No (scene-graph z-order) | Yes (`rlPushMatrix`/`rlPopMatrix`) |
| Filled rectangles | Yes | Yes | Yes (via draw) | Yes | Limited | Yes |
| Circles | Yes (fill only) | Yes (fill + outline) | No built-in | Yes (fill + outline) | No | Yes (fill + outline + sector) |
| Triangles | Yes | Yes (polygon) | No built-in | Yes (fill + outline) | No | Yes (fill + outline + fan + strip) |
| Lines / polylines | Yes (configurable width) | Yes (join styles, width) | No built-in | Yes (capsule lines) | No | Yes (width, bezier, curves) |
| Ellipses | No | Yes | No | No | No | **Yes** |
| Arcs | No | Yes | No | No | No | **Yes** (sector, ring) |
| Rounded rectangles | No | Yes | No | Yes | No | **Yes** |
| Arbitrary polygons | No | Yes | No | Yes (up to 8 verts) | No | **Yes** (regular + lines) |
| Points | No | Yes | No | No | No | Yes |
| Gradient fills | No | No | No | Yes (H/V gradients) | No | Yes (H/V + 4-corner) |
| Splines / Bezier curves | No | Yes (Bezier) | No | No | No | **Yes** (linear, basis, Catmull-Rom, quad/cubic Bezier) |
| Custom shaders | Yes (GLSL) | Yes (GLSL dialect) | Yes (GLSL) | Yes (fragment only) | No (SDL_Renderer) | Yes (GLSL, vertex + fragment) |
| Post-processing effects | Yes (Canvas + Shader) | Yes (via Canvas + Shader) | Yes (CRT effect built-in) | Yes (per-layer shaders) | No | Yes (RenderTexture + shader) |
| Canvas / off-screen render | Yes (non-MSAA FBO) | Yes (Canvas, MRT) | No | Yes (FBO layers) | No | Yes (`RenderTexture2D`) |
| Blend modes | Yes (alpha/add/multiply/replace) | Yes (8+ modes) | Yes (Normal, Additive) | Yes (Alpha, Additive) | Basic (SDL blend modes) | Yes (alpha/add/multiply/sub/custom) |
| Stencil masking | No | Yes | No | Yes | No | Yes (via `rlgl`) |
| MSAA | Yes | Yes | No | No | No | Yes (`FLAG_MSAA_4X_HINT`) |
| Mesh / custom geometry | No | Yes (Mesh object) | No | No | No | Yes (`Mesh`, VAO-backed) |
| Pixel-perfect scaling | Yes (canvas + nearest filter) | No | Yes (discrete scaling) | Yes (rough filter mode) | Yes (`with_scale` factor) | Manual (RenderTexture pattern) |
| Color tinting per draw | Yes | Yes | Yes | Yes | Yes (alpha) | Yes |
| Screenshots | Yes (PNG) | Yes (async) | No | No | No | Yes (`TakeScreenshot`) |
| HiDPI support | No | Yes | No | No | Auto (WASM canvas scaling) | Yes (`FLAG_WINDOW_HIGHDPI`) |
| Mipmaps | No | Yes | Yes (optional) | No | No | Yes (`GenTextureMipmaps`) |

**Status**: Canvas, blend modes, and pixel-perfect scaling are now implemented. Our canvas handles premultiplied alpha automatically (avoiding Love2D's biggest footgun) and Y-flip transparently (avoiding Raylib's annoyance — Raylib's `RenderTexture2D` requires a manual Y-flip when drawing back to the screen). Remaining gaps: stencil masking for fog-of-war/UI clipping, outlined shape drawing modes, and more blend modes (Love2D has 8+, we have 4).

**Raylib is the primitive-drawing leader** of any engine in this comparison — it's the only one with built-in ellipses, arcs/sectors, rings, and spline curves (linear, basis, Catmull-Rom, quadratic/cubic Bezier). If we want to close the Tier 3 "more drawing primitives" gap, Raylib's `rshapes.c` is the reference implementation worth studying.

**Carimbo stays minimal** on the primitive front — it's built around sprite rendering via SDL_Renderer, with shape drawing essentially not exposed to Lua. The design choice reflects its scope: declarative sprite-based games with animation timelines, not arbitrary procedural graphics.

**Detailed canvas comparison**:

| Capability | Ours | Love2D | Anchor | Raylib | Notes |
|---|---|---|---|---|---|
| Create render target | `new_canvas(w,h)` | `newCanvas(w,h,{...})` | `an:layer(name)` | `LoadRenderTexture(w,h)` | Love2D has most options (format, MSAA) |
| Redirect drawing | `set_canvas(c)` / `set_canvas()` | `setCanvas(c)` / `setCanvas()` | Implicit per-layer | `BeginTextureMode(t)` / `EndTextureMode()` | Raylib uses begin/end scoping instead of get/set |
| Draw as texture | `draw_canvas(c,x,y)` | `draw(canvas,x,y)` | Layer compositing | `DrawTextureRec` with flipped Y | Raylib requires manual Y-flip |
| Y-flip handling | Automatic (UV inversion) | Automatic | Automatic | **Manual** (user must flip source rect) | We and Love2D/Anchor avoid Raylib's most-complained-about gotcha |
| Premultiplied alpha | Automatic | Manual (footgun!) | Automatic | Manual | Our biggest UX win vs Love2D and Raylib |
| Filter modes | nearest / linear | nearest / linear + aniso | N/A | nearest / bilinear / trilinear / aniso | Parity with Love2D basics |
| Pixel formats | RGBA8 only | 20+ including HDR | RGBA8 | 20+ including HDR / compressed | Love2D and Raylib far ahead; RGBA8 sufficient for 2D |
| MRT (multi-target) | No | Yes (up to 4) | No | No | Needed for deferred lighting, not common in 2D |
| Per-canvas MSAA | No (non-MSAA only) | Yes | No | No | By design: pixel art and post-FX don't want MSAA |
| Scoped helper | Not yet | `renderTo(fn)` | N/A | `BeginTextureMode`/`EndTextureMode` | Easy to add in Lua |
| Draw with scaling | Yes (w,h params) | Via transform | Via transform | Via `DrawTexturePro` | Convenience over push/scale/pop |
| Blend modes | 4 (alpha/add/multiply/replace) | 8+ (+ premultiplied variants) | 2 (alpha/additive) | 5 (alpha/add/multiply/sub/custom) | We match Anchor and Raylib; Love2D has darken/lighten/screen extras |
| Clear with color | Yes `clear(r,g,b,a)` | Yes `clear(r,g,b,a)` | N/A | `ClearBackground(color)` | Parity with Love2D |
| Stencil on canvas | Not yet | Yes | Yes | Via `rlgl` low-level | Future work |
| Nesting | Yes (reset to screen) | Yes (reset to screen) | N/A | Yes (LIFO begin/end) | Same behavior as Love2D |

Carimbo is omitted from the canvas comparison because it does not expose off-screen render targets to scripts.

### 2. Animation

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Frame-based sprite animation | No (logic in Lua) | No built-in (libraries) | Yes (`anim_def_t`) | Yes (spritesheet animation) | **Yes** (JSON timelines, named `action` per entity) | No built-in (manual rect stepping) |
| Animation sequences | No | No built-in | Yes (arbitrary frame order) | Yes (frame indices) | **Yes** (arbitrary frame order + per-frame duration) | No |
| Loop / once / bounce modes | No | No built-in | Yes (loop + stop) | Yes (loop, once, bounce) | **Yes** (loop + oneshot) | No |
| Flip X/Y | No | No built-in | Yes | No built-in | **Yes** (`Flip.horizontal/vertical/both`) | Via negative source rect |
| Per-frame callbacks | No | No built-in | No | Yes | `on_begin` / `on_end` (timeline start/finish) | No |
| Animation rewind/goto | No | No built-in | Yes | Yes (`set_frame`, reset) | Implicit (reassign `action`) | N/A |
| Appear/disappear events | No | No | No | No | **Yes** (`on_appear`/`on_disappear` when switching to a timeline with/without frames) | No |

**Gaps to fill**: A built-in animation system is highly valuable. All action-oriented engines (high_impact, Anchor, Carimbo) include one; only Love2D and Raylib leave it to the user. Carimbo's model is especially interesting — animations are declared in per-object JSON, the entity holds a current `action` string, and switching the string re-triggers the timeline with `on_begin`/`on_end`/`on_appear`/`on_disappear` callbacks. That's a nice scripting ergonomics win over the more imperative `play(name)` / `is_finished()` APIs in high_impact and Anchor.

At minimum for us: define a sequence of frames from a spritesheet, specify frame timing, support loop/once modes, flip X/Y, and fire a callback on completion for oneshots. Carimbo's `action`-as-string pattern is worth considering for the Lua binding shape.

### 3. Camera

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Camera abstraction | Yes | No built-in (Transform) | Yes (`camera_t`) | Yes (camera object) | Tilemap-scene `on_camera(dt) -> Quad` callback | **Yes** (`Camera2D` struct: target/offset/rotation/zoom) |
| Follow target entity | Yes | No built-in | Yes (with snap) | Yes (lerp follow) | Manual (script returns viewport) | Manual (`camera.target = entity.pos`) |
| Smooth interpolation | Yes | No built-in | Yes (speed-based) | Yes (exponential lerp) | Manual | Manual |
| Dead zone | Yes | No built-in | Yes | No | Manual | No |
| Look-ahead | No | No built-in | Yes | No | Manual | No |
| Bounds clamping | Yes | No built-in | Yes (map bounds) | Yes (`set_bounds`) | Manual | No |
| Camera shake | Yes | No built-in | No | Yes (trauma, push, sine, square) | No | No |
| Zoom | Yes | No built-in | No | Yes | No | **Yes** (`camera.zoom`) |
| Rotation | Yes | Via transform | No | No | No | **Yes** (`camera.rotation`) |
| World/screen coord conversion | Yes | Yes (Transform) | No | Yes (`to_world` / `to_screen`) | No | **Yes** (`GetScreenToWorld2D` / `GetWorldToScreen2D`) |
| Begin/end scoping | Via push/pop | Via push/pop | Implicit | Implicit | Implicit | `BeginMode2D(cam)` / `EndMode2D()` |
| Parallax layers | Yes | No built-in | Yes (distance factor) | No | Per-layer offset factor in scene JSON | Manual |
| Handcam (subtle drift) | No | No built-in | No | Yes | No | No |

**Status**: Camera is now implemented (follow, deadzone, bounds, shake, zoom, parallax). The remaining gaps relative to other engines are niche: Anchor's handcam drift effect, high_impact's look-ahead, and more expressive screen-shake shapes.

**Raylib's `Camera2D` is the simplest design worth studying** — it's a plain struct with four fields (`target`, `offset`, `rotation`, `zoom`), scoped via `BeginMode2D`/`EndMode2D`, and paired with two pure-function coordinate converters. Everything else (follow, shake, bounds) is left to user code. That's a sharp contrast with Anchor's object-oriented camera and a reminder that the core primitive is small.

**Carimbo's approach is unusual**: the scene script owns the camera by implementing `on_camera(delta) -> Quad`, returning the viewport each frame. This means any follow/shake/bounds logic lives in Lua, and only tilemap scenes get cameras at all. Elegant for pure tilemap games, less flexible for mixed-content scenes.

### 4. Tilemap

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Tile map rendering | No | No built-in (STI library) | Yes (`map_t`) | No | Yes (JSON tilemap + atlas PNG) | No |
| Parallax scrolling | No | No built-in | Yes (distance factor) | No | Yes (layer offset factors) | No |
| Tile collision | No | No built-in | Yes (54 slope types!) | No | Box2D bodies from tile data | No |
| Animated tiles | No | No built-in | Yes | No | Unknown | No |
| Multiple layers | No | No built-in | Yes (4 bg + collision) | No | Yes | No |
| Foreground / background | No | No built-in | Yes | No | Yes (layer ordering) | No |
| Tile repeat / wrap | No | No built-in | Yes | No | Unknown | No |
| Level editor | No | No built-in | Yes (Weltmeister) | No | No | No |
| Level loading from JSON | No | No built-in | Yes | No | Yes (`tilemaps/<name>.json`) | No |

**Gaps to fill**: high_impact is still the clear leader here, now with Carimbo as a lighter-weight precedent. A tilemap system with collision detection is essential for platformers, top-down RPGs, and many other genres. high_impact's slope system (54 tile types) remains a standout feature. At minimum: load tile data, render with camera offset, query tiles at positions, and detect tile collisions.

Raylib has no tilemap support — the community ports Tiled / raylib-extras plugins. Love2D and Anchor both delegate to external libraries (STI for Love2D).

### 5. Entity / Game Object System

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Entity framework | No | No | Yes (`entity_t` + vtab) | Yes (object tree + mixins) | Yes (EnTT ECS, accessed via `pool["name"]`) | No |
| Entity types / classes | No | No | Yes (X-macro compile-time) | Yes (class inheritance) | Yes (per-object Lua module + JSON) | No |
| Entity groups / tags | No | No | Yes (bitmask groups) | Yes (tag system) | Yes (`kind` string + physics category bitmasks) | No |
| Entity queries | No | No | Yes (by type, name, proximity, location) | Yes (by tag) | By name via `pool`, by physics category via raycast | No |
| Entity references (safe) | No | No | Yes (survives death) | Yes (link system) | Yes (`entity.alive` check) | No |
| Draw order sorting | No | No | Yes (`draw_order` field) | No (FIFO call order) | Yes (`z` property per entity) | No |
| Collision groups | No | No | Yes (`check_against` bitmask) | Yes (tag-based filtering) | Yes (Box2D categories) | No |
| Kill / cleanup | No | No | Yes (auto on health <= 0) | Yes (deferred cleanup) | Yes (`entity:die()`) | No |
| Spawn from level data | No | No | Yes (JSON entity settings) | No | Yes (scene JSON) | No |
| Entity cloning | No | No | No | Yes | Yes (`entity:clone()` with fresh script env) | No |

**Position**: We intentionally leave entity management to Lua, and this decision stands — Carimbo's ECS is a genuinely different architectural style (declarative JSON + per-object Lua modules + EnTT in C++) that doesn't map cleanly onto our "engine as toolbox" philosophy. Worth noting as a contrast point, not a gap to chase. If we ever want to reduce Lua boilerplate, the lightest-weight borrowing would be tagging/grouping for collision filtering and draw-order sorting, neither of which requires a full ECS.

### 6. Physics & Collision

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Rigid body dynamics | Yes (Box2D) | Yes (Box2D) | Yes (custom) | Yes (Box2D 3.x) | Yes (Box2D per scene) | No core (optional `physac`) |
| Static bodies | Yes | Yes | Yes | Yes | Yes | physac: Yes |
| Dynamic bodies | Yes (box, circle) | Yes (all shapes) | Yes (AABB only) | Yes (circle, box, capsule, polygon) | Yes (Box2D shapes) | physac: polygons |
| Kinematic bodies | No | Yes | No | Yes | Yes (via velocity) | No |
| Collision callbacks | Yes (begin/end) | Yes (begin/end/pre/post) | Yes (collide, touch) | Yes (begin/end, sensor, hit) | Yes (`on_collision` / `on_collision_end`) | No |
| Sensors / triggers | No | Yes | Yes (touch groups) | Yes | Yes (via `trigger` category) | No |
| Joints | No | Yes (12 types) | No | No | Unknown | No |
| Raycasting | No | Yes | No | Yes | **Yes** (`world.raycast(origin, angle, distance, mask)`) | No |
| Spatial queries | No | Yes (AABB query) | No | Yes (point, circle, AABB, box, capsule, polygon, ray) | Raycast only | No |
| Slope collision | No | No | Yes (54 tile slopes) | No | No | No |
| One-way platforms | No | No | Yes | No | Unknown | No |
| Edge / chain shapes | No | Yes | No | No | Unknown | No |
| Mass-based resolution | No | Yes (Box2D) | Yes (custom) | Yes (Box2D) | Yes (Box2D) | physac: Yes |
| Gravity per entity | No | Yes (Body) | Yes (gravity multiplier) | Yes (gravity scale) | Yes (per scene world) | No |
| Bullet mode | No | Yes | No | Yes | Yes (Box2D feature) | No |
| Capsule shape | No | No | No | Yes | No | No |
| Polygon shape | No | Yes (8 verts) | No | Yes (8 verts) | Yes (Box2D) | physac: Yes |
| Category bitmask filtering | No | Yes | Yes | Yes | **Yes** (`PhysicsCategory.player`/`enemy`/`projectile`/`terrain`/`trigger`/`collectible`/`interface`) | No |

**Gaps to fill**: Our Box2D integration is minimal. Missing critical features: sensors (for trigger zones, item pickups), raycasting (for line-of-sight, laser beams), spatial queries (for area effects, AI detection), kinematic bodies (for moving platforms), and joints (for ropes, chains, vehicles). Capsule shapes are great for character controllers.

Carimbo's **per-scene world** is a useful pattern — each scene has its own Box2D world, destroyed on scene transition, which keeps bodies from leaking across menu → gameplay boundaries. Its predefined `PhysicsCategory` enum (player/enemy/projectile/terrain/trigger/collectible/interface) is also worth borrowing as a default filter set rather than making every game define its own bitmask layout.

### 7. Input

| Feature                             | Ours | Love2D | high_impact   | Anchor | Carimbo | Raylib |
| ----------------------------------- | ---- | ------ | ------------- | ------ | ------- | ------ |
| Keyboard press/hold/release         | Yes  | Yes    | Yes           | Yes    | Yes     | Yes    |
| Mouse position                      | Yes  | Yes    | Yes           | Yes    | Yes (logical coords) | Yes |
| Mouse buttons                       | Yes  | Yes    | Yes           | Yes    | Yes     | Yes    |
| Mouse wheel                         | Yes  | Yes    | No            | Yes    | Unknown | Yes    |
| Gamepad buttons                     | Yes  | Yes    | Yes           | Yes    | Yes (4 slots) | Yes |
| Gamepad analog axes                 | Yes  | Yes    | Yes           | Yes    | Yes     | Yes    |
| Action binding (abstract)           | No   | No     | Yes           | Yes    | No      | No     |
| Text input                          | Yes  | Yes    | Yes (capture) | No     | Yes (`on_text`) | Yes |
| Touch input                         | No   | Yes    | No            | No     | Unknown | Yes (multi-touch) |
| Gestures (tap/swipe/pinch)          | No   | No     | No            | No     | No      | **Yes** (`GetGestureDetected`) |
| Input chords / sequences            | No   | No     | No            | Yes    | No      | No     |
| Hold duration detection             | No   | No     | No            | Yes    | No      | No     |
| Mouse grab / relative mode          | No   | Yes    | No            | Yes    | No      | Yes (`DisableCursor`) |
| Custom cursor                       | No   | Yes    | No            | No     | Yes (animated via `cursors/*.json`) | Yes (from image) |
| Mouse world position (camera-aware) | No   | No     | No            | Yes    | No      | Yes (`GetScreenToWorld2D`) |
| File drop                           | No   | Yes    | No            | No     | No      | Yes |
| Input record / replay               | No   | No     | No            | No     | No      | **Yes** (automation events) |

**Gaps to fill**: Action binding is the key missing feature — it abstracts physical buttons into game actions ("jump", "shoot") allowing easy rebinding and multi-device support. Mouse world position (accounting for camera transform) is a must-have for any game with a camera.

Raylib brings two unique features: **gesture recognition** (tap/doubletap/hold/drag/swipe/pinch) which only matters if we target touch, and **input automation events** — a record/replay system that captures input streams to a file and replays them deterministically. That's a genuinely interesting feature for automated testing and would pair well with our existing Test input system design.

### 8. Audio

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Sound effect playback | Yes (WAV, OGG) | Yes (many formats) | Yes (QOA) | Yes (OGG) | Yes (Opus) | Yes (many formats) |
| Music / streaming | Yes (streaming) | Yes (stream source) | Yes (QOA streaming) | Yes (multi-channel) | Yes (via miniaudio) | Yes (`Music` streams) |
| Per-source volume | Yes | Yes | Yes | Yes | Yes | Yes |
| Global volume | Yes | Yes | Yes | Yes | Yes | Yes (`SetMasterVolume`) |
| Pitch control | Yes | Yes | Yes | Yes | Unknown | Yes |
| Panning (stereo) | Yes | Yes (3D position) | Yes (pan control) | No | Unknown | Yes |
| Looping | Yes | Yes | Yes | Yes | Yes | Yes |
| 3D spatialized audio | No | Yes (OpenAL) | No | No | No | No (miniaudio capable but not exposed) |
| Audio effects (reverb, etc.) | No | Yes (8 effect types) | No | No | No | Yes (processor callbacks) |
| Procedural / synth audio | No | No | Yes (pl_synth) | No | No | Yes (`AudioStream` callback) |
| Microphone recording | No | Yes | No | No | No | No |
| Playlist system | No | No | No | Yes (crossfade, shuffle) | No | No |
| MP3 / FLAC / tracker formats | No | Yes | No | No | No | **Yes** (WAV/OGG/MP3/FLAC/XM/MOD/QOA) |
| Pause / resume individual | Yes | Yes | Yes | Yes | Yes | Yes |
| Seek / tell | No | Yes | No | Yes | Unknown | Yes |

**Status**: Pitch, looping, panning, and per-source pause/resume are now implemented. Remaining gaps: playlist system (nice-to-have), seek/tell, 3D spatialized audio.

Raylib's `raudio` has the broadest format support of any comparison engine (XM/MOD tracker formats in addition to the usual suspects). It also exposes a raw `AudioStream` callback API for procedural audio (synths, chiptune, voxel engines), which is something no one else in the comparison does except high_impact's pl_synth.

### 9. Timer / Tween / Easing

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Delta time | Yes | Yes | Yes | Yes | Yes (`on_loop(delta)`) | Yes (`GetFrameTime`) |
| Game vs real time | Yes (pause support) | No built-in | Yes (`time_scale`) | Yes (`time_scale`) | Separate tick system (`on_tick`) | No |
| Time scale (slow-mo) | Yes | No built-in | Yes | Yes | Unknown | No |
| Delayed callbacks | Yes | No built-in | No | Yes (`timer:after`) | `ticker.wrap` (tick-based) | No |
| Repeating timers | Yes | No built-in | No | Yes (`timer:every`) | `ticker.wrap` | No |
| Duration callbacks | Yes | No built-in | No | Yes (`timer:during`) | No | No |
| Tweening | Yes | No built-in | No | Yes (`timer:tween`) | `Vec2.lerp` primitive only | Via `raymath` easings |
| Easing functions | Yes | No built-in | No | Yes (20+ curves) | No | Yes (`raymath` easings: linear/sine/circ/cubic/quad/expo/back/bounce/elastic) |
| Cooldowns | Yes | No built-in | No | Yes (`timer:cooldown`) | No | No |
| Spring physics | Yes | No built-in | No | Yes (damped oscillator) | No | No |

**Status**: Our timer/tween system is implemented and matches Anchor's capabilities closely. Carimbo takes a different approach with a dedicated tick rate separate from the frame loop (`with_ticks(10)` → `on_tick(tick)` fires at fixed rate regardless of frame rate), which is useful for game logic that shouldn't run every frame.

### 10. Math & Utilities

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| 2D vectors | Yes (`FVec2`, etc.) | No (use tables) | Yes (`vec2_t`) | No (use x,y args) | **Yes** (`Vec2` usertype with full API) | Yes (`Vector2` + `raymath`) |
| 3D/4D vectors | Yes | No | No | No | `Vec3` (limited) | Yes (`Vector3`/`Vector4`) |
| Matrices (2x2, 3x3, 4x4) | Yes | No (Transform object) | Yes (mat3) | No | No | Yes (`Matrix` 4x4) |
| Quaternions | No | No | No | No | No | Yes (`Quaternion`) |
| Dot product | Yes | No built-in | Yes | No | Yes (`Vec2.dot`) | Yes |
| Normalize | Yes | No built-in | Yes | Yes (function) | Yes (`Vec2.normalize`) | Yes |
| Distance | No | No built-in | Yes (`entity_dist`) | Yes (function) | Yes (`Vec2.distance`, `distance_squared`) | Yes |
| Angle between points | No | No built-in | Yes (`entity_angle`) | Yes (function) | Yes (`Vec2.angle_between`) | Yes |
| Lerp | No | No built-in | No | Yes (+ framerate-independent) | Yes (`Vec2.lerp`) | Yes (`Lerp`) |
| Clamp | Yes | No built-in | No | Yes | Yes (`Vec2.clamp`) | Yes |
| Snap to grid | No | No built-in | No | Yes | No | No |
| Sign | No | No built-in | No | Yes | No | Yes |
| Direction from angle | No | No built-in | No | Yes | Yes (`Vec2.rotate`) | Yes |
| Reflect | No | No built-in | No | Yes | Yes (`Vec2.reflect`) | Yes |
| Perpendicular | No | No | No | No | Yes (`Vec2.perpendicular`) | No |
| Project onto vector | No | No | No | No | Yes (`Vec2.project`) | No |
| Perlin / simplex noise | No | Yes (1-4D) | Yes (2D) | Yes | No | Yes (image-gen perlin) |
| Bezier curves | No | Yes | No | No | No | Yes |
| Triangulation | No | Yes | No | No | No | No |
| Seeded RNG | Yes (PCG) | Yes (platform-independent) | No built-in | Yes (PCG32) | Yes (xorshift128+) | Yes |
| Random from distribution | No | Yes (normal) | No | Yes (normal) | No | No |
| Weighted random | No | No built-in | No | Yes | No | No |
| Array utilities | No | No built-in | No | Yes (30+ functions) | No | No |
| Easing curves as functions | No | No | No | Yes (via tween) | No | Yes (`raymath` has 40+) |

**Gaps to fill**: Lerp (especially framerate-independent) is used everywhere. Distance, angle-between-points, and direction-from-angle are basics for game logic. Noise generation is essential for procedural content. Array utilities reduce Lua boilerplate significantly.

**Carimbo's `Vec2` is the most ergonomic Lua-facing vector type in the comparison** — it's a sol2 usertype with all the operators (`+`/`-`/`*`/`/`/`-`), static factory methods (`Vec2.zero()`, `Vec2.up()`, etc.), and a full static method set (`lerp`, `dot`, `cross`, `length`, `distance`, `angle`, `rotate`, `reflect`, `project`, `perpendicular`, `clamp`). That's worth studying as a reference when exposing our existing `FVec2` more fully to Lua.

### 11. Color

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Named colors | Yes (`color.cc` DB) | No | No | No | No | Yes (`RED`, `BLUE`, `RAYWHITE`, …) |
| RGBA components | Yes | Yes | Yes (`rgba_t`) | Yes (color object) | Yes (`Color` usertype) | Yes (`Color` struct) |
| Hex string constructor | No | No | No | No | **Yes** (`Color.color("#RRGGBBAA")`) | No |
| HSL / HSV support | No | No | No | Yes (auto-sync) | No | Yes (`ColorToHSV`, `ColorFromHSV`) |
| Color interpolation | No | No built-in | No | Yes (`mix`) | No | Yes (`ColorLerp`) |
| Color invert | No | No built-in | No | Yes | No | Yes (`ColorInvert`) |
| Color arithmetic | No | No built-in | Yes (blending) | Yes (operators) | No | Yes (`ColorTint`, `ColorBrightness`, `ColorContrast`) |
| Gamma correction | No | Yes (sRGB pipeline) | No | No | No | Yes (`ColorLinearToGamma`) |

**Gaps to fill**: HSL/HSV support and color interpolation are valuable for procedural color palettes, day/night cycles, and UI theming. Raylib has the most complete color utility set of any comparison engine (`ColorLerp`, `ColorTint`, `ColorBrightness`, `ColorContrast`, `ColorInvert`, `ColorFromHSV`, `ColorAlpha`, gamma conversion). These are all one-liners to implement if we want parity.

### 12. UI System

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| UI framework | No | No | No | No | Overlay system (Labels, Cursors) + HUD decorator | `raygui` (separate single-header) |
| Text wrapping | No (see font system) | Yes (`printf`) | Yes (`font_draw`) | No | No | No (manual) |
| Text alignment | No (see font system) | Yes (L/C/R/J) | Yes (L/C/R) | No | No | No (manual via `MeasureText`) |
| Colored text spans | No (ANSI escapes) | Yes (inline) | No | No | No | No |
| Word wrap measurement | No | Yes (`getWrap`) | No | No | No | Yes (`MeasureTextEx`) |
| Immediate-mode widgets | No | No | No | No | No | **Yes** (raygui: buttons, sliders, spinners, text boxes, dropdowns, color pickers, 20+ controls) |
| Localization / i18n | No | No | No | No | **Yes** (`locales/<lang>.json` + `_()` lookup, OS auto-detect) | No |
| HUD overlay | No | No | No | No | **Yes** (HUD decorator with layout, character, items) | No |

**Gaps to fill**: Love2D's text layout features (wrapping, alignment, colored spans) remain essential building blocks — our SDF font system already has these implemented and is tracked in the Font system design doc.

Two features stand out in this section that no other comparison engine has:

- **`raygui`** (Raylib) is a single-header immediate-mode UI library with 20+ widgets. It's separate from raylib core but written to the same philosophy — tiny, no dependencies, C99. If we want a quick-start UI for dev tools, inspectors, or in-game menus without building our own retained-mode framework, vendoring something `raygui`-shaped is the obvious path.
- **Localization** (Carimbo) is exposed as a one-character global function `_("key")` that looks up a string in `locales/<lang>.json` with OS language auto-detection. This is the simplest possible i18n API and matches what GNU gettext does in C. If we ever want to ship localized games, this is the pattern to copy.

### 13. Scene / State Management

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Scene abstraction | No | No | Yes (`scene_t`) | No (use object tree) | **Yes** (`SceneManager` with JSON + Lua pair per scene) | No |
| Scene switching | No | No | Yes (deferred swap) | No | **Yes** (`scenemanager:set(name)`, deferred to next update) | No |
| Resource cleanup on switch | No | No | Yes (automatic) | No | **Yes** (`scenemanager:destroy(name)` or `"*"`; clears `package.loaded`) | No |
| Scene lifecycle callbacks | No | No | enter/exit | No | **Yes** (`on_enter`/`on_leave`/`on_loop`/`on_tick`/`on_touch`/`on_motion`/`on_keypress`/`on_keyrelease`/`on_text`/`on_camera`) | No |
| Scene decorators | No | No | No | No | **Yes** (wrap patterns: `ticker.wrap`, `HUD`, `sentinel`) | No |

**Gaps to fill**: A scene system provides structure for game states (menu, gameplay, pause, game over). Even a minimal one (init/update/draw/cleanup callbacks with switching) reduces boilerplate.

**Carimbo is the state-management leader** in this comparison. Its `SceneManager` is the only one with a full lifecycle (`on_enter`/`on_leave`/`on_loop`/`on_tick` + input callbacks), deferred scene swap (avoiding mid-frame inconsistency), automatic `package.loaded` cleanup for Lua module reloading, and per-scene physics worlds. The "scene as a Lua module with a `sentinel()` call before `return scene`" pattern is also cute — the sentinel enables GC-based lifecycle tracking. Worth adapting if we build a scene system.

Save/load is now a separate section below — it's too important to bury under state management.

### 14. Save / Persistence / Achievements

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Per-user save directory resolver | No | Yes (sandboxed `save/`) | Yes (userdata path) | No | Yes (PhysFS write dir) | No |
| Save game state (structured) | No | Yes (via `love.filesystem` + serpent/bitser) | Yes (userdata API) | No | **Yes** (`Cassette` KV store) | Simple int slots only (`SaveStorageValue(pos, value)`) |
| Key/value API | No | No (files only) | Partial | No | **Yes** (`cassette:get/set`) | **Yes** (int-indexed, not string-keyed) |
| Atomic writes / corruption resistance | No | Yes (temp file + rename) | Depends | No | Depends on miniaudio/PhysFS impl | No |
| Achievements | No | No | No | No | **Yes** (built-in `Achievement` type) | No |
| Statistics tracking | No | No | No | No | Possible via Cassette | No |
| Leaderboards | No | No | No | No | `User` / `Buddy` types (social) | No |
| Settings persistence | No | Manual (save files) | Manual | No | Via Cassette | Via `SaveStorageValue` or files |
| Cloud sync | No | No | No | No | No | No |
| Namespaced keys | No | N/A | N/A | N/A | Unknown (single KV namespace) | No (int slots) |

**Carimbo is the only engine in the comparison with built-in persistent saves and achievements**. Its `Cassette` is a simple KV store available as a global (`cassette:get(key)`, `cassette:set(key, value)`) and its `Achievement` type is a first-class engine concept — both features that every shippable game needs and that we currently have zero support for. Raylib has a trivial int-indexed storage API (you `SaveStorageValue(position, value)` where `position` is a small integer slot) which covers simple save-slot cases but won't scale to structured game state.

**Our planned approach**: a namespaced SQLite KV store reusing the sqlite3 amalgamation we already vendor, stored at a platform save directory. See [Save and persistence](Save%20and%20persistence.md) for the full design. Key differentiators from Carimbo's Cassette:

- **Namespaces** — all keys are `(namespace, key)` pairs, so we can enumerate "all achievements" or "all save slots" cheaply via a range scan.
- **Atomic writes via WAL** — SQLite provides crash safety for free.
- **Built on existing infrastructure** — no new library, same `Allocator` integration we already have for the asset DB.

This is arguably the highest-leverage feature we could borrow from Carimbo, because it's the only thing in the comparison that's both **genuinely missing** from our engine and **necessary to ship a complete game**.

### 15. Platform & Distribution

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Windows | Stubbed | Yes | Yes | Yes | Yes | Yes |
| macOS | No | Yes | Yes | Yes | Yes | Yes |
| Linux | Yes | Yes | Yes | Yes | Yes | Yes |
| Web / WASM | No | Experimental | Yes (Emscripten) | Yes (Emscripten) | **Yes (primary target)** | Yes (Emscripten) |
| Android | No | Yes | No | No | Partial | Yes |
| iOS | No | Yes | No | No | Partial | No |
| Raspberry Pi / DRM | No | No | No | No | No | **Yes** |
| Single-exe packaging | Yes | Yes (fused) | Yes (compiled in) | Yes (zip append) | Cartridge zip (`.rom`) | Manual (or `rres`) |
| Hot reload | Yes | No | No | No | No | No |
| Save directory | No | Yes (sandboxed) | Yes (userdata) | No | Yes (PhysFS) | No |
| File drop support | No | Yes | No | No | No | Yes |
| Clipboard access | No | Yes | No | No | No | Yes |

**Gaps to fill**: Web/WASM is the most impactful platform gap — it enables instant sharing and playtesting. Windows support needs to be completed. A sandboxed save directory is necessary for persisting game state.

Carimbo being **WASM-first** (the engine is explicitly designed for `carimbo.games` hosting) is a useful existence proof that a modern C++ + SDL + Box2D engine can ship cleanly to the browser. Raylib's platform matrix is the broadest in the comparison, reaching Raspberry Pi and DRM (framebuffer) targets that nobody else even considers — relevant if we ever care about embedded or handheld targets.

### 16. Debugging & Development

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib |
|---|---|---|---|---|---|---|
| Hot reload | Yes | No | No | No | No | No |
| Fennel (lisp) support | Yes | No | No | No | No | No |
| Debug font | Yes | Yes | Yes (`font_tool`) | No | Yes (overlay labels) | Yes (`DrawText` with default font) |
| Performance metrics | Yes (timing) | Yes (draw calls, memory) | Yes (entities, checks, draw calls, ms) | Yes (fps, draw calls) | Yes (via overlay labels) | Yes (FPS, frame time) |
| Error screen | No | Yes (stack trace) | No | No | No | No |
| Type stubs (IDE) | Yes | No | No | No | No | N/A (C API) |
| CLI tooling | Yes (init/run/package/stubs) | No (external tools) | No | No | No | No |
| Crash reporting | No | No | No | No | **Yes** (Sentry integration) | No |
| Input record / replay | No | No | No | No | No | **Yes** (automation events) |
| Trace logging | Yes | Yes | Yes | Yes | Yes | Yes (`TraceLog` with levels) |

**Our advantages**: Hot reload, Fennel support, CLI tooling, and type stub generation remain genuine differentiators not found in any comparison engine. Carimbo and Raylib each add one unique dev feature:

- **Carimbo has Sentry crash reporting built in** (`with_sentry(dsn)`). Useful for shipping commercial games with telemetry.
- **Raylib has input automation events** — record a session to a file, replay deterministically. Pairs naturally with our Test input system design doc.

## Priority gap ranking

Based on what's needed to ship complete games, grouped by impact:

### Tier 1 — Blocking (can't ship most games without these)

1. ~~**Camera system** — Follow target, smooth interpolation, bounds, shake, world/screen conversion~~
2. ~~**Animation system** — Spritesheet frame sequences, loop/once/bounce, flip, timing~~
3. ~~**Timer / tween / easing** — Delayed callbacks, repeating timers, tweens, easing curves, springs~~
4. ~~**Canvas / off-screen rendering**~~ — DONE. `new_canvas`, `set_canvas`, `draw_canvas` with auto premultiplied alpha and Y-flip.
5. ~~**Audio improvements**~~ — DONE. Looping, pitch, pan, pause/resume per source.
6. **Save / persistence / achievements** — Platform save dir + namespaced KV store + achievements. Carimbo is the only comparison engine with this and it's necessary to ship any non-trivial game. See [Save and persistence](Save%20and%20persistence.md).

### Tier 2 — Important (needed for specific genres or polish)

7. **Tilemap system** — Tile rendering, collision, parallax, multiple layers
8. **Physics expansion** — Sensors, raycasting, spatial queries, kinematic bodies, more shapes
9. **Input action binding** — Map physical buttons to game actions, support rebinding
10. **Scene / state management** — Init/update/draw/cleanup, switching, resource cleanup. Carimbo's `SceneManager` is the reference.
11. ~~**Blend modes** — DONE. Alpha, additive, multiply, replace.~~
12. ~~**Time scale** — Global slow-motion / pause, separate game vs real time~~

### Tier 3 — Nice to have (quality of life and polish)

13. **Math utilities** — Lerp, distance, angle, direction, noise, easing functions. Carimbo's `Vec2` sol2 binding is the ergonomics target.
14. **Text layout** — Word wrap, alignment (left/center/right). Tracked in the Font system doc.
15. **More drawing primitives** — Outlined shapes, rounded rectangles, ellipses, arcs, splines. Raylib's `rshapes.c` is the reference.
16. **Color utilities** — HSV, interpolation, arithmetic. Raylib has the most complete set.
17. **Stencil masking** — For fog-of-war, UI clipping, spotlight effects
18. **Web/WASM target** — Instant sharing and playtesting. Carimbo is an existence proof that modern C++ + SDL + Box2D ships cleanly to WASM.
19. **Localization / i18n** — Carimbo's `_("key")` + `locales/<lang>.json` pattern is the simplest possible starting point.
20. **Input record / replay** — Raylib's automation events pair naturally with the Test input system design.
21. **Entity utilities** — Tagging, queries, safe references, draw-order sorting. (Explicitly not an ECS.)
22. ~~**Pixel-perfect rendering** — Integer scaling for pixel art games~~

## Appendix: What we do well

Features where our engine equals or exceeds the competition (now against five reference engines):

- **Hot reload** — None of Love2D, high_impact, Anchor, Carimbo, or Raylib have this. This remains a major development speed advantage.
- **Fennel scripting** — Unique among these engines. Lisp-family language for those who prefer it.
- **Asset pipeline** — SQLite-backed with checksums, file watching, and parallel loading is more sophisticated than any comparison engine. Carimbo uses PhysFS with a zip cartridge, Raylib ships plain files. Neither has our incremental hot-reload story.
- **SDF fonts** — Resolution-independent font rendering. Love2D uses FreeType bitmap rasterization, high_impact uses bitmap fonts, Anchor uses FreeType, Carimbo uses bitmap atlas, Raylib uses stb_truetype bitmap atlas. Our SDF approach scales to any size without regenerating atlases — unique in this comparison.
- **CLI tooling** — `game init`, `game run`, `game package`, `game stubs` — a complete workflow. No comparison engine has a first-party CLI.
- **Memory management** — Arena allocators with a fixed 4 GB budget, explicit allocator passing. More controlled than Love2D, Anchor, or Carimbo (which inherit their allocators from their dependency graphs); comparable to high_impact's bump allocator and Raylib's plain malloc.
- **Type stubs** — IDE completion for the Lua API. No comparison engine generates these.
- **Timer/tween/easing** — Implemented with cooldowns, springs, and 20+ easing curves. Matches Anchor (the gold standard) and exceeds everything else.
- **Camera system** — Follow, deadzone, bounds, shake, zoom, parallax. More complete than Raylib's bare `Camera2D` and Carimbo's script-driven `on_camera`; on par with Anchor.
- **Animation system** — Frame sequences, loop/once/bounce, flip, timing. Matches Carimbo and exceeds Love2D and Raylib (which have no built-in animation at all).

## Notes on the newly added engines

**Carimbo** (`willtobyte/carimbo`) is the comparison engine most philosophically similar to ours in tech stack — C++ core with Lua scripting via sol2, SDL + Box2D + miniaudio + PhysFS, WebAssembly target. It's also the most ambitious in scope, shipping built-in particle systems, tilemaps, animation, scene management, persistent saves, achievements, and localization. The tradeoff is that it's a far more opinionated framework: games are declarative JSON scenes + per-object Lua modules + an EnTT entity registry, not the imperative toolbox we offer. Read it as "what our engine could look like if we leaned hard into framework territory," and borrow individual features (Cassette, SceneManager, Vec2 binding shape, localization) without adopting the overall architecture.

**Raylib** (`raysan5/raylib`) is the comparison engine most philosophically different from ours — no scripting, C99 throughout, procedural API. Its value as a reference is its breadth: the richest primitive drawing set, the most complete color utility set, the broadest platform matrix, and genuinely unique features (automation events, `Camera2D` simplicity, raymath's easing curve library). It's also the engine with the largest community and the most third-party extensions (`raygui`, `physac`, `rres`, raylib-extras), which means for any feature we consider building, there's usually a small raylib-style reference implementation to study.
