---
status: in-design
tags: [renderer, particles, lua-api]
---

# Particle System Design

## What Is a Particle System?

A particle system manages a large number of small, short-lived visual elements
("particles") to create effects like fire, smoke, sparks, rain, explosions,
and trails. Each particle is a tiny sprite (a small textured rectangle) with
its own position, velocity, size, color, and remaining lifetime. Every frame
the engine:

1. **Spawns** new particles from an "emitter" (a source point or shape).
2. **Updates** every live particle: applies gravity, friction, and other
   forces, advances its age, and modulates its visual properties (fading out,
   shrinking, changing color).
3. **Kills** particles whose age exceeds their lifetime.
4. **Draws** all surviving particles to the screen.

The key challenge is doing this efficiently for thousands of particles at
60 frames per second. A naïve approach -- one heap-allocated object per
particle, one individual GPU draw call per particle -- falls apart quickly.
The design below explains how to avoid those pitfalls.

## Glossary

A few terms come up repeatedly. If you're comfortable with these, skip ahead.

**Draw call.** A single command sent from the CPU to the GPU that says "render
this geometry." Each draw call has overhead (driver validation, state setup),
so fewer calls = better. Drawing 10,000 particles as 10,000 separate draw
calls is catastrophically slow. Drawing them as one call with 10,000
"instances" is fast.

**Instanced rendering.** A GPU feature where one draw call renders the same
shape (e.g., a quad) many times, each with different per-instance data
(position, color, size). The GPU processes all instances in parallel. This is
the core trick that makes large particle counts viable.

**Vertex shader.** A small program that runs on the GPU once per vertex. It
transforms vertex positions (e.g., from world coordinates to screen
coordinates) and passes data to the next stage. In our particle system, the
vertex shader takes a unit square (four corners) and scales/rotates/positions
it to form each particle's on-screen rectangle.

**Fragment shader.** A small program that runs on the GPU once per pixel of
each drawn shape. It determines the final color of that pixel, typically by
sampling a texture and multiplying by a vertex color.

**Vertex buffer / VBO (Vertex Buffer Object).** A block of GPU memory that
holds vertex data (positions, colors, texture coordinates). You fill it from
the CPU, then the GPU reads it during rendering. Uploading data to a VBO every
frame is normal and fast -- the GPU is optimized for this.

**Instance buffer.** A vertex buffer where the data advances once per instance
(per particle) rather than once per vertex (per corner of the quad). OpenGL
calls this a "vertex attribute divisor" -- you mark certain attributes as
advancing per instance.

**Texture / sprite.** A 2D image stored on the GPU. Each particle is drawn as
a small rectangle textured with a sprite (a spark image, a smoke puff, etc.).

**UV coordinates.** Numbers in [0, 1] that map a point on a rectangle to a
point in a texture. (0, 0) is one corner, (1, 1) is the opposite. When
multiple sprites are packed into one texture (an "atlas"), each sprite's UVs
are a sub-rectangle of the full [0, 1] range.

**Texture atlas.** A single large texture containing many smaller images packed
together. Using one atlas avoids switching textures between draw calls, which
is expensive.

**Blend mode.** Controls how a newly drawn pixel combines with what's already
on screen. "Alpha blend" makes semi-transparent pixels layer naturally.
"Additive blend" adds the new pixel's brightness to the existing one, creating
a glowing effect -- the standard look for fire, sparks, and magic.

**SoA (Struct of Arrays) vs AoS (Array of Structs).** Two ways to lay out data
for a collection of items:

- **AoS**: `[{x,y,vx,vy,color}, {x,y,vx,vy,color}, ...]` -- each item is a
  contiguous struct. Natural in OOP. Bad for cache when you only need one field.
- **SoA**: `{x: [x0,x1,...], y: [y0,y1,...], vx: [vx0,vx1,...]}` -- each
  field is a contiguous array. Better for cache when processing one field at a
  time (which particle updates do).

**Cache / cache line.** The CPU loads memory in chunks called cache lines
(typically 64 bytes). If your data is contiguous and you access it
sequentially, the hardware prefetcher loads the next chunk before you need it
("cache hit"). If your data is scattered, each access waits for main memory
("cache miss"), which is ~100x slower. SoA layout maximizes cache hits during
particle updates because each update pass only touches the fields it needs.

**L2 / L3 cache.** CPU caches are organized in levels. L1 is the smallest and
fastest (~32 KB per core, ~1 ns). L2 is larger and slower (~256 KB - 1 MB per
core, ~3-10 ns). L3 is shared across cores (~8-32 MB, ~10-30 ns). Main memory
(RAM) is ~100 ns. When particle data fits in L2/L3 during a single update
pass, the update is fast. When it spills to main memory, throughput drops
sharply.

**Allocator.** In this engine, all memory allocation goes through explicit
allocator objects rather than calling `malloc`/`free` directly. This lets
different subsystems use different strategies. The two relevant ones here:
- **System allocator**: wraps mimalloc (a fast general-purpose allocator). Used
  for long-lived allocations like the particle pool itself.
- **Arena allocator**: a bump allocator that hands out memory from a
  pre-allocated block. Extremely fast (just increment a pointer), but you can
  only free everything at once (reset the pointer back to the start). Used for
  temporary per-frame scratch work.

**Hot reload.** The ability to modify game files (scripts, shaders, sprites)
while the game is running and see changes immediately without restarting. Our
engine already supports this for Lua scripts, shaders, and audio. The particle
system design hooks into this existing mechanism.

## Survey of Existing Particle Systems

Before designing ours, here's how other engines approach particles. The range
is wide -- from full entity-per-particle (Impact.js) to GPU compute (Godot
GPUParticles2D). Our design should sit somewhere in between: data-oriented
CPU simulation with instanced GPU rendering.

### Love2D -- ParticleSystem

Love2D's particle system is the closest reference for our engine's scope. It
stores particles in a flat C++ array with implicit pooling -- when a particle
dies, its slot is reused for the next spawned particle rather than being
freed and reallocated. Configuration happens through setter methods:

