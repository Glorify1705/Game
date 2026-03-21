# Engine Comparison: Feature Gap Analysis

This document compares our engine against three reference 2D game engines — **Love2D**, **high_impact**, and **Anchor** — to identify missing features needed to ship complete videogames.

## Engine profiles

| | Our engine | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| **Language** | C++17 core, Lua 5.1 scripting | C++ core, Lua (LuaJIT) scripting | Pure C99, user code in C | C99 core, Lua 5.4 scripting |
| **Size** | ~21,500 LOC | Large (~200K+ LOC) | ~4,800 LOC | ~10,000 LOC (C) + Lua framework |
| **Renderer** | OpenGL 3.3+, SDL2 | OpenGL/ES, SDL2 | OpenGL/Metal/Software, SDL2 or Sokol | OpenGL 3.3/WebGL 2, SDL2 |
| **Physics** | Box2D | Box2D | Custom AABB + slopes | Box2D 3.x |
| **Audio** | SDL2 callback, dr_wav + stb_vorbis | OpenAL Soft | QOA + pl_synth | miniaudio + Vorbis |
| **Asset format** | SQLite DB + hot-reload | PhysFS (zip-based) | QOI/QOA files | Files or zip appended to exe |
| **Platforms** | Linux (Windows stubbed) | Win/Mac/Linux/Android/iOS | Win/Mac/Linux/Web (WASM) | Win/Mac/Linux/Web (WASM) |
| **License** | Proprietary | zlib/libpng | MIT | MIT |

## Feature-by-feature comparison

### 1. Rendering

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Sprite/image drawing | Yes | Yes | Yes | Yes |
| Sprite atlas / spritesheet | Yes (JSON/XML) | Yes (Quad-based) | Yes (tileset) | Yes (spritesheet) |
| Batch rendering | Yes (command buffer) | Yes (auto + SpriteBatch) | Yes (texture atlas batching) | Yes (deferred command queue) |
| Transform stack | Yes (push/pop, mat4) | Yes (push/pop) | Yes (push/pop, mat3) | Yes (push/pop) |
| Filled rectangles | Yes | Yes | Yes (via draw) | Yes |
| Circles | Yes (fill only) | Yes (fill + outline) | No built-in | Yes (fill + outline) |
| Triangles | Yes | Yes (polygon) | No built-in | Yes (fill + outline) |
| Lines / polylines | Yes (configurable width) | Yes (join styles, width) | No built-in | Yes (capsule lines) |
| Ellipses | No | Yes | No | No |
| Arcs | No | Yes | No | No |
| Rounded rectangles | No | Yes | No | Yes |
| Arbitrary polygons | No | Yes | No | Yes (up to 8 verts) |
| Points | No | Yes | No | No |
| Gradient fills | No | No | No | Yes (H/V gradients) |
| Custom shaders | Yes (GLSL) | Yes (GLSL dialect) | Yes (GLSL) | Yes (fragment only) |
| Post-processing effects | No | Yes (via Canvas + Shader) | Yes (CRT effect built-in) | Yes (per-layer shaders) |
| Canvas / off-screen render | No | Yes (Canvas, MRT) | No | Yes (FBO layers) |
| Blend modes | No | Yes (8+ modes) | Yes (Normal, Additive) | Yes (Alpha, Additive) |
| Stencil masking | No | Yes | No | Yes |
| MSAA | Yes | Yes | No | No |
| Mesh / custom geometry | No | Yes (Mesh object) | No | No |
| Pixel-perfect scaling | No | No | Yes (discrete scaling) | Yes (rough filter mode) |
| Color tinting per draw | Yes | Yes | Yes | Yes |
| Screenshots | Yes (PNG) | Yes (async) | No | No |
| HiDPI support | No | Yes | No | No |
| Mipmaps | No | Yes | Yes (optional) | No |

**Gaps to fill**: Canvas/off-screen rendering is the biggest gap — it's the foundation for post-processing, minimaps, UI layers, and split-screen. Blend modes are essential for particle effects and lighting. Stencil masking enables fog-of-war and UI clipping. Outline drawing modes for circles/rectangles are useful for debugging and UI.

