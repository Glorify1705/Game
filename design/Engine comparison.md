---
status: implemented
tags: [reference, comparison]
---

# Engine Comparison: Feature Gap Analysis

This document compares our engine against six reference 2D game engines — **Love2D**, **high_impact**, **Anchor**, **Carimbo**, **Raylib**, and **libGDX** — to identify missing features needed to ship complete videogames.

## Engine profiles

| | Our engine | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| **Language** | C++17 core, Lua 5.1 scripting | C++ core, Lua (LuaJIT) scripting | Pure C99, user code in C | C99 core, Lua 5.4 scripting | C++23 core, Lua scripting (sol2) | Pure C99, no built-in scripting | Java core, user code in Java/Kotlin/Scala |
| **Architecture** | Module-based, explicit allocators | Module-based | Flat C with X-macro entities | Object tree + mixins | ECS (EnTT) with declarative JSON scenes | Flat C procedural API (rlgl low-level layer) | `ApplicationListener` + `Screen` pattern; Scene2D `Actor` tree for UI; optional Ashley ECS |
| **Size** | ~21,500 LOC | Large (~200K+ LOC) | ~4,800 LOC | ~10,000 LOC (C) + Lua framework | Small core, heavy dep tree (boost, EnTT, sol2, SDL, miniaudio, Box2D, PhysFS, yyjson, stb) | ~60,000 LOC | ~250K LOC Java + JNI bridges |
| **Renderer** | OpenGL 3.3+, SDL2 | OpenGL/ES, SDL2 | OpenGL/Metal/Software, SDL2 or Sokol | OpenGL 3.3/WebGL 2, SDL2 | SDL_Renderer (SDL3) | OpenGL 1.1/2.1/3.3/ES2/ES3 via `rlgl` | OpenGL ES 2.0/3.0; WebGL via GWT transpile |
| **Physics** | Box2D | Box2D | Custom AABB + slopes | Box2D 3.x | Box2D (per-scene world) with raycast + category bitmasks | None in core (optional `physac` single-header) | Box2D (JNI binding) + Bullet (3D) |
| **Audio** | SDL2 callback, dr_wav + stb_vorbis | OpenAL Soft | QOA + pl_synth | miniaudio + Vorbis | miniaudio + Opus | `raudio` (miniaudio) — WAV/OGG/MP3/FLAC/XM/MOD/QOA | OpenAL (desktop) / AudioTrack (Android) / WebAudio (GWT) |
| **Asset format** | SQLite DB + hot-reload | PhysFS (zip-based) | QOI/QOA files | Files or zip appended to exe | PhysFS-backed `cartridge.rom` zip | Plain files (optional `rres` packer) | `FileHandle` abstraction over plain files; `AssetManager` for async loading |
| **Platforms** | Linux (Windows stubbed) | Win/Mac/Linux/Android/iOS | Win/Mac/Linux/Web (WASM) | Win/Mac/Linux/Web (WASM) | Linux/Win/Mac/Web (WASM-first), Android/iOS partial | Win/Mac/Linux/Web/Android/Raspberry Pi/DRM | Win/Mac/Linux (LWJGL3), Android, iOS (RoboVM/MOE), HTML5 (GWT), **Headless** |
| **License** | Proprietary | zlib/libpng | MIT | MIT | Custom permissive (attribution) | zlib/libpng | Apache 2.0 |

## Feature-by-feature comparison

### 1. Rendering

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Sprite/image drawing | Yes | Yes | Yes | Yes | Yes | Yes | Yes (`SpriteBatch`) |
| Sprite atlas / spritesheet | Yes (JSON/XML) | Yes (Quad-based) | Yes (tileset) | Yes (spritesheet) | Yes (JSON object descriptors) | No built-in (manual source rects) | Yes (`TextureAtlas` + `TexturePacker` tool) |
| Batch rendering | Yes (command buffer) | Yes (auto + SpriteBatch) | Yes (texture atlas batching) | Yes (deferred command queue) | SDL_Renderer-driven | Yes (auto via `rlgl`) | Yes (`SpriteBatch`, `PolygonSpriteBatch`) |
| Transform stack | Yes (push/pop, mat4) | Yes (push/pop) | Yes (push/pop, mat3) | Yes (push/pop) | No (scene-graph z-order) | Yes (`rlPushMatrix`/`rlPopMatrix`) | Via `Matrix4` + `batch.setTransformMatrix` (no push/pop helper) |
| Filled rectangles | Yes | Yes | Yes (via draw) | Yes | Limited | Yes | Yes (`ShapeRenderer`) |
| Circles | Yes (fill only) | Yes (fill + outline) | No built-in | Yes (fill + outline) | No | Yes (fill + outline + sector) | Yes (fill + outline) |
| Triangles | Yes | Yes (polygon) | No built-in | Yes (fill + outline) | No | Yes (fill + outline + fan + strip) | Yes (fill + outline) |
| Lines / polylines | Yes (configurable width) | Yes (join styles, width) | No built-in | Yes (capsule lines) | No | Yes (width, bezier, curves) | Yes (line, polyline) |
| Ellipses | No | Yes | No | No | No | **Yes** | Yes (`ShapeRenderer.ellipse`) |
| Arcs | No | Yes | No | No | No | **Yes** (sector, ring) | Yes (`ShapeRenderer.arc`) |
| Rounded rectangles | No | Yes | No | Yes | No | **Yes** | No built-in |
| Arbitrary polygons | No | Yes | No | Yes (up to 8 verts) | No | **Yes** (regular + lines) | Yes (`Polygon` + `PolygonSpriteBatch`) |
| Points | No | Yes | No | No | No | Yes | Yes |
| Gradient fills | No | No | No | Yes (H/V gradients) | No | Yes (H/V + 4-corner) | Yes (4-corner via `SpriteBatch` vertex colors) |
| Splines / Bezier curves | No | Yes (Bezier) | No | No | No | **Yes** (linear, basis, Catmull-Rom, quad/cubic Bezier) | Yes (`Bezier`, `CatmullRomSpline`, `BSpline` math classes) |
| Custom shaders | Yes (GLSL) | Yes (GLSL dialect) | Yes (GLSL) | Yes (fragment only) | No (SDL_Renderer) | Yes (GLSL, vertex + fragment) | Yes (`ShaderProgram`, vertex + fragment) |
| Post-processing effects | Yes (Canvas + Shader) | Yes (via Canvas + Shader) | Yes (CRT effect built-in) | Yes (per-layer shaders) | No | Yes (RenderTexture + shader) | Yes (`FrameBuffer` + `ShaderProgram`) |
| Canvas / off-screen render | Yes (non-MSAA FBO) | Yes (Canvas, MRT) | No | Yes (FBO layers) | No | Yes (`RenderTexture2D`) | Yes (`FrameBuffer`, `FrameBufferCubemap`) |
| Blend modes | Yes (alpha/add/multiply/replace) | Yes (8+ modes) | Yes (Normal, Additive) | Yes (Alpha, Additive) | Basic (SDL blend modes) | Yes (alpha/add/multiply/sub/custom) | Yes (raw GL blend func — anything OpenGL supports) |
| Stencil masking | No | Yes | No | Yes | No | Yes (via `rlgl`) | Yes (`ScissorStack` + GL stencil) |
| MSAA | Yes | Yes | No | No | No | Yes (`FLAG_MSAA_4X_HINT`) | Yes (config samples) |
| Mesh / custom geometry | No | Yes (Mesh object) | No | No | No | Yes (`Mesh`, VAO-backed) | Yes (`Mesh` class with VBO/IBO) |
| Pixel-perfect scaling | Yes (canvas + nearest filter) | No | Yes (discrete scaling) | Yes (rough filter mode) | Yes (`with_scale` factor) | Manual (RenderTexture pattern) | Via `FitViewport` / `ScreenViewport` |
| Color tinting per draw | Yes | Yes | Yes | Yes | Yes (alpha) | Yes | Yes (`batch.setColor`) |
| Screenshots | Yes (PNG) | Yes (async) | No | No | No | Yes (`TakeScreenshot`) | Yes (`ScreenUtils.getFrameBufferPixmap`) |
| HiDPI support | No | Yes | No | No | Auto (WASM canvas scaling) | Yes (`FLAG_WINDOW_HIGHDPI`) | Yes (`Graphics.getBackBufferWidth`, density-aware) |
| Mipmaps | No | Yes | Yes (optional) | No | No | Yes (`GenTextureMipmaps`) | Yes (`Texture.MipMapLinearLinear`) |