```lua
ps = love.graphics.newParticleSystem(image, 1000)
ps:setParticleLifetime(2, 5)
ps:setEmissionRate(100)
ps:setSpeed(50, 150)
ps:setColors(1,1,1,1, 0.5,0.5,0.5,0)  -- up to 8 RGBA stops
ps:setSizes(4, 2, 0)                    -- up to 8 size stops
ps:setLinearAcceleration(-20, -50, 20, 0)
```

**Strengths:** Simple API, good defaults, efficient quad batching (groups many
particle quads into one draw call).
**Weaknesses:** Point emission only (particles can only spawn from a single
point, not from a shape like a circle or rectangle), discrete 8-value tables
instead of smooth curves for property animation, no sub-emitters (particles
can't spawn other particles), no collision with the game world. The
setter-heavy API doesn't compose well -- you can't snapshot or diff a
configuration, and there's no way to serialize one to a file.

### a327ex/BYTEPATH (Love2D-based)

BYTEPATH skips Love2D's built-in particle system entirely and rolls its own
using pre-allocated slot arrays (SoA-style -- separate arrays for x, y,
velocity, etc., rather than an array of particle objects). Each particle type
(`ExplodeParticles`, `TrailParticles`, `TargetParticles`) is a separate class
with a 2000-slot pool. Spawning calls `getFreeIndex()` to recycle dead slots.
Property animation comes from a tween library -- a system that smoothly
interpolates a value from A to B over a duration (e.g., shrink size from 5 to
0 over 0.4 seconds) -- rather than built-in curves:

```lua
function ExplodeParticles:add(x, y, vx, vy, size, color)
    local i = self:getFreeIndex()
    self.ps[i] = {x=x, y=y, vx=vx, vy=vy, size=size, color=color,
                  creation_time=love.timer.getTime(), duration=random(0.3, 0.5)}
    tween(self.ps[i].duration, self.ps[i], {size=0, vx=0, vy=0}, 'out')
end
```

**Strengths:** Full control per particle, no GC pressure from slot reuse,
particles can have arbitrary behavior.
**Weaknesses:** Manual bookkeeping, no declarative configuration, each particle
type is a separate class with duplicated update/draw boilerplate. Works for
hundreds, not thousands.

**Key takeaway:** The tween-based approach is simple but doesn't scale -- our
timer/tween system runs in Lua, and calling it per particle would be expensive.
Over-lifetime interpolation should happen in C++.

### Godot -- CPUParticles2D / GPUParticles2D

Godot offers two variants. `CPUParticles2D` keeps a `Vector<Particle>` on the
CPU with a parallel `Vector<float>` GPU buffer (16 floats per particle: 8 for
the transformation matrix that positions/rotates/scales the particle, 4 for
RGBA color, 4 for custom data). The CPU computes particle physics, then copies
the results to GPU memory each frame for rendering. Properties are modulated
over lifetime via `CurveTexture` -- a 1D texture (a single row of pixels)
where the horizontal position represents "how far through its life the particle
is" (0% to 100%) and the pixel value at that position represents the property
value (e.g., size or opacity). This is elegant: reading the curve is just a
texture lookup (extremely fast on GPUs), and artists can edit curves visually.

`GPUParticles2D` moves the entire simulation to the GPU via a
`ParticleProcessMaterial` (a specialized shader program that computes particle
physics instead of rendering pixels). The CPU only tells the GPU where the
emitter is; all velocity/gravity/lifetime math runs on the GPU in parallel.
This scales to millions of particles because the GPU has thousands of cores,
but it's harder to debug and impossible to interact with from game logic.

Both share the same `ParticleProcessMaterial` for configuration:

```
gravity, initial_velocity (min/max), linear_accel (min/max/curve),
radial_accel (min/max/curve), tangential_accel (min/max/curve),
damping (min/max/curve), scale (min/max/curve), color (gradient),
angular_velocity (min/max), hue_shift (min/max/curve)
```

Emission shapes: point, sphere, rectangle, mesh, ring, directed points.

**Strengths:** Curve-based modulation is powerful and artist-friendly,
GPU variant scales to millions, clean separation of emitter config (data)
from simulation (code).
**Weaknesses:** Heavy -- `ParticleProcessMaterial` is a full shader material
with dozens of parameters. The CPU variant must copy its entire float buffer
to GPU memory every frame (a "CPU-GPU sync"), which becomes a bottleneck at
high particle counts. Overkill for our scope but the *data model* (min/max
ranges + curves) is worth borrowing.

### Unity -- Shuriken

Unity's system is module-based: 24 optional modules (Emission, Shape, Velocity
over Lifetime, Color over Lifetime, Size over Lifetime, Noise, Collision, Sub
Emitters, Trails, etc.) that stack additively -- each module independently
modifies the particle, and you only enable the ones you need. Each property
uses `MinMaxCurve` -- a flexible type that can represent a constant value, a
random range, a curve (a graph of value over time), or a random value between
two curves. This single abstraction covers simple and complex effects alike.

```csharp
var main = ps.main;
main.startLifetime = new MinMaxCurve(1f, 3f);
main.startSpeed = new MinMaxCurve(5f, 15f);

var colorOverLifetime = ps.colorOverLifetime;
colorOverLifetime.enabled = true;
colorOverLifetime.color = gradient;
```

**Strengths:** Incredible flexibility via module composition, MinMaxCurve is a
great abstraction (constant, random between two constants, curve, random
between two curves), burst scheduling with repeat counts.
**Weaknesses:** Massive API surface, editor-centric design doesn't map well to
code-only configuration, internal memory model is opaque.

**Key takeaway:** MinMaxCurve is the right abstraction for particle properties.
We can implement a simpler version called `PropertyRamp` (see Data Model
below): a constant value, a random range picked at spawn time, or a series of
N values that the particle linearly interpolates through over its lifetime
(e.g., size goes 4 -> 2 -> 0, meaning it starts at 4, shrinks to 2 halfway
through life, and reaches 0 at death).

### Impact.js -- Entity Particles