### 2. Animation

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Frame-based sprite animation | No (logic in Lua) | No built-in (libraries) | Yes (anim_def_t) | Yes (spritesheet animation) |
| Animation sequences | No | No built-in | Yes (arbitrary frame order) | Yes (frame indices) |
| Loop / once / bounce modes | No | No built-in | Yes (loop + stop) | Yes (loop, once, bounce) |
| Flip X/Y | No | No built-in | Yes | No built-in |
| Per-frame callbacks | No | No built-in | No | Yes |
| Animation rewind/goto | No | No built-in | Yes | Yes (set_frame, reset) |

**Gaps to fill**: A built-in animation system is highly valuable. All action-oriented engines (high_impact, Anchor) include one. At minimum: define a sequence of frames from a spritesheet, specify frame timing, support loop/once modes, and flip.

### 3. Camera

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Camera abstraction | No | No built-in (Transform) | Yes (camera_t) | Yes (camera object) |
| Follow target entity | No | No built-in | Yes (with snap) | Yes (lerp follow) |
| Smooth interpolation | No | No built-in | Yes (speed-based) | Yes (exponential lerp) |
| Dead zone | No | No built-in | Yes | No |
| Look-ahead | No | No built-in | Yes | No |
| Bounds clamping | No | No built-in | Yes (map bounds) | Yes (set_bounds) |
| Camera shake | No | No built-in | No | Yes (trauma, push, sine, square) |
| Zoom | No | No built-in | No | Yes |
| World/screen coord conversion | No | Yes (Transform) | No | Yes (to_world / to_screen) |
| Handcam (subtle drift) | No | No built-in | No | Yes |

**Gaps to fill**: A camera system is critical for any game with scrolling. Must-haves: follow-target with smooth interpolation, bounds clamping, screen-shake, and world/screen coordinate conversion. Anchor's camera is the gold standard here with trauma-based shake and composable effects.

### 4. Tilemap

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Tile map rendering | No | No built-in (STI library) | Yes (map_t) | No |
| Parallax scrolling | No | No built-in | Yes (distance factor) | No |
| Tile collision | No | No built-in | Yes (54 slope types!) | No |
| Animated tiles | No | No built-in | Yes | No |
| Multiple layers | No | No built-in | Yes (4 bg + collision) | No |
| Foreground / background | No | No built-in | Yes | No |
| Tile repeat / wrap | No | No built-in | Yes | No |
| Level editor | No | No built-in | Yes (Weltmeister) | No |
| Level loading from JSON | No | No built-in | Yes | No |

**Gaps to fill**: high_impact is the clear leader here. A tilemap system with collision detection is essential for platformers, top-down RPGs, and many other genres. The slope system (54 tile types) is a standout feature. At minimum: load tile data, render with camera offset, query tiles at positions, and detect tile collisions.

### 5. Entity / Game Object System

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Entity framework | No | No | Yes (entity_t + vtab) | Yes (object tree + mixins) |
| Entity types / classes | No | No | Yes (X-macro compile-time) | Yes (class inheritance) |
| Entity groups / tags | No | No | Yes (bitmask groups) | Yes (tag system) |
| Entity queries | No | No | Yes (by type, name, proximity, location) | Yes (by tag) |
| Entity references (safe) | No | No | Yes (survives death) | Yes (link system) |
| Draw order sorting | No | No | Yes (draw_order field) | No (FIFO call order) |
| Collision groups | No | No | Yes (check_against bitmask) | Yes (tag-based filtering) |
| Kill / cleanup | No | No | Yes (auto on health <= 0) | Yes (deferred cleanup) |
| Spawn from level data | No | No | Yes (JSON entity settings) | No |

**Gaps to fill**: While our engine intentionally leaves entity management to Lua, having a lightweight entity query and lifecycle system reduces boilerplate. Key features: tagging/grouping for collision filtering, safe entity references, draw-order sorting, and proximity queries.