**Status**: Canvas, blend modes, and pixel-perfect scaling are now implemented. Our canvas handles premultiplied alpha automatically (avoiding Love2D's biggest footgun) and Y-flip transparently (avoiding Raylib's annoyance — Raylib's `RenderTexture2D` requires a manual Y-flip when drawing back to the screen). Remaining gaps: stencil masking for fog-of-war/UI clipping, outlined shape drawing modes, and more blend modes (Love2D has 8+, we have 4).

**Raylib is the primitive-drawing leader** of any engine in this comparison — it's the only one with built-in ellipses, arcs/sectors, rings, and spline curves (linear, basis, Catmull-Rom, quadratic/cubic Bezier). If we want to close the Tier 3 "more drawing primitives" gap, Raylib's `rshapes.c` is the reference implementation worth studying.

**Carimbo stays minimal** on the primitive front — it's built around sprite rendering via SDL_Renderer, with shape drawing essentially not exposed to Lua. The design choice reflects its scope: declarative sprite-based games with animation timelines, not arbitrary procedural graphics.

**libGDX is the most "give you the GL handle" of the comparison engines**: blend modes are raw `glBlendFunc` calls, MSAA is configured at app start, FBOs map directly to GL framebuffers, and you can drop down to a `Mesh` with custom vertex attributes whenever you want. The cost of that flexibility is that the high-level conveniences (transform stack, scoped canvas, blend mode presets) all have to be built in user code.

**Detailed canvas comparison**:

| Capability | Ours | Love2D | Anchor | Raylib | libGDX | Notes |
|---|---|---|---|---|---|---|
| Create render target | `new_canvas(w,h)` | `newCanvas(w,h,{...})` | `an:layer(name)` | `LoadRenderTexture(w,h)` | `new FrameBuffer(format, w, h, depth)` | libGDX exposes pixel format and depth-buffer flag at construction |
| Redirect drawing | `set_canvas(c)` / `set_canvas()` | `setCanvas(c)` / `setCanvas()` | Implicit per-layer | `BeginTextureMode(t)` / `EndTextureMode()` | `fbo.begin()` / `fbo.end()` | Raylib and libGDX both use begin/end scoping |
| Draw as texture | `draw_canvas(c,x,y)` | `draw(canvas,x,y)` | Layer compositing | `DrawTextureRec` with flipped Y | `batch.draw(fbo.getColorBufferTexture(), ...)` with manual flipped V | libGDX shares Raylib's Y-flip gotcha |
| Y-flip handling | Automatic (UV inversion) | Automatic | Automatic | **Manual** (user must flip source rect) | **Manual** (flip V coordinate) | We, Love2D, and Anchor avoid the gotcha |
| Premultiplied alpha | Automatic | Manual (footgun!) | Automatic | Manual | Manual | Our biggest UX win vs everyone except Anchor |
| Filter modes | nearest / linear | nearest / linear + aniso | N/A | nearest / bilinear / trilinear / aniso | nearest / linear / mipmap variants | Parity with Love2D basics |
| Pixel formats | RGBA8 only | 20+ including HDR | RGBA8 | 20+ including HDR / compressed | RGBA8 / RGB565 / RGBA4 / float HDR | libGDX and Love2D/Raylib all support HDR; RGBA8 sufficient for 2D |
| MRT (multi-target) | No | Yes (up to 4) | No | No | Yes (`MRTFrameBuffer`) | Needed for deferred lighting, not common in 2D |
| Per-canvas MSAA | No (non-MSAA only) | Yes | No | No | No | By design: pixel art and post-FX don't want MSAA |
| Scoped helper | Not yet | `renderTo(fn)` | N/A | `BeginTextureMode`/`EndTextureMode` | `begin`/`end` (manual try/finally) | Easy to add in Lua |
| Draw with scaling | Yes (w,h params) | Via transform | Via transform | Via `DrawTexturePro` | Via overload of `batch.draw` | Convenience over push/scale/pop |
| Blend modes | 4 (alpha/add/multiply/replace) | 8+ (+ premultiplied variants) | 2 (alpha/additive) | 5 (alpha/add/multiply/sub/custom) | Any (raw `glBlendFunc`) | libGDX gives the most flexibility but no presets |
| Clear with color | Yes `clear(r,g,b,a)` | Yes `clear(r,g,b,a)` | N/A | `ClearBackground(color)` | `Gdx.gl.glClearColor` + `glClear` | Parity with Love2D once wrapped |
| Stencil on canvas | Not yet | Yes | Yes | Via `rlgl` low-level | Yes (raw GL stencil ops) | Future work |
| Nesting | Yes (reset to screen) | Yes (reset to screen) | N/A | Yes (LIFO begin/end) | Yes (LIFO begin/end) | Same behavior as Love2D |

Carimbo is omitted from the canvas comparison because it does not expose off-screen render targets to scripts.

### 2. Animation

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Frame-based sprite animation | No (logic in Lua) | No built-in (libraries) | Yes (`anim_def_t`) | Yes (spritesheet animation) | **Yes** (JSON timelines, named `action` per entity) | No built-in (manual rect stepping) | Yes (`Animation<TextureRegion>`) |
| Animation sequences | No | No built-in | Yes (arbitrary frame order) | Yes (frame indices) | **Yes** (arbitrary frame order + per-frame duration) | No | Yes (frame array + uniform frame duration) |
| Loop / once / bounce modes | No | No built-in | Yes (loop + stop) | Yes (loop, once, bounce) | **Yes** (loop + oneshot) | No | **Yes** (`PlayMode.NORMAL/REVERSED/LOOP/LOOP_REVERSED/LOOP_PINGPONG/LOOP_RANDOM`) |
| Flip X/Y | No | No built-in | Yes | No built-in | **Yes** (`Flip.horizontal/vertical/both`) | Via negative source rect | Via `TextureRegion.flip` |
| Per-frame callbacks | No | No built-in | No | Yes | `on_begin` / `on_end` (timeline start/finish) | No | No (poll `getKeyFrameIndex`) |
| Animation rewind/goto | No | No built-in | Yes | Yes (`set_frame`, reset) | Implicit (reassign `action`) | N/A | Via `stateTime` variable |
| Appear/disappear events | No | No | No | No | **Yes** (`on_appear`/`on_disappear` when switching to a timeline with/without frames) | No | No |

**Gaps to fill**: A built-in animation system is highly valuable. All action-oriented engines (high_impact, Anchor, Carimbo, libGDX) include one; only Love2D and Raylib leave it to the user. Carimbo's model is especially interesting — animations are declared in per-object JSON, the entity holds a current `action` string, and switching the string re-triggers the timeline with `on_begin`/`on_end`/`on_appear`/`on_disappear` callbacks. That's a nice scripting ergonomics win over the more imperative `play(name)` / `is_finished()` APIs in high_impact, Anchor, and libGDX.

**libGDX's `Animation<T>` is the most minimal "real" implementation in the comparison** — it's a generic over the frame type (`TextureRegion` is most common, but you can animate anything), holds an array of frames + a uniform `frameDuration`, and exposes `getKeyFrame(stateTime, looping)`. The user maintains the `stateTime` accumulator, which is a strict separation between "what animation is playing" (the `Animation` object) and "where in the playback we are" (the `stateTime` float). The `LOOP_PINGPONG` and `LOOP_RANDOM` play modes are unusual extras; everyone else stops at loop/once/bounce.

At minimum for us: define a sequence of frames from a spritesheet, specify frame timing, support loop/once modes, flip X/Y, and fire a callback on completion for oneshots. Carimbo's `action`-as-string pattern is worth considering for the Lua binding shape.

### 3. Camera

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Camera abstraction | Yes | No built-in (Transform) | Yes (`camera_t`) | Yes (camera object) | Tilemap-scene `on_camera(dt) -> Quad` callback | **Yes** (`Camera2D` struct: target/offset/rotation/zoom) | Yes (`OrthographicCamera` / `PerspectiveCamera`) |
| Follow target entity | Yes | No built-in | Yes (with snap) | Yes (lerp follow) | Manual (script returns viewport) | Manual (`camera.target = entity.pos`) | Manual (`camera.position.set(...)`) |
| Smooth interpolation | Yes | No built-in | Yes (speed-based) | Yes (exponential lerp) | Manual | Manual | Manual (often via `Interpolation.exp10` lerp) |
| Dead zone | Yes | No built-in | Yes | No | Manual | No | Manual |
| Look-ahead | No | No built-in | Yes | No | Manual | No | Manual |
| Bounds clamping | Yes | No built-in | Yes (map bounds) | Yes (`set_bounds`) | Manual | No | Manual |
| Camera shake | Yes | No built-in | No | Yes (trauma, push, sine, square) | No | No | No (community extensions) |
| Zoom | Yes | No built-in | No | Yes | No | **Yes** (`camera.zoom`) | Yes (`camera.zoom`) |
| Rotation | Yes | Via transform | No | No | No | **Yes** (`camera.rotation`) | Yes (`camera.rotate`, `up` vector) |
| World/screen coord conversion | Yes | Yes (Transform) | No | Yes (`to_world` / `to_screen`) | No | **Yes** (`GetScreenToWorld2D` / `GetWorldToScreen2D`) | Yes (`camera.unproject` / `camera.project`) |
| Begin/end scoping | Via push/pop | Via push/pop | Implicit | Implicit | Implicit | `BeginMode2D(cam)` / `EndMode2D()` | Set `camera.combined` on the batch / shape renderer |
| Parallax layers | Yes | No built-in | Yes (distance factor) | No | Per-layer offset factor in scene JSON | Manual | Manual (or via `ParallaxLayer` extension) |
| Handcam (subtle drift) | No | No built-in | No | Yes | No | No | No |
| Viewport abstraction | No | No | No | No | No | No | **Yes** (`FitViewport`, `FillViewport`, `StretchViewport`, `ScreenViewport`, `ExtendViewport`) |

**Status**: Camera is now implemented (follow, deadzone, bounds, shake, zoom, parallax). The remaining gaps relative to other engines are niche: Anchor's handcam drift effect, high_impact's look-ahead, and more expressive screen-shake shapes.

**Raylib's `Camera2D` is the simplest design worth studying** — it's a plain struct with four fields (`target`, `offset`, `rotation`, `zoom`), scoped via `BeginMode2D`/`EndMode2D`, and paired with two pure-function coordinate converters. Everything else (follow, shake, bounds) is left to user code. That's a sharp contrast with Anchor's object-oriented camera and a reminder that the core primitive is small.

**Carimbo's approach is unusual**: the scene script owns the camera by implementing `on_camera(delta) -> Quad`, returning the viewport each frame. This means any follow/shake/bounds logic lives in Lua, and only tilemap scenes get cameras at all. Elegant for pure tilemap games, less flexible for mixed-content scenes.

**libGDX is the only engine in the comparison with a `Viewport` abstraction** as a separate concept from the camera. A `Viewport` owns both a camera and a strategy for how the world's logical units map to physical pixels on resize: `FitViewport` adds letterboxing to preserve aspect ratio, `FillViewport` crops, `StretchViewport` distorts, `ExtendViewport` extends the world rather than letterboxing, and `ScreenViewport` maps 1:1. This split — "where is the camera looking" vs "how does the world fit on the screen" — is genuinely useful and is the cleanest answer to the resize-handling problem in this comparison. We currently bake both concerns into the camera + canvas system.

### 4. Tilemap

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Tile map rendering | No | No built-in (STI library) | Yes (`map_t`) | No | Yes (JSON tilemap + atlas PNG) | No | **Yes** (`TiledMap` + `OrthogonalTiledMapRenderer` + isometric/hex variants) |
| Parallax scrolling | No | No built-in | Yes (distance factor) | No | Yes (layer offset factors) | No | Yes (multiple renderers) |
| Tile collision | No | No built-in | Yes (54 slope types!) | No | Box2D bodies from tile data | No | Manual (read object layer + build Box2D bodies) |
| Animated tiles | No | No built-in | Yes | No | Unknown | No | Yes (`AnimatedTiledMapTile`) |
| Multiple layers | No | No built-in | Yes (4 bg + collision) | No | Yes | No | Yes (any number, `MapLayer` hierarchy) |
| Foreground / background | No | No built-in | Yes | No | Yes (layer ordering) | No | Yes (per-layer render order) |
| Tile repeat / wrap | No | No built-in | Yes | No | Unknown | No | Yes (via `TiledMapTileLayer` props) |
| Level editor | No | No built-in | Yes (Weltmeister) | No | No | No | Tiled (external, but first-class TMX import) |
| Level loading from JSON | No | No built-in | Yes | No | Yes (`tilemaps/<name>.json`) | No | Yes (`TmxMapLoader`, also TMJ JSON via plugin) |

**Gaps to fill**: high_impact and libGDX are the clear leaders here, with Carimbo as a lighter-weight precedent. A tilemap system with collision detection is essential for platformers, top-down RPGs, and many other genres. high_impact's slope system (54 tile types) remains a standout feature. At minimum: load tile data, render with camera offset, query tiles at positions, and detect tile collisions.

Raylib has no tilemap support — the community ports Tiled / raylib-extras plugins. Love2D and Anchor both delegate to external libraries (STI for Love2D).

**libGDX is the only engine here with first-class Tiled (TMX) import in core**. The renderer hierarchy splits orthogonal/isometric/hex into separate classes, animated tiles are a built-in `TiledMapTile` subclass, and the loader handles object layers, tile properties, and tilesets in a single call. The cost is verbosity — you get a `Map` of `MapLayer`s of `MapObject`s/`Cell`s and have to walk the structure yourself for collision. high_impact's collision-with-slopes system is still richer for platformers specifically.

### 5. Entity / Game Object System

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Entity framework | No | No | Yes (`entity_t` + vtab) | Yes (object tree + mixins) | Yes (EnTT ECS, accessed via `pool["name"]`) | No | Two: Scene2D `Actor`/`Group` tree (UI-focused), Ashley ECS (extension) |
| Entity types / classes | No | No | Yes (X-macro compile-time) | Yes (class inheritance) | Yes (per-object Lua module + JSON) | No | Java class hierarchy (Scene2D); component types (Ashley) |
| Entity groups / tags | No | No | Yes (bitmask groups) | Yes (tag system) | Yes (`kind` string + physics category bitmasks) | No | Yes (Scene2D groups; Ashley `Family`) |
| Entity queries | No | No | Yes (by type, name, proximity, location) | Yes (by tag) | By name via `pool`, by physics category via raycast | No | Yes (Scene2D `findActor`; Ashley family iterators) |
| Entity references (safe) | No | No | Yes (survives death) | Yes (link system) | Yes (`entity.alive` check) | No | Java GC keeps refs valid; manual `removed` flag |
| Draw order sorting | No | No | Yes (`draw_order` field) | No (FIFO call order) | Yes (`z` property per entity) | No | Yes (Scene2D z-order via child order; manual sort otherwise) |
| Collision groups | No | No | Yes (`check_against` bitmask) | Yes (tag-based filtering) | Yes (Box2D categories) | No | Yes (Box2D Filter on bodies) |
| Kill / cleanup | No | No | Yes (auto on health <= 0) | Yes (deferred cleanup) | Yes (`entity:die()`) | No | Yes (`actor.remove()`; Ashley `engine.removeEntity`) |
| Spawn from level data | No | No | Yes (JSON entity settings) | No | Yes (scene JSON) | No | Manual (read TMX object layer) |
| Entity cloning | No | No | No | Yes | Yes (`entity:clone()` with fresh script env) | No | Manual (clone constructor) |

**Position**: We intentionally leave entity management to Lua, and this decision stands — Carimbo's ECS is a genuinely different architectural style (declarative JSON + per-object Lua modules + EnTT in C++) that doesn't map cleanly onto our "engine as toolbox" philosophy. Worth noting as a contrast point, not a gap to chase. If we ever want to reduce Lua boilerplate, the lightest-weight borrowing would be tagging/grouping for collision filtering and draw-order sorting, neither of which requires a full ECS.

**libGDX deliberately ships two answers** rather than picking one: Scene2D's `Actor`/`Group` tree (a retained-mode object hierarchy with built-in input event bubbling, actions, and hit testing — designed for UI but usable for game objects) and Ashley (a separate ECS extension following EnTT/Artemis lineage). Most libGDX games use Scene2D only for UI and roll their own game-object system on top of plain Java classes.

### 6. Physics & Collision

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Rigid body dynamics | Yes (Box2D) | Yes (Box2D) | Yes (custom) | Yes (Box2D 3.x) | Yes (Box2D per scene) | No core (optional `physac`) | Yes (Box2D via JNI; Bullet for 3D) |
| Static bodies | Yes | Yes | Yes | Yes | Yes | physac: Yes | Yes |
| Dynamic bodies | Yes (box, circle) | Yes (all shapes) | Yes (AABB only) | Yes (circle, box, capsule, polygon) | Yes (Box2D shapes) | physac: polygons | Yes (all Box2D shapes) |
| Kinematic bodies | No | Yes | No | Yes | Yes (via velocity) | No | Yes |
| Collision callbacks | Yes (begin/end) | Yes (begin/end/pre/post) | Yes (collide, touch) | Yes (begin/end, sensor, hit) | Yes (`on_collision` / `on_collision_end`) | No | Yes (`ContactListener`: begin/end/pre/post) |
| Sensors / triggers | No | Yes | Yes (touch groups) | Yes | Yes (via `trigger` category) | No | Yes (`fixture.isSensor`) |
| Joints | No | Yes (12 types) | No | No | Unknown | No | Yes (all Box2D joint types) |
| Raycasting | No | Yes | No | Yes | **Yes** (`world.raycast(origin, angle, distance, mask)`) | No | Yes (`World.rayCast` with callback) |
| Spatial queries | No | Yes (AABB query) | No | Yes (point, circle, AABB, box, capsule, polygon, ray) | Raycast only | No | Yes (`World.QueryAABB`) |
| Slope collision | No | No | Yes (54 tile slopes) | No | No | No | No |
| One-way platforms | No | No | Yes | No | Unknown | No | Manual (Box2D `preSolve` disable) |
| Edge / chain shapes | No | Yes | No | No | Unknown | No | Yes (`EdgeShape`, `ChainShape`) |
| Mass-based resolution | No | Yes (Box2D) | Yes (custom) | Yes (Box2D) | Yes (Box2D) | physac: Yes | Yes (Box2D) |
| Gravity per entity | No | Yes (Body) | Yes (gravity multiplier) | Yes (gravity scale) | Yes (per scene world) | No | Yes (`body.setGravityScale`) |
| Bullet mode | No | Yes | No | Yes | Yes (Box2D feature) | No | Yes |
| Capsule shape | No | No | No | Yes | No | No | No (Box2D limitation) |
| Polygon shape | No | Yes (8 verts) | No | Yes (8 verts) | Yes (Box2D) | physac: Yes | Yes (Box2D) |
| Category bitmask filtering | No | Yes | Yes | Yes | **Yes** (`PhysicsCategory.player`/`enemy`/`projectile`/`terrain`/`trigger`/`collectible`/`interface`) | No | Yes (`Filter` with category/mask/group) |

**Gaps to fill**: Our Box2D integration is minimal. Missing critical features: sensors (for trigger zones, item pickups), raycasting (for line-of-sight, laser beams), spatial queries (for area effects, AI detection), kinematic bodies (for moving platforms), and joints (for ropes, chains, vehicles). Capsule shapes are great for character controllers.

Carimbo's **per-scene world** is a useful pattern — each scene has its own Box2D world, destroyed on scene transition, which keeps bodies from leaking across menu → gameplay boundaries. Its predefined `PhysicsCategory` enum (player/enemy/projectile/terrain/trigger/collectible/interface) is also worth borrowing as a default filter set rather than making every game define its own bitmask layout.

### 7. Input

| Feature                             | Ours | Love2D | high_impact   | Anchor | Carimbo | Raylib | libGDX |
| ----------------------------------- | ---- | ------ | ------------- | ------ | ------- | ------ | ------ |
| Keyboard press/hold/release         | Yes  | Yes    | Yes           | Yes    | Yes     | Yes    | Yes    |
| Mouse position                      | Yes  | Yes    | Yes           | Yes    | Yes (logical coords) | Yes | Yes |
| Mouse buttons                       | Yes  | Yes    | Yes           | Yes    | Yes     | Yes    | Yes    |
| Mouse wheel                         | Yes  | Yes    | No            | Yes    | Unknown | Yes    | Yes (`scrolled` callback) |
| Gamepad buttons                     | Yes  | Yes    | Yes           | Yes    | Yes (4 slots) | Yes | Yes (`Controllers` extension) |
| Gamepad analog axes                 | Yes  | Yes    | Yes           | Yes    | Yes     | Yes    | Yes |
| Action binding (abstract)           | No   | No     | Yes           | Yes    | No      | No     | No (community libs) |
| Text input                          | Yes  | Yes    | Yes (capture) | No     | Yes (`on_text`) | Yes | Yes (`Input.getTextInput` + `keyTyped`) |
| Touch input                         | No   | Yes    | No            | No     | Unknown | Yes (multi-touch) | **Yes** (multi-touch primary on Android/iOS) |
| Gestures (tap/swipe/pinch)          | No   | No     | No            | No     | No      | **Yes** (`GetGestureDetected`) | **Yes** (`GestureDetector`: tap, longPress, fling, pan, zoom, pinch) |
| Input chords / sequences            | No   | No     | No            | Yes    | No      | No     | No |
| Hold duration detection             | No   | No     | No            | Yes    | No      | No     | Via `GestureDetector.longPress` only |
| Mouse grab / relative mode          | No   | Yes    | No            | Yes    | No      | Yes (`DisableCursor`) | Yes (`Input.setCursorCatched`) |
| Custom cursor                       | No   | Yes    | No            | No     | Yes (animated via `cursors/*.json`) | Yes (from image) | Yes (`Graphics.setCursor` from Pixmap) |
| Mouse world position (camera-aware) | No   | No     | No            | Yes    | No      | Yes (`GetScreenToWorld2D`) | Via `camera.unproject` |
| File drop                           | No   | Yes    | No            | No     | No      | Yes    | Yes (LWJGL3 backend) |
| Input record / replay               | No   | No     | No            | No     | No      | **Yes** (automation events) | Via `HeadlessApplication` + custom `InputProcessor` (no built-in record) |

**Gaps to fill**: Action binding is the key missing feature — it abstracts physical buttons into game actions ("jump", "shoot") allowing easy rebinding and multi-device support. Mouse world position (accounting for camera transform) is a must-have for any game with a camera.

Raylib brings two unique features: **gesture recognition** (tap/doubletap/hold/drag/swipe/pinch) which only matters if we target touch, and **input automation events** — a record/replay system that captures input streams to a file and replays them deterministically. That's a genuinely interesting feature for automated testing and would pair well with our existing Test input system design.

**libGDX matches Raylib on gesture detection** with `GestureDetector` (tap, long press, fling, pan, zoom, pinch — all with configurable thresholds), and goes one step further on the testing side: its **`HeadlessApplication`** backend lets you instantiate the engine without a display server or audio device, and feed events directly into an `InputProcessor` from a JUnit test. There's no built-in record/replay format like Raylib's, but the headless test pattern is the standard way libGDX games get covered by automated tests today. Both data points feed directly into our [Test input system](Test%20input%20system.md) design.

### 8. Audio

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Sound effect playback | Yes (WAV, OGG) | Yes (many formats) | Yes (QOA) | Yes (OGG) | Yes (Opus) | Yes (many formats) | Yes (`Sound`: WAV, MP3, OGG) |
| Music / streaming | Yes (streaming) | Yes (stream source) | Yes (QOA streaming) | Yes (multi-channel) | Yes (via miniaudio) | Yes (`Music` streams) | Yes (`Music` class) |
| Per-source volume | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Global volume | Yes | Yes | Yes | Yes | Yes | Yes (`SetMasterVolume`) | Manual (apply per source) |
| Pitch control | Yes | Yes | Yes | Yes | Unknown | Yes | Yes |
| Panning (stereo) | Yes | Yes (3D position) | Yes (pan control) | No | Unknown | Yes | Yes |
| Looping | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| 3D spatialized audio | No | Yes (OpenAL) | No | No | No | No (miniaudio capable but not exposed) | No (extensions only) |
| Audio effects (reverb, etc.) | No | Yes (8 effect types) | No | No | No | Yes (processor callbacks) | No |
| Procedural / synth audio | No | No | Yes (pl_synth) | No | No | Yes (`AudioStream` callback) | Yes (`AudioDevice` for raw PCM writes) |
| Microphone recording | No | Yes | No | No | No | No | Yes (`AudioRecorder`) |
| Playlist system | No | No | No | Yes (crossfade, shuffle) | No | No | No |
| MP3 / FLAC / tracker formats | No | Yes | No | No | No | **Yes** (WAV/OGG/MP3/FLAC/XM/MOD/QOA) | Partial (MP3, OGG, WAV; no FLAC or trackers) |
| Pause / resume individual | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Seek / tell | No | Yes | No | Yes | Unknown | Yes | Yes (`Music.setPosition` / `getPosition`) |

**Status**: Pitch, looping, panning, and per-source pause/resume are now implemented. Remaining gaps: playlist system (nice-to-have), seek/tell, 3D spatialized audio.

Raylib's `raudio` has the broadest format support of any comparison engine (XM/MOD tracker formats in addition to the usual suspects). It also exposes a raw `AudioStream` callback API for procedural audio (synths, chiptune, voxel engines), which is something no one else in the comparison does except high_impact's pl_synth.

### 9. Timer / Tween / Easing

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Delta time | Yes | Yes | Yes | Yes | Yes (`on_loop(delta)`) | Yes (`GetFrameTime`) | Yes (`Gdx.graphics.getDeltaTime`) |
| Game vs real time | Yes (pause support) | No built-in | Yes (`time_scale`) | Yes (`time_scale`) | Separate tick system (`on_tick`) | No | Manual |
| Time scale (slow-mo) | Yes | No built-in | Yes | Yes | Unknown | No | Manual |
| Delayed callbacks | Yes | No built-in | No | Yes (`timer:after`) | `ticker.wrap` (tick-based) | No | Yes (`Timer.schedule`) |
| Repeating timers | Yes | No built-in | No | Yes (`timer:every`) | `ticker.wrap` | No | Yes (`Timer.schedule` with interval) |
| Duration callbacks | Yes | No built-in | No | Yes (`timer:during`) | No | No | Via Scene2D `Action` |
| Tweening | Yes | No built-in | No | Yes (`timer:tween`) | `Vec2.lerp` primitive only | Via `raymath` easings | Via Scene2D `Actions.moveTo/scaleTo/...` (or Universal Tween Engine ext) |
| Easing functions | Yes | No built-in | No | Yes (20+ curves) | No | Yes (`raymath` easings: linear/sine/circ/cubic/quad/expo/back/bounce/elastic) | **Yes** (`Interpolation` class with 20+ curves: linear/fade/pow2-5/sine/exp/circle/elastic/swing/bounce + reversed/in/out variants) |
| Cooldowns | Yes | No built-in | No | Yes (`timer:cooldown`) | No | No | Manual |
| Spring physics | Yes | No built-in | No | Yes (damped oscillator) | No | No | No |

**Status**: Our timer/tween system is implemented and matches Anchor's capabilities closely. Carimbo takes a different approach with a dedicated tick rate separate from the frame loop (`with_ticks(10)` → `on_tick(tick)` fires at fixed rate regardless of frame rate), which is useful for game logic that shouldn't run every frame.

**libGDX's `Interpolation` class is the cleanest easing-curve API in the comparison** — it's a static set of public final fields (`Interpolation.bounce`, `Interpolation.elastic`, etc.) where each one is itself an object with an `apply(float a)` method, plus subclasses like `Interpolation.Pow` and `Interpolation.PowIn` that take constructor params. The "easing curves are values, not enums" shape is worth borrowing.

### 10. Math & Utilities

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| 2D vectors | Yes (`FVec2`, etc.) | No (use tables) | Yes (`vec2_t`) | No (use x,y args) | **Yes** (`Vec2` usertype with full API) | Yes (`Vector2` + `raymath`) | Yes (`Vector2`) |
| 3D/4D vectors | Yes | No | No | No | `Vec3` (limited) | Yes (`Vector3`/`Vector4`) | `Vector3` only (no `Vector4`) |
| Matrices (2x2, 3x3, 4x4) | Yes | No (Transform object) | Yes (mat3) | No | No | Yes (`Matrix` 4x4) | Yes (`Matrix3`, `Matrix4`) |
| Quaternions | No | No | No | No | No | Yes (`Quaternion`) | Yes (`Quaternion`) |
| Dot product | Yes | No built-in | Yes | No | Yes (`Vec2.dot`) | Yes | Yes |
| Normalize | Yes | No built-in | Yes | Yes (function) | Yes (`Vec2.normalize`) | Yes | Yes (`nor()`) |
| Distance | No | No built-in | Yes (`entity_dist`) | Yes (function) | Yes (`Vec2.distance`, `distance_squared`) | Yes | Yes (`dst`, `dst2`) |
| Angle between points | No | No built-in | Yes (`entity_angle`) | Yes (function) | Yes (`Vec2.angle_between`) | Yes | Yes (`angleDeg`) |
| Lerp | No | No built-in | No | Yes (+ framerate-independent) | Yes (`Vec2.lerp`) | Yes (`Lerp`) | Yes (`lerp`, `MathUtils.lerp`) |
| Clamp | Yes | No built-in | No | Yes | Yes (`Vec2.clamp`) | Yes | Yes (`MathUtils.clamp`) |
| Snap to grid | No | No built-in | No | Yes | No | No | No |
| Sign | No | No built-in | No | Yes | No | Yes | Manual |
| Direction from angle | No | No built-in | No | Yes | Yes (`Vec2.rotate`) | Yes | Yes (`Vector2.setAngleDeg`) |
| Reflect | No | No built-in | No | Yes | Yes (`Vec2.reflect`) | Yes | No core |
| Perpendicular | No | No | No | No | Yes (`Vec2.perpendicular`) | No | Yes (`rotate90`) |
| Project onto vector | No | No | No | No | Yes (`Vec2.project`) | No | Manual (dot product) |
| Perlin / simplex noise | No | Yes (1-4D) | Yes (2D) | Yes | No | Yes (image-gen perlin) | No (extensions) |
| Bezier curves | No | Yes | No | No | No | Yes | Yes (`Bezier`, `BSpline`, `CatmullRomSpline`) |
| Triangulation | No | Yes | No | No | No | No | Yes (`DelaunayTriangulator`, `EarClippingTriangulator`) |
| Seeded RNG | Yes (PCG) | Yes (platform-independent) | No built-in | Yes (PCG32) | Yes (xorshift128+) | Yes | Yes (`RandomXS128`) |
| Random from distribution | No | Yes (normal) | No | Yes (normal) | No | No | Yes (`MathUtils.randomTriangular`) |
| Weighted random | No | No built-in | No | Yes | No | No | No |
| Array utilities | No | No built-in | No | Yes (30+ functions) | No | No | Yes (`Array`, `Sort`, `OrderedMap`, `IntMap`, `ObjectSet`, etc.) |
| Easing curves as functions | No | No | No | Yes (via tween) | No | Yes (`raymath` has 40+) | Yes (`Interpolation` static fields) |

**Gaps to fill**: Lerp (especially framerate-independent) is used everywhere. Distance, angle-between-points, and direction-from-angle are basics for game logic. Noise generation is essential for procedural content. Array utilities reduce Lua boilerplate significantly.

**Carimbo's `Vec2` is the most ergonomic Lua-facing vector type in the comparison** — it's a sol2 usertype with all the operators (`+`/`-`/`*`/`/`/`-`), static factory methods (`Vec2.zero()`, `Vec2.up()`, etc.), and a full static method set (`lerp`, `dot`, `cross`, `length`, `distance`, `angle`, `rotate`, `reflect`, `project`, `perpendicular`, `clamp`). That's worth studying as a reference when exposing our existing `FVec2` more fully to Lua.

**libGDX brings the heaviest collections + math library of the comparison.** Beyond `Vector2`/`Vector3`/`Matrix3`/`Matrix4`/`Quaternion`, it ships triangulators (Delaunay + ear-clipping), spline classes (`Bezier`, `BSpline`, `CatmullRomSpline`), specialized GC-free collections (`Array<T>`, `IntMap`, `ObjectSet`, `OrderedMap`, `Bits`), and a `MathUtils` static class with the usual helpers. The collections in particular exist because Java's standard collections allocate boxed objects on every operation — libGDX rewrote them to be primitive-friendly. We don't have that problem (we already avoid STL), but the breadth is a useful reference for what a "complete" math/utils package looks like.

### 11. Color

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Named colors | Yes (`color.cc` DB) | No | No | No | No | Yes (`RED`, `BLUE`, `RAYWHITE`, …) | Yes (`Color.WHITE`, `Color.RED`, `Color.LIME`, … 25+) |
| RGBA components | Yes | Yes | Yes (`rgba_t`) | Yes (color object) | Yes (`Color` usertype) | Yes (`Color` struct) | Yes (`Color` class with float r/g/b/a) |
| Hex string constructor | No | No | No | No | **Yes** (`Color.color("#RRGGBBAA")`) | No | Yes (`Color.valueOf("#RRGGBBAA")`) |
| HSL / HSV support | No | No | No | Yes (auto-sync) | No | Yes (`ColorToHSV`, `ColorFromHSV`) | Yes (`fromHsv`, `toHsv`) |
| Color interpolation | No | No built-in | No | Yes (`mix`) | No | Yes (`ColorLerp`) | Yes (`Color.lerp`) |
| Color invert | No | No built-in | No | Yes | No | Yes (`ColorInvert`) | Manual (`1 - r`, etc.) |
| Color arithmetic | No | No built-in | Yes (blending) | Yes (operators) | No | Yes (`ColorTint`, `ColorBrightness`, `ColorContrast`) | Yes (`add`, `sub`, `mul`) |
| Gamma correction | No | Yes (sRGB pipeline) | No | No | No | Yes (`ColorLinearToGamma`) | Manual (shader) |

**Gaps to fill**: HSL/HSV support and color interpolation are valuable for procedural color palettes, day/night cycles, and UI theming. Raylib has the most complete color utility set of any comparison engine (`ColorLerp`, `ColorTint`, `ColorBrightness`, `ColorContrast`, `ColorInvert`, `ColorFromHSV`, `ColorAlpha`, gamma conversion). These are all one-liners to implement if we want parity.

### 12. UI System

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| UI framework | No | No | No | No | Overlay system (Labels, Cursors) + HUD decorator | `raygui` (separate single-header) | **Yes** (Scene2D.UI: full retained-mode widget system) |
| Text wrapping | No (see font system) | Yes (`printf`) | Yes (`font_draw`) | No | No | No (manual) | Yes (`Label` with `setWrap(true)`) |
| Text alignment | No (see font system) | Yes (L/C/R/J) | Yes (L/C/R) | No | No | No (manual via `MeasureText`) | Yes (`Label.setAlignment`) |
| Colored text spans | No (ANSI escapes) | Yes (inline) | No | No | No | No | Yes (`BitmapFont` markup `[#RRGGBB]text[]`) |
| Word wrap measurement | No | Yes (`getWrap`) | No | No | No | Yes (`MeasureTextEx`) | Yes (`GlyphLayout`) |
| Immediate-mode widgets | No | No | No | No | No | **Yes** (raygui: buttons, sliders, spinners, text boxes, dropdowns, color pickers, 20+ controls) | No (retained mode); `gdx-imgui` extension exists |
| Retained-mode widgets | No | No | No | No | No | No | **Yes** (40+ widgets: `Button`/`TextField`/`Slider`/`SelectBox`/`List`/`Tree`/`Window`/`Dialog`/`Table`/`Stack`/...) |
| Layout system | No | No | No | No | No | No | **Yes** (`Table` with cell-based layout, expand/fill/pad/align rules) |
| Skinning / theming | No | No | No | No | No | Themed via `style` enum | **Yes** (`Skin` JSON + `TextureAtlas` of 9-patches) |
| Localization / i18n | No | No | No | No | **Yes** (`locales/<lang>.json` + `_()` lookup, OS auto-detect) | No | **Yes** (`I18NBundle` over Java `ResourceBundle` + `MessageFormat`) |
| HUD overlay | No | No | No | No | **Yes** (HUD decorator with layout, character, items) | No | Yes (`Stage` with overlay viewport) |

**Gaps to fill**: Love2D's text layout features (wrapping, alignment, colored spans) remain essential building blocks — our SDF font system already has these implemented and is tracked in the Font system design doc.

Three features stand out in this section that no other comparison engine has:

- **`raygui`** (Raylib) is a single-header immediate-mode UI library with 20+ widgets. It's separate from raylib core but written to the same philosophy — tiny, no dependencies, C99. If we want a quick-start UI for dev tools, inspectors, or in-game menus without building our own retained-mode framework, vendoring something `raygui`-shaped is the obvious path.
- **Localization** (Carimbo) is exposed as a one-character global function `_("key")` that looks up a string in `locales/<lang>.json` with OS language auto-detection. This is the simplest possible i18n API and matches what GNU gettext does in C. If we ever want to ship localized games, this is the pattern to copy.
- **Scene2D.UI** (libGDX) is the most complete first-party UI library in the comparison. 40+ widgets, a Table-based layout system (modeled after Java Swing's TableLayout), JSON-driven Skin/9-patch theming, action-based animation built in, and a full input event bubbling system. It's massive and opinionated — the opposite of `raygui`'s philosophy — but it's the existence proof that you can ship a real retained-mode UI as a C-level extension on top of a primitive renderer. The `Skin` + `Drawable` + 9-patch atlas pipeline is the part most worth borrowing if we ever build retained-mode UI.

### 13. Scene / State Management

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Scene abstraction | No | No | Yes (`scene_t`) | No (use object tree) | **Yes** (`SceneManager` with JSON + Lua pair per scene) | No | Yes (`Game` + `Screen` interface) |
| Scene switching | No | No | Yes (deferred swap) | No | **Yes** (`scenemanager:set(name)`, deferred to next update) | No | Yes (`game.setScreen(new GameScreen())`) |
| Resource cleanup on switch | No | No | Yes (automatic) | No | **Yes** (`scenemanager:destroy(name)` or `"*"`; clears `package.loaded`) | No | Manual via `Screen.dispose()` |
| Scene lifecycle callbacks | No | No | enter/exit | No | **Yes** (`on_enter`/`on_leave`/`on_loop`/`on_tick`/`on_touch`/`on_motion`/`on_keypress`/`on_keyrelease`/`on_text`/`on_camera`) | No | Yes (`show`/`hide`/`render`/`resize`/`pause`/`resume`/`dispose`) |
| Scene decorators | No | No | No | No | **Yes** (wrap patterns: `ticker.wrap`, `HUD`, `sentinel`) | No | No |

**Gaps to fill**: A scene system provides structure for game states (menu, gameplay, pause, game over). Even a minimal one (init/update/draw/cleanup callbacks with switching) reduces boilerplate.

**Carimbo is the state-management leader** in this comparison. Its `SceneManager` is the only one with a full lifecycle (`on_enter`/`on_leave`/`on_loop`/`on_tick` + input callbacks), deferred scene swap (avoiding mid-frame inconsistency), automatic `package.loaded` cleanup for Lua module reloading, and per-scene physics worlds. The "scene as a Lua module with a `sentinel()` call before `return scene`" pattern is also cute — the sentinel enables GC-based lifecycle tracking. Worth adapting if we build a scene system.

**libGDX's `Game` + `Screen` pattern is the simplest scene API in the comparison** — `Game` is an `ApplicationListener` that holds the current `Screen`, and `Screen` is an interface with `show`/`render(delta)`/`resize`/`pause`/`resume`/`hide`/`dispose`. Switching is `game.setScreen(new GameScreen())`. The lifecycle is explicit (no decorators, no module-cleanup magic), the contract is small, and the user is responsible for `dispose()`-ing the previous screen. It's the minimum useful scene system, and a good starting point if we want to ship something quickly.

Save/load is now a separate section below — it's too important to bury under state management.

### 14. Save / Persistence / Achievements

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Per-user save directory resolver | No | Yes (sandboxed `save/`) | Yes (userdata path) | No | Yes (PhysFS write dir) | No | Yes (`Gdx.files.local`) |
| Save game state (structured) | No | Yes (via `love.filesystem` + serpent/bitser) | Yes (userdata API) | No | **Yes** (`Cassette` KV store) | Simple int slots only (`SaveStorageValue(pos, value)`) | Yes (`Preferences` KV + `Json` class for object serialization) |
| Key/value API | No | No (files only) | Partial | No | **Yes** (`cassette:get/set`) | **Yes** (int-indexed, not string-keyed) | **Yes** (`Preferences.putString/putInt/putBool/getFloat/...`) |
| Atomic writes / corruption resistance | No | Yes (temp file + rename) | Depends | No | Depends on miniaudio/PhysFS impl | No | Yes (`Preferences.flush` writes via temp + rename) |
| Achievements | No | No | No | No | **Yes** (built-in `Achievement` type) | No | No (Google Play Games / Steam via extensions) |
| Statistics tracking | No | No | No | No | Possible via Cassette | No | Manual (use Preferences) |
| Leaderboards | No | No | No | No | `User` / `Buddy` types (social) | No | No (extensions) |
| Settings persistence | No | Manual (save files) | Manual | No | Via Cassette | Via `SaveStorageValue` or files | **Yes** (`Preferences` is the standard pattern) |
| Cloud sync | No | No | No | No | No | No | No core |
| Namespaced keys | No | N/A | N/A | N/A | Unknown (single KV namespace) | No (int slots) | Yes (each `Preferences` instance is its own namespace) |

**Carimbo is the only engine in the comparison with built-in achievements**, and the only one with a single-call KV API exposed as a Lua global. Its `Cassette` (a simple KV store) and `Achievement` type are first-class engine concepts — both features that every shippable game needs and that we currently have zero support for. Raylib has a trivial int-indexed storage API (you `SaveStorageValue(position, value)` where `position` is a small integer slot) which covers simple save-slot cases but won't scale to structured game state.

**libGDX's `Preferences` is the closest match to our planned save API**: it's a typed KV store (string/int/long/float/bool keys, no nesting), each instance is its own namespace (file), `flush()` writes via temp file + rename for atomicity, and it works on every platform including HTML5 (via LocalStorage) and Android (via `SharedPreferences`). What it doesn't have is structured/nested values, transactions, or range queries — for those, libGDX games typically combine `Preferences` with the `Json` class (for serializing whole objects) or `FileHandle.writeBytes`. Our planned SQLite KV approach gives us range queries and transactions for free, which is the main reason to prefer SQLite over a Preferences-style flat file.

**Our planned approach**: a namespaced SQLite KV store reusing the sqlite3 amalgamation we already vendor, stored at a platform save directory. See [Save and persistence](Save%20and%20persistence.md) for the full design. Key differentiators from Carimbo's Cassette:

- **Namespaces** — all keys are `(namespace, key)` pairs, so we can enumerate "all achievements" or "all save slots" cheaply via a range scan.
- **Atomic writes via WAL** — SQLite provides crash safety for free.
- **Built on existing infrastructure** — no new library, same `Allocator` integration we already have for the asset DB.

This is arguably the highest-leverage feature we could borrow from Carimbo, because it's the only thing in the comparison that's both **genuinely missing** from our engine and **necessary to ship a complete game**.

### 15. Platform & Distribution

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Windows | Stubbed | Yes | Yes | Yes | Yes | Yes | Yes |
| macOS | No | Yes | Yes | Yes | Yes | Yes | Yes |
| Linux | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Web / WASM | No | Experimental | Yes (Emscripten) | Yes (Emscripten) | **Yes (primary target)** | Yes (Emscripten) | Yes (GWT — JS transpile, not WASM) |
| Android | No | Yes | No | No | Partial | Yes | **Yes (primary target)** |
| iOS | No | Yes | No | No | Partial | No | Yes (RoboVM/MOE) |
| Raspberry Pi / DRM | No | No | No | No | No | **Yes** | Possible via Linux desktop |
| Headless backend | No | No | No | No | No | No | **Yes (`HeadlessApplication` for tests/servers)** |
| Single-exe packaging | Yes | Yes (fused) | Yes (compiled in) | Yes (zip append) | Cartridge zip (`.rom`) | Manual (or `rres`) | Fat JAR + bundled JRE (jpackage) |
| Hot reload | Yes | No | No | No | No | No | No |
| Save directory | No | Yes (sandboxed) | Yes (userdata) | No | Yes (PhysFS) | No | Yes (`Gdx.files.local`/`external`) |
| File drop support | No | Yes | No | No | No | Yes | Yes (LWJGL3 backend) |
| Clipboard access | No | Yes | No | No | No | Yes | Yes (`Gdx.app.getClipboard`) |

**Gaps to fill**: Web/WASM is the most impactful platform gap — it enables instant sharing and playtesting. Windows support needs to be completed. A sandboxed save directory is necessary for persisting game state.

Carimbo being **WASM-first** (the engine is explicitly designed for `carimbo.games` hosting) is a useful existence proof that a modern C++ + SDL + Box2D engine can ship cleanly to the browser. Raylib's platform matrix is the broadest in the comparison, reaching Raspberry Pi and DRM (framebuffer) targets that nobody else even considers — relevant if we ever care about embedded or handheld targets.

**libGDX is the only engine in the comparison with a first-class headless backend.** `HeadlessApplication` instantiates the entire framework (`Gdx.files`, `Gdx.app`, `Gdx.input`) without a window, OpenGL context, or audio device — it just runs the `ApplicationListener.render()` callback on a fixed timestep and lets your test code drive it. This is the standard pattern for libGDX game testing: spin up a headless app in a JUnit `@BeforeEach`, feed events into your `InputProcessor` directly, run the game for N frames, assert on game state. It's the cleanest existing answer to the "how do I run my game in CI" problem in this comparison and is the model worth following for our [Test input system](Test%20input%20system.md).

### 16. Debugging & Development

| Feature | Ours | Love2D | high_impact | Anchor | Carimbo | Raylib | libGDX |
|---|---|---|---|---|---|---|---|
| Hot reload | Yes | No | No | No | No | No | No (JVM hot-swap is limited; `gdx-liftoff` adds JRebel-style flow) |
| Fennel (lisp) support | Yes | No | No | No | No | No | No (Kotlin/Scala instead) |
| Debug font | Yes | Yes | Yes (`font_tool`) | No | Yes (overlay labels) | Yes (`DrawText` with default font) | Yes (default `BitmapFont`) |
| Performance metrics | Yes (timing) | Yes (draw calls, memory) | Yes (entities, checks, draw calls, ms) | Yes (fps, draw calls) | Yes (via overlay labels) | Yes (FPS, frame time) | Yes (`Gdx.graphics.getFramesPerSecond`, `GLProfiler` for draw call counts) |
| Error screen | No | Yes (stack trace) | No | No | No | No | JVM stack trace to console |
| Type stubs (IDE) | Yes | No | No | No | No | N/A (C API) | N/A (Java has full type info) |
| CLI tooling | Yes (init/run/package/stubs) | No (external tools) | No | No | No | No | `gdx-setup` / `gdx-liftoff` (project generators only) |
| Crash reporting | No | No | No | No | **Yes** (Sentry integration) | No | No core (Firebase Crashlytics on Android via ext) |
| Input record / replay | No | No | No | No | No | **Yes** (automation events) | Via headless tests, no built-in record format |
| Headless test backend | No | No | No | No | No | No | **Yes** (`HeadlessApplication`) |
| Trace logging | Yes | Yes | Yes | Yes | Yes | Yes (`TraceLog` with levels) | Yes (`Gdx.app.log/debug/error`) |

**Our advantages**: Hot reload, Fennel support, CLI tooling, and type stub generation remain genuine differentiators not found in any comparison engine. Carimbo, Raylib, and libGDX each add one unique dev feature:

- **Carimbo has Sentry crash reporting built in** (`with_sentry(dsn)`). Useful for shipping commercial games with telemetry.
- **Raylib has input automation events** — record a session to a file, replay deterministically. Pairs naturally with our Test input system design doc.
- **libGDX has a headless backend** designed for unit/integration tests. `HeadlessApplication` runs the entire framework without a window, GL context, or audio device, and is the standard way libGDX games get covered by JUnit. Combined with Raylib's automation-events idea, this is the strongest evidence that automated testing is the right next investment for our engine.

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

13. **Math utilities** — Lerp, distance, angle, direction, noise, easing functions. Carimbo's `Vec2` sol2 binding is the ergonomics target; libGDX's `Interpolation` static-field pattern is the easing-curve reference.
14. **Text layout** — Word wrap, alignment (left/center/right). Tracked in the Font system doc.
15. **More drawing primitives** — Outlined shapes, rounded rectangles, ellipses, arcs, splines. Raylib's `rshapes.c` is the reference.
16. **Color utilities** — HSV, interpolation, arithmetic. Raylib has the most complete set; libGDX matches on `valueOf("#hex")` and `lerp`.
17. **Stencil masking** — For fog-of-war, UI clipping, spotlight effects
18. **Web/WASM target** — Instant sharing and playtesting. Carimbo is an existence proof that modern C++ + SDL + Box2D ships cleanly to WASM.
19. **Localization / i18n** — Carimbo's `_("key")` + `locales/<lang>.json` pattern is the simplest possible starting point; libGDX's `I18NBundle` is the heavyweight alternative.
20. **Test input system + headless mode** — The combination of Raylib's input automation events and libGDX's `HeadlessApplication` backend is the strongest cross-engine signal that this is the right next investment. See [Test input system](Test%20input%20system.md).
21. **Viewport abstraction** — libGDX is the only comparison engine with `FitViewport`/`FillViewport`/`ExtendViewport`/`ScreenViewport`. The split between "where the camera looks" and "how the world fits the screen" is genuinely useful and we currently bake both concerns into the camera + canvas system.
22. **Entity utilities** — Tagging, queries, safe references, draw-order sorting. (Explicitly not an ECS.)
23. ~~**Pixel-perfect rendering** — Integer scaling for pixel art games~~

## Appendix: What we do well

Features where our engine equals or exceeds the competition (now against six reference engines):

- **Hot reload** — None of Love2D, high_impact, Anchor, Carimbo, Raylib, or libGDX have this. This remains a major development speed advantage.
- **Fennel scripting** — Unique among these engines. Lisp-family language for those who prefer it.
- **Asset pipeline** — SQLite-backed with checksums, file watching, and parallel loading is more sophisticated than any comparison engine. Carimbo uses PhysFS with a zip cartridge, Raylib ships plain files, libGDX has a runtime `AssetManager` over plain files. Neither has our incremental hot-reload story.
- **SDF fonts** — Resolution-independent font rendering. Love2D uses FreeType bitmap rasterization, high_impact uses bitmap fonts, Anchor uses FreeType, Carimbo uses bitmap atlas, Raylib uses stb_truetype bitmap atlas, libGDX uses bitmap or FreeType-generated atlases. Our SDF approach scales to any size without regenerating atlases — unique in this comparison.
- **CLI tooling** — `game init`, `game run`, `game package`, `game stubs` — a complete workflow. libGDX has project generators (`gdx-setup`, `gdx-liftoff`) but no run/package/stubs CLI; nothing else has anything.
- **Memory management** — Arena allocators with a fixed 4 GB budget, explicit allocator passing. More controlled than Love2D, Anchor, Carimbo, or libGDX (which inherit their allocators from their dependency graphs / from the JVM); comparable to high_impact's bump allocator and Raylib's plain malloc.
- **Type stubs** — IDE completion for the Lua API. No comparison engine generates these (libGDX gets type info for free from Java's static types).
- **Timer/tween/easing** — Implemented with cooldowns, springs, and 20+ easing curves. Matches Anchor and libGDX's `Interpolation` (the gold standards) and exceeds everything else.
- **Camera system** — Follow, deadzone, bounds, shake, zoom, parallax. More complete than Raylib's bare `Camera2D`, libGDX's `OrthographicCamera`, and Carimbo's script-driven `on_camera`; on par with Anchor. libGDX's `Viewport` separation is the one design idea worth borrowing here.
- **Animation system** — Frame sequences, loop/once/bounce, flip, timing. Matches Carimbo and libGDX, exceeds Love2D and Raylib (which have no built-in animation at all).

## Notes on the newly added engines

**Carimbo** (`willtobyte/carimbo`) is the comparison engine most philosophically similar to ours in tech stack — C++ core with Lua scripting via sol2, SDL + Box2D + miniaudio + PhysFS, WebAssembly target. It's also the most ambitious in scope, shipping built-in particle systems, tilemaps, animation, scene management, persistent saves, achievements, and localization. The tradeoff is that it's a far more opinionated framework: games are declarative JSON scenes + per-object Lua modules + an EnTT entity registry, not the imperative toolbox we offer. Read it as "what our engine could look like if we leaned hard into framework territory," and borrow individual features (Cassette, SceneManager, Vec2 binding shape, localization) without adopting the overall architecture.

**Raylib** (`raysan5/raylib`) is the comparison engine most philosophically different from ours — no scripting, C99 throughout, procedural API. Its value as a reference is its breadth: the richest primitive drawing set, the most complete color utility set, the broadest platform matrix, and genuinely unique features (automation events, `Camera2D` simplicity, raymath's easing curve library). It's also the engine with the largest community and the most third-party extensions (`raygui`, `physac`, `rres`, raylib-extras), which means for any feature we consider building, there's usually a small raylib-style reference implementation to study.

**libGDX** (`libgdx/libgdx`) is the most mature, most "industrial" engine in the comparison — Apache 2.0, in active production since 2009, used to ship many commercial titles (most famously *Slay the Spire*). Its value as a reference is the breadth and the maturity: the richest UI system (Scene2D.UI with `Skin`/`Table`/40+ widgets), the cleanest easing-curve API (`Interpolation`), a `Viewport` abstraction that nobody else has, full Tiled (TMX) integration, complete Box2D bindings, and — most relevant for our roadmap — a **`HeadlessApplication`** backend designed specifically for unit and integration testing without a window or audio device. Combined with Raylib's automation-events feature, libGDX is the strongest piece of evidence that "ship the engine with first-class testability" is a real, achievable goal that meaningfully differentiates the engines that have it from the ones that don't. The tradeoff with libGDX is verbosity (Java + retained-mode UI + many-classes-per-feature) and its general "give you the GL handle" attitude — there are very few high-level conveniences layered over the primitives, so most non-trivial games rewrap a lot of it themselves.