Impact.js has no dedicated particle system. Each particle is a full `ig.Entity`
-- the same heavyweight game object used for the player, enemies, and items --
with position, velocity, acceleration, friction, collision, animation. Pooling
via `ig.EntityPool` keeps killed entities in a list for reuse instead of
garbage-collecting them (important in JavaScript where GC pauses can cause
frame hitches).

**Strengths:** Particles can collide, animate, interact with the game world
just like any other entity.
**Weaknesses:** Each entity carries the full overhead of a game object (hash
tables, method dispatch, collision shapes) even though a particle only needs
a position and a color. This limits practical count to low hundreds. Exactly
what we want to avoid.

## Comparison Summary

| | Love2D | BYTEPATH | Godot CPU | Unity | Impact.js |
|---|---|---|---|---|---|
| **Storage** | Flat array | Slot arrays | Vector + GPU buf | Opaque | Entity objects |
| **Pooling** | Implicit | Manual slots | N/A (fixed count) | Implicit | EntityPool |
| **Config model** | Setters | Per-spawn args | Material + curves | Modules + MinMaxCurve | Entity properties |
| **Over-lifetime** | 8-stop tables | External tweens | CurveTexture | MinMaxCurve (24 modules) | Manual code |
| **Emission shapes** | Point only | Manual | Point/sphere/rect/mesh/ring | 6+ shape types | Manual |
| **Rendering** | Batched quads | Manual draw | Instanced | GPU | Entity draw |
| **Practical max** | ~50k | ~500 | ~50k CPU / millions GPU | Millions | ~100 |
| **Hot reload** | N/A | N/A | Material resource | Prefab | N/A |

## Design Goals

1. **No system malloc in the hot path.** The "hot path" is the code that runs
   every frame: spawning, updating, killing, and drawing particles. All memory
   for particles must be allocated up front when the emitter is created (the
   "cold path"), and never during the per-frame loop. Calling `malloc`/`free`
   thousands of times per frame causes fragmentation and unpredictable latency
   spikes from the allocator's internal bookkeeping.

2. **Cache-friendly.** SoA layout for particle data so that each update pass
   (e.g., "apply gravity to all velocities") streams through a contiguous
   array of floats. This maximizes hardware prefetcher effectiveness and
   minimizes cache misses. See the Glossary for a detailed explanation of SoA
   and CPU caches.

3. **Instanced rendering.** One draw call per emitter, not one draw call per
   particle. We already use `glDrawElementsInstanced` elsewhere in the
   renderer. Instanced rendering lets the GPU draw the same quad shape
   thousands of times with different per-particle data (position, size, color)
   in a single call. Without this, 10,000 particles means 10,000 draw calls,
   each with driver overhead, state validation, and pipeline stalls.

4. **Hot-reloadable configuration.** Emitter definitions (how particles look
   and behave) should live in the asset DB as Lua tables. When you edit and
   save a particle definition file while the game is running, the changes
   appear immediately -- same as our existing shader and script hot-reload.

5. **Lua-friendly.** Simple declarative API for common cases (pass a table of
   properties, get an emitter back), with escape hatches for per-particle
   control when needed.

6. **Allocator-aware.** All memory comes from an explicit `Allocator*`,
   fitting the engine's convention. The particle pool itself is allocated from
   the system allocator (long-lived, freed when the emitter is destroyed).
   Frame-temporary scratch (e.g., a buffer used during spawn position
   calculation and discarded at the end of the frame) uses the arena allocator.

7. **10k-100k particles** at 60fps on integrated GPUs. CPU simulation (doing
   the physics math on the CPU, not the GPU) is fine at this scale because
   the data fits comfortably in L2/L3 cache with SoA layout. GPU compute
   (moving the physics to the GPU via compute shaders) can come later if
   needed but adds significant complexity.

## Data Model

### Particle (SoA layout)

Instead of an array of particle structs (AoS), we store parallel arrays for
each property (SoA). Concretely, instead of this:

```
AoS (what you'd write naturally):
  particles[0] = {x=10, y=20, vx=5, vy=-3, age=0.1, size=4, color=red}
  particles[1] = {x=30, y=40, vx=2, vy=-1, age=0.5, size=3, color=blue}
  In memory: [x,y,vx,vy,age,size,color, x,y,vx,vy,age,size,color, ...]
```

We store this:

```
SoA (what we use):
  x[]       = [10, 30, ...]
  y[]       = [20, 40, ...]
  vx[]      = [5,  2,  ...]
  vy[]      = [-3, -1, ...]
  age[]     = [0.1, 0.5, ...]
  size[]    = [4, 3, ...]
  color[]   = [red, blue, ...]
```

Each array is a separate contiguous allocation. This gives optimal cache
behavior during the update loop: the "apply gravity" pass only touches `vx[]`
and `vy[]` -- two tight arrays that fit in cache. With AoS, accessing
`particles[i].vx` would also load `x`, `y`, `age`, `size`, and `color` into
the cache line, wasting 70% of the cache space on data we don't need for this
particular pass.

```cpp
// particles.h
struct ParticlePool {
    // Per-particle state (parallel arrays, all size = max_particles).
    float* x;              // Position x (world pixels).
    float* y;              // Position y (world pixels).
    float* vx;             // Velocity x (pixels per second).
    float* vy;             // Velocity y (pixels per second).
    float* age;            // Seconds since this particle was spawned.
    float* lifetime;       // Total lifetime in seconds (randomized at spawn).
    float* size;           // Current visual size (half-width of the drawn quad).
    float* angle;          // Current rotation angle (radians).
    float* spin;           // Angular velocity (radians per second).
    Color* color;          // Current RGBA color (4 bytes packed).

    uint32_t count;        // Number of live particles (always <= max_particles).
    uint32_t max_particles;
    Allocator* allocator;  // The allocator that owns all the arrays above.
};
```

Live particles are packed contiguously at indices `[0, count)`. When a particle
dies (age >= lifetime), we swap it with the last live particle and decrement
`count`. Visually:

```
Before killing particle 2 (count = 5):
  index:  [0] [1] [2] [3] [4]
  alive:   A   B   C   D   E      <- C dies

Swap C with the last (E), decrement count:
  index:  [0] [1] [2] [3]  | [4]
  alive:   A   B   E   D   |  (dead, ignored)
                             count = 4
```