### 6. Physics & Collision

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Rigid body dynamics | Yes (Box2D) | Yes (Box2D) | Yes (custom) | Yes (Box2D 3.x) |
| Static bodies | Yes | Yes | Yes | Yes |
| Dynamic bodies | Yes (box, circle) | Yes (all shapes) | Yes (AABB only) | Yes (circle, box, capsule, polygon) |
| Kinematic bodies | No | Yes | No | Yes |
| Collision callbacks | Yes (begin/end) | Yes (begin/end/pre/post) | Yes (collide, touch) | Yes (begin/end, sensor, hit) |
| Sensors / triggers | No | Yes | Yes (touch groups) | Yes |
| Joints | No | Yes (12 types) | No | No |
| Raycasting | No | Yes | No | Yes |
| Spatial queries | No | Yes (AABB query) | No | Yes (point, circle, AABB, box, capsule, polygon, ray) |
| Slope collision | No | No | Yes (54 tile slopes) | No |
| One-way platforms | No | No | Yes | No |
| Edge / chain shapes | No | Yes | No | No |
| Mass-based resolution | No | Yes (Box2D) | Yes (custom) | Yes (Box2D) |
| Gravity per entity | No | Yes (Body) | Yes (gravity multiplier) | Yes (gravity scale) |
| Bullet mode | No | Yes | No | Yes |
| Capsule shape | No | No | No | Yes |
| Polygon shape | No | Yes (8 verts) | No | Yes (8 verts) |

**Gaps to fill**: Our Box2D integration is minimal. Missing critical features: sensors (for trigger zones, item pickups), raycasting (for line-of-sight, laser beams), spatial queries (for area effects, AI detection), kinematic bodies (for moving platforms), and joints (for ropes, chains, vehicles). Capsule shapes are great for character controllers.

### 7. Input

| Feature                             | Ours | Love2D | high_impact   | Anchor |
| ----------------------------------- | ---- | ------ | ------------- | ------ |
| Keyboard press/hold/release         | Yes  | Yes    | Yes           | Yes    |
| Mouse position                      | Yes  | Yes    | Yes           | Yes    |
| Mouse buttons                       | Yes  | Yes    | Yes           | Yes    |
| Mouse wheel                         | Yes  | Yes    | No            | Yes    |
| Gamepad buttons                     | Yes  | Yes    | Yes           | Yes    |
| Gamepad analog axes                 | Yes  | Yes    | Yes           | Yes    |
| Action binding (abstract)           | No   | No     | Yes           | Yes    |
| Text input                          | Yes  | Yes    | Yes (capture) | No     |
| Touch input                         | No   | Yes    | No            | No     |
| Input chords / sequences            | No   | No     | No            | Yes    |
| Hold duration detection             | No   | No     | No            | Yes    |
| Mouse grab / relative mode          | No   | Yes    | No            | Yes    |
| Custom cursor                       | No   | Yes    | No            | No     |
| Mouse world position (camera-aware) | No   | No     | No            | Yes    |

**Gaps to fill**: Action binding is the key missing feature — it abstracts physical buttons into game actions ("jump", "shoot") allowing easy rebinding and multi-device support. Mouse world position (accounting for camera transform) is a must-have for any game with a camera.

### 8. Audio

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Sound effect playback | Yes (WAV, OGG) | Yes (many formats) | Yes (QOA) | Yes (OGG) |
| Music / streaming | Yes (streaming) | Yes (stream source) | Yes (QOA streaming) | Yes (multi-channel) |
| Per-source volume | Yes | Yes | Yes | Yes |
| Global volume | Yes | Yes | Yes | Yes |
| Pitch control | No | Yes | Yes | Yes |
| Panning (stereo) | No | Yes (3D position) | Yes (pan control) | No |
| Looping | No | Yes | Yes | Yes |
| 3D spatialized audio | No | Yes (OpenAL) | No | No |
| Audio effects (reverb, etc.) | No | Yes (8 effect types) | No | No |
| Procedural / synth audio | No | No | Yes (pl_synth) | No |
| Microphone recording | No | Yes | No | No |
| Playlist system | No | No | No | Yes (crossfade, shuffle) |
| MP3 / FLAC / tracker formats | No | Yes | No | No |
| Pause / resume individual | No | Yes | Yes | Yes |
| Seek / tell | No | Yes | No | Yes |