This is O(1) removal and keeps the arrays dense -- no holes, no free-list
traversal, no branching in the update loop. The tradeoff is that particle
order is not preserved (particles shuffle around), but since they're all tiny
identical-looking sprites, this is invisible.

**Memory cost:** ~52 bytes per particle (10 float arrays * 4 bytes each + 4
bytes for Color, with alignment padding). 100k particles = ~5 MB. All arrays
are allocated once when the emitter is created, and freed once when it's
destroyed. No per-frame allocation ever happens.

**Why not AoS?** During the gravity pass, we iterate `vx[]` and `vy[]` but
don't touch `color[]` or `size[]`. With SoA, the gravity pass loads only 8
bytes per particle into cache (two floats). With AoS, each cache line fetch
would pull in the full 52-byte struct, wasting 85% of the bandwidth. At 100k
particles this is the difference between the working set fitting in L2 cache
(~800 KB for just vx+vy) versus spilling to L3 or main memory (~5.2 MB for
full structs).

### Property Ramp

A "ramp" describes how a particle's property (size, speed, opacity) changes
over its lifetime. This is the core mechanism for effects like "particles
shrink as they age" or "sparks start bright yellow and fade to dark red."

Our `PropertyRamp` is a lightweight alternative to Unity's `MinMaxCurve`. It
supports three modes, covering the vast majority of particle effects:

```cpp
// Defines how a property varies over a particle's normalized lifetime [0, 1].
// "Normalized lifetime" means: 0.0 = just spawned, 0.5 = halfway through
// its life, 1.0 = about to die.
struct PropertyRamp {
    enum Mode : uint8_t {
        kConstant,     // Single value, never changes. Example: size = 4.
        kRandomRange,  // Random value between min and max, picked once at
                       // spawn time and held constant for that particle's
                       // entire life. Example: size = random(2, 6).
        kRamp,         // Linear interpolation through N evenly-spaced stops.
                       // The particle walks through these values over its
                       // lifetime. Example: size starts at 4, shrinks to 2
                       // at the midpoint, reaches 0 at death.
    };

    Mode mode;
    uint8_t num_stops;           // 1 for constant, 2+ for ramp.
    float stops[8];              // Values at evenly spaced lifetime fractions.
                                 // stops[0] = value at birth (age = 0).
                                 // stops[num_stops-1] = value at death (age = lifetime).
    float min, max;              // Used only for kRandomRange mode.
};
```

The 8-stop limit matches Love2D's proven design and is enough for virtually
any effect curve (most use 2-4 stops). Stops are evenly spaced in time -- no
need for explicit time keys, which simplifies both the data structure and the
Lua API.

**How sampling works:** Given a particle's normalized age `t` (a float from
0.0 to 1.0), we compute which two stops it falls between and linearly
interpolate:

```
t = age / lifetime                     // e.g., 0.4 (40% through life)
index = t * (num_stops - 1)            // e.g., 0.4 * 2 = 0.8 (for 3 stops)
low = floor(index)                     // 0
high = ceil(index)                     // 1
frac = index - low                     // 0.8
result = stops[low] * (1 - frac) + stops[high] * frac   // lerp
```

This is a few multiplies and an add -- no branches, no heap allocation, no
function pointers.

**Color ramp** is the same idea but with 4 channels (red, green, blue, alpha).
Stored as a separate `ColorRamp` with `Color stops[8]`. Each stop is a full
RGBA color, and we interpolate all four channels independently. This is how
you get "bright yellow -> orange -> dark red -> transparent" fade-outs.

### Emitter Definition

The emitter definition is the declarative configuration -- a pure data struct
that describes *what kind* of particles to produce: how fast, how big, what
color, what forces apply. It has no behavior (no methods, no update loop) --
it's just a bag of numbers. This makes it trivially serializable (can be
written to/read from a file) and hot-reloadable (swap the struct pointer and
new particles use the new config).

```cpp
struct EmitterDef {
    // Spawning.
    // --- Spawning ---

    float emission_rate;           // Particles per second (0 = manual burst only).
    uint32_t max_particles;        // Pool capacity (pre-allocated, never grows).

    // --- Initial particle state (randomized per spawn) ---

    PropertyRamp initial_speed;    // Speed magnitude in pixels/sec. Combined
                                   // with direction to get a velocity vector.
    PropertyRamp initial_size;     // Visual half-width in pixels.
    PropertyRamp initial_spin;     // Rotation speed in radians/sec.
    float lifetime_min, lifetime_max; // Each particle gets a random lifetime
                                   // in this range (seconds).
    float direction;               // Base emission angle in radians.
                                   // 0 = rightward, -pi/2 = upward.
    float spread;                  // Half-angle of the emission cone in radians.
                                   // 0 = all particles go exactly in `direction`.
                                   // pi = particles go in all directions (full circle).
                                   // pi/6 = 30-degree cone.

    // --- Emission shape ---
    // Where new particles appear relative to the emitter position.

    enum Shape : uint8_t {
        kPoint,   // All particles spawn at the emitter's exact position.
        kCircle,  // Spawn at random points within a circle.
        kRect,    // Spawn at random points within a rectangle.
    };
    Shape shape;
    float shape_width, shape_height; // For kCircle: width = radius (height unused).
                                     // For kRect: half-width and half-height.

    // --- Over-lifetime modulation ---
    // These ramps control how properties change as the particle ages.

    PropertyRamp size_over_life;   // Multiplier on the particle's current size.
    PropertyRamp speed_over_life;  // Multiplier on the particle's current speed.
    PropertyRamp spin_over_life;   // Multiplier on the particle's spin rate.
    ColorRamp color_over_life;     // Absolute RGBA color at each life stage.

    // --- Forces ---

    float gravity_x, gravity_y;   // Constant acceleration in pixels/sec^2.
                                   // (0, 300) = downward pull. (0, -200) = upward.
    float damping;                 // Per-second velocity retention factor.
                                   // 1.0 = no drag (velocity unchanged).
                                   // 0.95 = loses 5% speed per second (gentle drag).
                                   // 0.5 = halves speed every second (heavy drag).
                                   // Applied as: velocity *= pow(damping, dt).

    // --- Rendering ---

    std::string_view texture;      // Asset name of the particle sprite (e.g.,
                                   // "spark.png"). Empty = solid white pixel.
    BlendMode blend_mode;          // How particles combine with the background.
                                   // BLEND_ADD (additive) for fire/sparks/glow.
                                   // BLEND_ALPHA (normal) for smoke/leaves/debris.
    bool relative_to_emitter;      // If true, particles move with the emitter
                                   // (like a comet tail). If false, particles
                                   // detach and drift freely (like sparks).
};
```

**Hot reload path:** `EmitterDef` is built from a Lua table in the asset DB.
When the file changes, the file watcher triggers an asset reload. The engine
reconstructs the `EmitterDef` from the new Lua table and swaps it into the
live emitter. Active particles continue with the old properties; only newly
spawned particles use the updated definition. This matches how our shader and
script hot-reload already works.

### Emitter (runtime state)

The `Emitter` is the live, per-frame object that owns a particle pool and
tracks the emitter's position in the game world. Multiple emitters can share
the same `EmitterDef` (e.g., every torch in the level uses the same fire
definition but has its own pool of particles at its own position).

```cpp
struct Emitter {
    EmitterDef* def;               // Points to the shared definition. Multiple
                                   // emitters can share one def. When hot-reload
                                   // swaps the def, all emitters see the change.
    ParticlePool pool;             // Owned particle storage (the SoA arrays).

    float emit_accumulator;        // Fractional particle counter. Explanation:
                                   // If emission_rate is 100 particles/sec and
                                   // dt is 0.016 sec (60 fps), we should spawn
                                   // 1.6 particles this frame. We spawn 1,
                                   // carry over the 0.6, and next frame we
                                   // spawn 2 (1.6 + 0.6 = 2.2, carry 0.2).
                                   // This avoids rounding errors that would
                                   // make emission rates inaccurate.
    float x, y;                    // Emitter world position (set by game script).
    float angle;                   // Emitter orientation (rotates the emission
                                   // direction).
    bool active;                   // Whether continuous emission is enabled.
                                   // When false, no new particles spawn (but
                                   // existing particles continue to live and die).

    Allocator* allocator;          // The allocator that owns the pool memory.
};
```

## Simulation

### Update Loop (CPU)

The update runs once per frame. `dt` is the time elapsed since the last frame
(typically ~0.016 seconds at 60fps). The loop runs over the SoA arrays in
three passes, each doing a simple linear scan. Splitting into passes (rather
than doing everything in one loop) improves cache behavior: each pass only
touches the arrays it needs.

```
Update(emitter, dt):
    pool = emitter.pool

    // Pass 1: Age and kill.
    // Increment every particle's age. If a particle has lived past its
    // lifetime, remove it using the swap-with-last trick (see diagram in
    // the ParticlePool section above).
    for i in [0, pool.count):
        pool.age[i] += dt
        if pool.age[i] >= pool.lifetime[i]:
            swap particle i with particle (pool.count - 1)
            pool.count--
            // IMPORTANT: Don't advance i. Index i now holds a different
            // particle (the one swapped in), so we must re-check it.

    // Pass 2: Apply forces and integrate velocity into position.
    // "Integrate" here means the basic physics step: velocity changes
    // position, acceleration changes velocity. This is Euler integration,
    // the simplest method (and sufficient for particles, which don't need
    // precise physics).
    frame_damping = pow(def.damping, dt)  // Precompute once (see note below).
    for i in [0, pool.count):
        // Apply gravity (constant acceleration in pixels/sec^2).
        pool.vx[i] += def.gravity_x * dt
        pool.vy[i] += def.gravity_y * dt
        // Apply damping (velocity drag). Multiplying by a value < 1.0
        // reduces the velocity, simulating air resistance.
        pool.vx[i] *= frame_damping
        pool.vy[i] *= frame_damping
        // Move the particle according to its velocity.
        pool.x[i] += pool.vx[i] * dt
        pool.y[i] += pool.vy[i] * dt
        // Rotate the particle sprite.
        pool.angle[i] += pool.spin[i] * dt

    // Pass 3: Evaluate over-lifetime ramps.
    // Compute each particle's normalized age (0.0 = just born, 1.0 = about
    // to die) and look up what its size/color/spin should be at this point
    // in its life.
    for i in [0, pool.count):
        t = pool.age[i] / pool.lifetime[i]   // Normalized age [0, 1].
        pool.size[i] = EvalRamp(def.size_over_life, t, pool.size[i])
        pool.color[i] = EvalColorRamp(def.color_over_life, t)
        pool.spin[i] = EvalRamp(def.spin_over_life, t, pool.spin[i])
```

**Damping note:** `pow(damping, dt)` computes a floating-point exponent, which
is expensive (~20-50 CPU cycles). Computing it once per frame outside the loop
and reusing the result for all particles is a trivial optimization that
eliminates thousands of `pow` calls.

**Kill pass note:** The backward-swap-and-decrement pattern means we must
iterate carefully (re-check the swapped-in particle at the same index). An
alternative is a two-pass approach: pass 1 marks dead particles (e.g., sets a
flag), pass 2 compacts the arrays by copying survivors to the front. The
compact pass can use `memcpy` on entire SoA arrays, which is very
cache-friendly because `memcpy` is typically implemented with SIMD
instructions that move 16-32 bytes at a time.

### Spawning

Spawning a particle means filling in slot `pool.count` (the first unused slot)
and incrementing the count. No memory is allocated -- we're just writing into
the pre-allocated arrays.