**Gaps to fill**: Pitch control, looping, and per-source pause/resume are basic missing features. Panning is important for game feel. A playlist system (like Anchor's) is nice-to-have for music.

### 9. Timer / Tween / Easing

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Delta time | Yes | Yes | Yes | Yes |
| Game vs real time | Yes (pause support) | No built-in | Yes (time_scale) | Yes (time_scale) |
| Time scale (slow-mo) | No | No built-in | Yes | Yes |
| Delayed callbacks | No | No built-in | No | Yes (timer:after) |
| Repeating timers | No | No built-in | No | Yes (timer:every) |
| Duration callbacks | No | No built-in | No | Yes (timer:during) |
| Tweening | No | No built-in | No | Yes (timer:tween) |
| Easing functions | No | No built-in | No | Yes (20+ curves) |
| Cooldowns | No | No built-in | No | Yes (timer:cooldown) |
| Spring physics | No | No built-in | No | Yes (damped oscillator) |

**Gaps to fill**: Timer/tween infrastructure is one of the biggest productivity gaps. Delayed callbacks, repeating timers, and tweens are used constantly in game code (screen transitions, damage flashes, spawn waves, UI animations). Easing functions and spring physics add polish. Anchor's timer system is the gold standard.

### 10. Math & Utilities

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| 2D vectors | Yes (FVec2, etc.) | No (use tables) | Yes (vec2_t) | No (use x,y args) |
| 3D/4D vectors | Yes | No | No | No |
| Matrices (2x2, 3x3, 4x4) | Yes | No (Transform object) | Yes (mat3) | No |
| Dot product | Yes | No built-in | Yes | No |
| Normalize | Yes | No built-in | Yes | Yes (function) |
| Distance | No | No built-in | Yes (entity_dist) | Yes (function) |
| Angle between points | No | No built-in | Yes (entity_angle) | Yes (function) |
| Lerp | No | No built-in | No | Yes (+ framerate-independent) |
| Clamp | Yes | No built-in | No | Yes |
| Snap to grid | No | No built-in | No | Yes |
| Sign | No | No built-in | No | Yes |
| Direction from angle | No | No built-in | No | Yes |
| Reflect | No | No built-in | No | Yes |
| Perlin / simplex noise | No | Yes (1-4D) | Yes (2D) | Yes |
| Bezier curves | No | Yes | No | No |
| Triangulation | No | Yes | No | No |
| Seeded RNG | Yes (PCG) | Yes (platform-independent) | No built-in | Yes (PCG32) |
| Random from distribution | No | Yes (normal) | No | Yes (normal) |
| Weighted random | No | No built-in | No | Yes |
| Array utilities | No | No built-in | No | Yes (30+ functions) |

**Gaps to fill**: Lerp (especially framerate-independent) is used everywhere. Distance, angle-between-points, and direction-from-angle are basics for game logic. Noise generation is essential for procedural content. Array utilities reduce Lua boilerplate significantly.

### 11. Color

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Named colors | Yes (color.cc DB) | No | No | No |
| RGBA components | Yes | Yes | Yes (rgba_t) | Yes (color object) |
| HSL support | No | No | No | Yes (auto-sync) |
| Color interpolation | No | No built-in | No | Yes (mix) |
| Color invert | No | No built-in | No | Yes |
| Color arithmetic | No | No built-in | Yes (blending) | Yes (operators) |
| Gamma correction | No | Yes (sRGB pipeline) | No | No |

**Gaps to fill**: HSL support and color interpolation are valuable for procedural color palettes, day/night cycles, and UI theming.

### 12. UI System

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| UI framework | No | No | No | No |
| Text wrapping | No | Yes (printf) | Yes (font_draw) | No |
| Text alignment | No | Yes (L/C/R/J) | Yes (L/C/R) | No |
| Colored text spans | No | Yes (inline) | No | No |
| Word wrap measurement | No | Yes (getWrap) | No | No |

**Gaps to fill**: No engine provides a full UI framework, but Love2D's text layout features (wrapping, alignment, colored spans) are essential building blocks. At minimum: word-wrapped text with alignment.

### 13. Scene / State Management

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Scene abstraction | No | No | Yes (scene_t) | No (use object tree) |
| Scene switching | No | No | Yes (deferred swap) | No |
| Resource cleanup on switch | No | No | Yes (automatic) | No |
| Save / load game state | No | No | Yes (userdata API) | No |

**Gaps to fill**: A scene system provides structure for game states (menu, gameplay, pause, game over). Even a minimal one (init/update/draw/cleanup callbacks with switching) reduces boilerplate.

### 14. Platform & Distribution

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Windows | Stubbed | Yes | Yes | Yes |
| macOS | No | Yes | Yes | Yes |
| Linux | Yes | Yes | Yes | Yes |
| Web / WASM | No | Experimental | Yes (Emscripten) | Yes (Emscripten) |
| Android | No | Yes | No | No |
| iOS | No | Yes | No | No |
| Single-exe packaging | Yes | Yes (fused) | Yes (compiled in) | Yes (zip append) |
| Hot reload | Yes | No | No | No |
| Save directory | No | Yes (sandboxed) | Yes (userdata) | No |
| File drop support | No | Yes | No | No |

**Gaps to fill**: Web/WASM is the most impactful platform gap — it enables instant sharing and playtesting. Windows support needs to be completed. A sandboxed save directory is necessary for persisting game state.

### 15. Debugging & Development

| Feature | Ours | Love2D | high_impact | Anchor |
|---|---|---|---|---|
| Hot reload | Yes | No | No | No |
| Fennel (lisp) support | Yes | No | No | No |
| Debug font | Yes | Yes | Yes (font_tool) | No |
| Performance metrics | Yes (timing) | Yes (draw calls, memory) | Yes (entities, checks, draw calls, ms) | Yes (fps, draw calls) |
| Error screen | No | Yes (stack trace) | No | No |
| Type stubs (IDE) | Yes | No | No | No |
| CLI tooling | Yes (init/run/package/stubs) | No (external tools) | No | No |

**Our advantages**: Hot reload, Fennel support, CLI tooling, and type stub generation are genuine differentiators not found in any comparison engine.

## Priority gap ranking

Based on what's needed to ship complete games, grouped by impact:

### Tier 1 — Blocking (can't ship most games without these)

1. **Camera system** — Follow target, smooth interpolation, bounds, shake, world/screen conversion
2. **Animation system** — Spritesheet frame sequences, loop/once/bounce, flip, timing
3. **Timer / tween / easing** — Delayed callbacks, repeating timers, tweens, easing curves, springs
4. **Canvas / off-screen rendering** — Framebuffers as render targets, compositing, post-processing foundation
5. **Audio improvements** — Looping, pitch, pan, pause/resume per source

### Tier 2 — Important (needed for specific genres or polish)

6. **Tilemap system** — Tile rendering, collision, parallax, multiple layers
7. **Physics expansion** — Sensors, raycasting, spatial queries, kinematic bodies, more shapes
8. **Input action binding** — Map physical buttons to game actions, support rebinding
9. **Scene / state management** — Init/update/draw/cleanup, switching, resource cleanup
10. **Blend modes** — At least alpha, additive, multiply
11. **Time scale** — Global slow-motion / pause, separate game vs real time

### Tier 3 — Nice to have (quality of life and polish)

12. **Math utilities** — Lerp, distance, angle, direction, noise, easing functions
13. **Text layout** — Word wrap, alignment (left/center/right)
14. **More drawing primitives** — Outlined shapes, rounded rectangles, ellipses, polygons
15. **Color utilities** — HSL, interpolation, arithmetic
16. **Stencil masking** — For fog-of-war, UI clipping, spotlight effects
17. **Web/WASM target** — Instant sharing and playtesting
18. **Save/load system** — Sandboxed save directory for game state persistence
19. **Entity utilities** — Tagging, queries, safe references, draw-order sorting
20. **Pixel-perfect rendering** — Integer scaling for pixel art games

## Appendix: What we do well

Features where our engine equals or exceeds the competition:

- **Hot reload** — None of the comparison engines have this. This is a major development speed advantage.
- **Fennel scripting** — Unique among these engines. Lisp-family language for those who prefer it.
- **Asset pipeline** — SQLite-backed with checksums, file watching, and parallel loading is more sophisticated than any comparison engine.
- **SDF fonts** — Resolution-independent font rendering. Love2D uses FreeType bitmap rasterization, high_impact uses bitmap fonts, Anchor uses FreeType. Our SDF approach scales to any size without regenerating atlases.
- **CLI tooling** — `game init`, `game run`, `game package`, `game stubs` — a complete workflow. No comparison engine has this.
- **Memory management** — Arena allocators with a fixed 4GB budget. More controlled than Love2D or Anchor, comparable to high_impact's bump allocator.
- **Type stubs** — IDE completion for the Lua API. No comparison engine generates these.