```
Spawn(emitter):
    if pool.count >= pool.max_particles: return   // Pool full, silently drop.

    i = pool.count++                              // Claim the next slot.

    // Position: start at the emitter position, offset by the emission shape.
    switch def.shape:
        kPoint:  ox, oy = 0, 0                   // Spawn exactly at emitter.
        kCircle: ox, oy = random point within a circle of radius def.shape_width
        kRect:   ox, oy = random point in [-w, +w] x [-h, +h]

    pool.x[i] = emitter.x + ox
    pool.y[i] = emitter.y + oy

    // Velocity: pick a random direction within the emission cone, then
    // pick a random speed. Convert (angle, speed) to (vx, vy) using
    // basic trig: vx = cos(angle) * speed, vy = sin(angle) * speed.
    angle = def.direction + random(-def.spread, def.spread)
    speed = EvalSpawnRamp(def.initial_speed)   // Picks a value depending
                                               // on mode (constant, random
                                               // range, or ramp start).
    pool.vx[i] = cos(angle) * speed
    pool.vy[i] = sin(angle) * speed

    // Each particle gets its own randomized lifetime, size, spin, etc.
    // This variation is what makes particle effects look organic rather
    // than mechanical.
    pool.lifetime[i] = random(def.lifetime_min, def.lifetime_max)
    pool.age[i] = 0
    pool.size[i] = EvalSpawnRamp(def.initial_size)
    pool.spin[i] = EvalSpawnRamp(def.initial_spin)
    pool.angle[i] = 0
    pool.color[i] = def.color_over_life.stops[0]  // Start at the first
                                                    // color in the ramp.
```

### Burst Emission

For one-shot effects (explosions, impacts), the Lua API exposes:

```lua
emitter:burst(count)        -- Spawn count particles immediately.
emitter:burst(count, x, y)  -- Spawn at a specific position.
```

This bypasses the emission rate accumulator and directly calls `Spawn` in a
loop. If `count` exceeds remaining pool capacity, it spawns as many as
possible without error.

## Rendering

### Instanced Draw

The key rendering technique is **instanced drawing**. Here's the idea:

Without instancing, to draw 10,000 particles you'd need to either:
- (a) Make 10,000 separate draw calls (catastrophically slow due to CPU-GPU
  round-trip overhead per call), or
- (b) Build a single vertex buffer with 40,000 vertices (4 corners * 10k
  quads) on the CPU every frame and draw it in one call. This works but
  requires the CPU to compute all 40k vertex positions, which is wasteful.

With instancing, the GPU knows how to draw one quad (4 vertices, defined once).
We upload a buffer of per-particle data (position, size, color, rotation), and
tell the GPU "draw that quad 10,000 times, using row N of this buffer for the
Nth instance." The GPU handles the replication in hardware, in parallel. The
CPU only uploads the compact per-instance data (~40 bytes per particle), not
the expanded vertices.

Each emitter renders with a single instanced draw call. The per-instance data
is a flat array of these structs, uploaded to GPU memory once per frame:

```cpp
struct ParticleVertex {
    float x, y;           // World position (center of the particle quad).
    float size;            // Half-width of the quad in pixels. A size of 4
                           // means the quad extends 4 pixels in each direction
                           // from its center (so 8x8 pixels total).
    float angle;           // Rotation of the quad in radians.
    Color color;           // RGBA color (4 bytes). Multiplied with the texture
                           // in the fragment shader to tint the particle.
    float tex_u, tex_v;    // Top-left corner of this sprite's region in the
                           // texture atlas (UV coordinates, range 0-1).
    float tex_w, tex_h;    // Width and height of the sprite region in UV space.
};
```

The vertex shader (a small GPU program that positions each vertex) takes a
"unit quad" -- four corners at (0,0), (1,0), (1,1), (0,1) -- and scales,
rotates, and positions it to form each particle:

```glsl
// These four corners are shared by ALL instances. They define the basic
// square shape. The GPU reads these once and reuses them 10,000 times.
layout(location = 0) in vec2 a_corner;     // (0,0), (1,0), (1,1), (0,1)

// These values change per instance (per particle). The GPU reads the next
// row from the instance buffer for each copy of the quad.
layout(location = 1) in vec2 a_position;   // Particle world position.
layout(location = 2) in float a_size;      // Particle size.
layout(location = 3) in float a_angle;     // Particle rotation.
layout(location = 4) in vec4 a_color;      // Particle RGBA color.
layout(location = 5) in vec4 a_tex_rect;   // UV rect: (u, v, width, height).

// Outputs passed to the fragment shader (which determines pixel color).
out vec2 v_uv;     // Texture coordinate for this vertex.
out vec4 v_color;  // Vertex color (interpolated across the quad's pixels).

void main() {
    // 1. Scale the unit corner by particle size to get the quad extent.
    //    (a_corner - 0.5) centers the quad: (0,0) becomes (-0.5,-0.5), etc.
    vec2 offset = (a_corner - 0.5) * a_size * 2.0;

    // 2. Rotate the offset around the particle center.
    //    This is the standard 2D rotation formula:
    //    x' = x*cos(θ) - y*sin(θ)
    //    y' = x*sin(θ) + y*cos(θ)
    float c = cos(a_angle), s = sin(a_angle);
    vec2 rotated = vec2(offset.x * c - offset.y * s,
                        offset.x * s + offset.y * c);

    // 3. Place the rotated quad at the particle's world position.
    //    u_projection is a matrix that converts world coordinates to
    //    screen coordinates (provided by the engine as a "uniform" --
    //    a value set once per draw call, not per vertex).
    gl_Position = u_projection * vec4(a_position + rotated, 0.0, 1.0);

    // 4. Compute the texture coordinate for this corner. Maps the unit
    //    corner (0-1) into the sprite's sub-region of the atlas.
    v_uv = a_tex_rect.xy + a_corner * a_tex_rect.zw;
    v_color = a_color;
}
```

**What happens after the vertex shader:** The GPU rasterizes each quad (fills
in the pixels between the four corners), then runs the fragment shader once per
pixel. The fragment shader reads the particle's texture at the interpolated UV
coordinate, multiplies by the vertex color (to apply tinting and alpha fade),
and writes the result to the screen using the active blend mode.

**Performance comparison with our current approach:** Our batch renderer
currently draws shapes by pushing `PushQuad` commands into a command buffer.
For 10k particles, that means 10k command entries, 40k vertices built on the
CPU, and a large chunk of the 16 MB command buffer consumed. With instanced
rendering: 1 draw call, 4 shared vertices, 10k instance records (~400 KB),
and zero command buffer overhead beyond the single `kRenderParticles` entry.

**Integration with BatchRenderer:** The particle draw is a new command type
`kRenderParticles` in the existing command queue. When the batch renderer
encounters this command during its render pass, it binds the particle instance
buffer and calls `glDrawElementsInstanced` (the OpenGL function that says
"draw this shape N times with per-instance data"). Because the command lives
in the same queue as all other draw commands, particles respect the current
draw order, blend mode, canvas (render target), scissor rect, and stencil
state. You can draw particles between other geometry and they'll appear at the
correct depth.

### Texture Atlas Support

Particles reference a sprite by asset name (e.g., `"spark"`). When the emitter
is created, the engine looks up the sprite in the asset database and retrieves
its UV rectangle -- the sub-region of the larger texture atlas where that
sprite lives. This UV rect is baked into every `ParticleVertex`'s `tex_rect`
field. Because all sprites in an atlas share one GPU texture, all particles
from one emitter can be drawn in the same instanced call without texture
switching.

For animated particles (a sprite sheet where each frame of animation is a
different cell), the `EmitterDef` can specify a frame count and FPS. During
the update loop, each particle's frame index advances based on its age, and
the UV rect is recomputed to point at the correct cell. This is a Phase 2
feature.

## Lua API

### Declarative Emitter Creation

```lua
local sparks = G.particles.new_emitter({
    texture = "spark",
    max_particles = 5000,
    emission_rate = 200,             -- particles/sec
    lifetime = {0.3, 0.8},          -- {min, max}
    speed = {100, 300},             -- {min, max}
    direction = -math.pi / 2,       -- upward
    spread = math.pi / 6,           -- 30 degree cone
    size = {4, 8},                   -- {min, max} at spawn
    size_over_life = {1.0, 0.5, 0}, -- ramp: full -> half -> zero
    color_over_life = {
        {1, 1, 0.8, 1},             -- bright yellow
        {1, 0.4, 0, 0.8},           -- orange
        {0.5, 0, 0, 0},             -- dark red, transparent
    },
    gravity = {0, 300},              -- {x, y}
    damping = 0.97,
    blend_mode = "add",
})
```

The Lua table maps directly to an `EmitterDef` struct on the C++ side. The
C++ binding code reads each key from the table and populates the corresponding
field. The type of the Lua value determines which `PropertyRamp` mode is used:

- **Single number** (`size = 4`): becomes `kConstant` mode. Every particle
  gets exactly this value.
- **Two-element table** (`speed = {100, 300}`): becomes `kRandomRange` mode.
  Each particle gets a random value between 100 and 300 at spawn time.
- **Three-or-more-element table** (`size_over_life = {1.0, 0.5, 0}`): becomes
  `kRamp` mode. The particle interpolates through these values over its
  lifetime (starts at 1.0, reaches 0.5 at the midpoint, fades to 0 at death).
- **Table of color tables** (`color_over_life = {{1,1,0.8,1}, {1,0.4,0,0.8},
  ...}`): becomes a `ColorRamp`. Each inner table is an RGBA color stop.

### Runtime Control

```lua
-- Continuous emission at a position.
sparks:set_position(x, y)
sparks:start()
sparks:stop()

-- One-shot burst.
sparks:burst(50)
sparks:burst(50, x, y)

-- Per-frame (called by engine automatically if registered, or manually).
sparks:update(dt)
sparks:draw()

-- Modify at runtime (takes effect for newly spawned particles).
sparks:set_emission_rate(500)
sparks:set_direction(angle)
sparks:set_gravity(0, 500)

-- Query.
local n = sparks:particle_count()
local alive = sparks:is_active()

-- Cleanup (returns memory to allocator).
sparks:destroy()
```

### Per-Particle Callback (Escape Hatch)

For effects that need custom per-particle logic (homing missiles, particles
that follow curves, etc.), an optional callback runs during the update pass:

```lua
local custom = G.particles.new_emitter({
    max_particles = 100,
    -- ... basic config ...
    update_callback = function(particles, dt)
        -- particles is a byte_buffer with SoA layout, directly mapped.
        -- Expensive, use sparingly. For most effects, ramps are enough.
        for i = 0, particles.count - 1 do
            local px, py = particles:get_position(i)
            local tx, ty = target.x, target.y
            local dx, dy = tx - px, ty - py
            local len = math.sqrt(dx*dx + dy*dy)
            if len > 0 then
                particles:set_velocity(i,
                    particles:get_vx(i) + dx/len * 200 * dt,
                    particles:get_vy(i) + dy/len * 200 * dt)
            end
        end
    end,
})
```

This is deliberately verbose to discourage overuse. The ramp system handles
95% of effects without touching Lua per-particle.

## Hot Reload

Emitter definitions are Lua tables in the asset DB. The hot reload path:

1. **File watcher** detects change to a `.lua` file containing particle defs.
2. **Asset DB** rebuilds, new Lua source enters the DB with updated checksum.
3. **Script reload** re-executes the Lua module. The module calls
   `G.particles.new_emitter()` again, which returns a new `EmitterDef`.
4. **Live emitters** referencing the old def get the new def pointer swapped
   in. Already-spawned particles keep their current state; new spawns use the
   updated definition.

This matches the existing hot-reload contract: the game script's `init()`
re-runs, recreating emitters with new configs. No special particle-specific
reload machinery is needed -- the standard script reload handles it.

For even faster iteration, a future enhancement could allow editing emitter
properties in a debug overlay (like the existing Tab debug panel) and
writing changes back to the Lua source file.

## Memory Budget

All particle memory is pre-allocated at emitter creation and freed at
destruction. No allocations happen during the update/spawn/draw loop. This
is the most important performance property of the design: `malloc`/`free` in
a per-frame loop causes fragmentation and unpredictable latency.

```
Per emitter (N = max_particles):
    SoA arrays:  N * 52 bytes     (10 float arrays + Color array, CPU-side)
    Instance buf: N * 40 bytes    (ParticleVertex, GPU-side)
    EmitterDef:  ~200 bytes       (shared across all emitters of the same type)

Example budgets:
    Casual game:   10 emitters * 1k particles  = ~1 MB total
    Action game:   50 emitters * 5k particles  = ~23 MB total
    Stress test:   5 emitters * 100k particles = ~46 MB total
```

**CPU-side memory** (the SoA arrays in `ParticlePool`) is allocated from the
system allocator (which wraps mimalloc, a fast general-purpose allocator) when
the emitter is created. This is a single allocation for all arrays (one
`Alloc` call for the total size, then pointer arithmetic to divide it into
sub-arrays). It's freed once when the emitter is destroyed.

**GPU-side memory** (the instance buffer) is a `GL_ARRAY_BUFFER` -- a block of
GPU memory allocated via OpenGL. It's created once at emitter creation with a
fixed size (`max_particles * sizeof(ParticleVertex)`). Each frame, we write the
current particles' data into this buffer using `glBufferSubData` (a function
that copies CPU data into an existing GPU buffer without reallocating it).
This is sometimes called "buffer streaming" and is the standard approach for
data that changes every frame.

**Frame-temporary scratch** (e.g., a temporary buffer used during spawn
position calculation) uses the arena allocator, which is reset at the end of
each frame. This costs zero overhead: the arena allocator just increments a
pointer to "allocate" and resets the pointer to "free everything."

## Implementation Plan

**Phase 1 -- CPU Particles with Instanced Draw (target: working prototype)**

1. Add `particles.h` / `particles.cc` with `ParticlePool`, `EmitterDef`,
   `Emitter`, `PropertyRamp`, `ColorRamp`. Pure C++ data structures and
   simulation logic, no OpenGL calls.
2. Implement `Update()`, `Spawn()`, `Burst()` on the CPU with SoA layout.
   This is the core simulation loop described above.
3. Add a particle-specific vertex shader (the GLSL code shown above) and
   the OpenGL setup to create and populate the instance buffer. This involves
   creating a Vertex Array Object (VAO) -- an OpenGL object that describes
   the layout of vertex data -- and configuring per-instance attribute
   divisors (telling OpenGL "advance this attribute once per instance, not
   once per vertex").
4. Add `kRenderParticles` command to `BatchRenderer`. When the renderer
   encounters this command during its flush, it switches to the particle
   shader, binds the particle VAO and instance buffer, and calls
   `glDrawElementsInstanced`.
5. Add `lua_particles.cc` with `new_emitter`, `set_position`, `start`,
   `stop`, `burst`, `update`, `draw`, `destroy`. Follow the existing Lua
   binding pattern (`LuaApiFunction` arrays, `Registry` for C++ object
   access).
6. Write tests in `tests/test.cc`: pool lifecycle (create, spawn, kill,
   destroy), swap-and-compact correctness, ramp evaluation at boundary
   values (t=0, t=1, t=0.5), edge cases (zero lifetime, pool at max
   capacity, zero emission rate, burst when pool is nearly full).

**Phase 2 -- Polish and Features**

7. Sprite sheet animation (per-particle frame index based on age, UV rect
   recalculated each frame to point at the correct animation cell).
8. Per-particle Lua callback escape hatch (the `update_callback` API).
9. `relative_to_emitter` mode (particles' positions are stored as offsets
   from the emitter rather than world coordinates, so they follow it).
10. Debug overlay: show emitter bounding boxes, live/max particle counts,
    and pool utilization in the Tab debug panel.
11. Emission shapes beyond point: circle and rectangle spawning.

**Phase 3 -- GPU Compute (future, if needed)**

12. Compute shader update pass. A compute shader is a GPU program that
    does general-purpose math (not rendering). We'd write the particle
    physics (gravity, damping, aging) as a compute shader that runs on the
    GPU using `glDispatchCompute` (OpenGL 4.3+). Each GPU thread updates
    one particle in parallel.
13. Particle state would live in an SSBO (Shader Storage Buffer Object) --
    a GPU memory buffer readable and writable by compute shaders. No more
    CPU-to-GPU upload each frame.
14. This eliminates the CPU-side SoA arrays entirely. The downside: you
    can no longer easily read particle state from game logic (Lua), and
    debugging becomes harder.
15. Only worth pursuing if CPU simulation becomes the bottleneck, which is
    unlikely below 100k particles on modern hardware.

## Open Questions

1. **Should emitters be automatically updated by the engine, or must the game  script call `update(dt)` and `draw()` explicitly?** Auto-update is
   convenient but hides control. Love2D requires explicit calls. Godot
   auto-updates. Recommendation: auto-update by default (registered emitters
   tick in the engine loop), with an opt-out `manual = true` flag for scripts
   that want full control.

	%% I would rather that we require an update(dt) and draw calls, it makes it clearer the the game is using the particle system. %%

3. **Sub-emitters?** Unity supports particles spawning child particles
   (e.g., a firework trail particle spawns sparks on death). This is powerful
   but complex. Recommendation: defer to Phase 3. Scripts can approximate it
   by listening for particle death events and spawning new emitters.

	%% Defer it for now %%

4. **Particle-world collision?** Godot and Unity support it. For a 2D engine,
   this would mean integrating with the collision system (spatial hash /
   Box2D). Recommendation: defer. Most 2D particle effects (fire, smoke,
   sparks, blood) don't need collision. If needed, the per-particle Lua
   callback can query the collision world manually.

	%% Deffer %%

5. **Sorting within an emitter?** Godot supports draw order by index,
   lifetime, or reverse lifetime. This matters for alpha-blended particles
   because alpha blending is order-dependent: drawing particle A on top of
   particle B gives a different result than B on top of A. (Additive blending
   is commutative -- order doesn't matter, which is one reason additive is
   preferred for particles.) Recommendation: support back-to-front sort as
   an option on the emitter. For additive blend (the common case for
   fire/sparks/glow), skip sorting entirely.

	%% Support back to front seems fine %%.