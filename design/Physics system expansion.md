---
status: partially-implemented
tags: [physics, lua-api]
---

# Physics System Expansion

This document analyzes the engine's current Box2D physics wrapper, compares it
with physics systems in Love2D, Defold, Solar2D, Cocos2d-x, and Godot,
identifies gaps, and proposes a phased expansion plan. It also proposes two
test games that exercise the new features.

## Motivation

The engine has two complementary systems: `G.physics` (Box2D rigid body
dynamics) and `G.collision` (standalone spatial queries and collision
detection). The collision system was recently expanded and is now feature-rich.
The physics system, however, remains a minimal proof-of-concept wrapper:

- Only dynamic and static bodies (no kinematic)
- Only boxes and circles (no polygons, edges, chains)
- Only one joint type (friction, auto-attached, hardcoded)
- All material properties hardcoded (density=2.0, friction=0.3)
- No collision filtering
- No per-body property access (velocity, damping, mass, gravity scale)
- No joints exposed to Lua
- No debug drawing
- No world configuration (gravity is implicit, iteration counts are fixed)

Games that need physics-driven gameplay (ragdolls, vehicles, bridges, chains,
catapults, pinball, Angry Birds-style destruction) cannot be built with the
current API. This expansion aims to expose enough of Box2D to support these
genres while keeping the API simpler than Love2D's notoriously verbose wrapper.

## Glossary

**World**: The Box2D simulation container. Holds all bodies, joints, and
contacts. Stepped forward each frame by `Update(dt)`. Our engine has exactly
one world, created at startup.

**Body**: A rigid body with position, angle, velocity, and mass. Box2D has
three body types:
- **Static**: Does not move. Infinite mass. Used for ground, walls, platforms.
- **Dynamic**: Fully simulated. Responds to forces, impulses, gravity, and
  collisions.
- **Kinematic**: Moves at a set velocity but is not affected by forces or
  collisions with dynamic bodies. Used for moving platforms, conveyor belts,
  and animated obstacles.

**Shape**: Geometric description attached to a body. Box2D supports circle,
polygon (convex, max 8 vertices), edge (line segment), and chain (connected
edges). A body can have multiple shapes (compound bodies).

**Fixture**: Box2D's binding of a shape to a body. Carries material properties
(density, friction, restitution) and collision filtering. Love2D exposes
fixtures directly; most other engines hide them behind a simpler API.

**Joint**: A constraint between two bodies. Box2D provides distance, revolute,
prismatic, pulley, gear, wheel, weld, friction, rope, and motor joints. Joints
are the primary mechanism for building complex physical structures (bridges,
vehicles, ragdolls, chains, catapults).

**Fixture (hidden)**: In our proposed API, we skip the fixture concept and
attach shapes with material properties directly to bodies, matching the
approach of Solar2D, Defold, and Cocos2d-x. The Body/Shape/Fixture trichotomy
is universally considered the worst part of Love2D's physics API.

**Sensor**: A shape that detects overlaps without generating physical response.
Equivalent to triggers in the collision system but integrated with the physics
world. Used for hit boxes, pickup zones, or detection regions on
physics-simulated bodies.

**Collision filter**: Bitmask system controlling which bodies collide. Each
shape has a `category` (what I am) and a `mask` (what I collide with). Two
shapes collide if `(A.category & B.mask) != 0 AND (B.category & A.mask) != 0`.
The collision system already uses this exact model.

**Pixels-per-meter (PPM)**: Scale factor between pixel coordinates (game) and
meter coordinates (Box2D). Box2D is tuned for objects 0.1-10 meters.
Configurable per-world in the proposed API (currently hardcoded to 60).

## Current system

```
src/physics.h        86 lines    Box2D wrapper class
src/physics.cc       221 lines   Implementation
src/lua_physics.cc   207 lines   Lua bindings (11 functions)
```

### Lua API today

| Function | Description |
|---|---|
| `physics.add_box(tx, ty, bx, by, angle, cb)` | Create dynamic box |
| `physics.add_circle(x, y, radius, cb)` | Create dynamic circle |
| `physics.destroy_handle(h)` | Destroy body |
| `physics.create_ground()` | Create static boundary walls |
| `physics.on_begin_contact(fn)` | Global begin-contact callback |
| `physics.position(h)` | Get position (x, y) |
| `physics.angle(h)` | Get angle (radians) |
| `physics.rotate(h, angle)` | Add rotation delta |
| `physics.apply_linear_impulse(h, x, y)` | Instant impulse |
| `physics.apply_force(h, x, y)` | Continuous force |
| `physics.apply_torque(h, torque)` | Angular force |

### Hardcoded values

| Property | Value | Should be configurable |
|---|---|---|
| Density | 2.0 | Yes, per-shape |
| Friction | 0.3 | Yes, per-shape |
| Restitution | 0.0 (bodies), 0.4 (ground) | Yes, per-shape |
| Gravity | 10.0 (via friction joint) | Yes, world-level |
| Velocity iterations | 6 | Yes, world-level |
| Position iterations | 2 | Yes, world-level |
| Pixels per meter | 60 | Yes, world-level |
| Friction joint max force | mass * gravity | Derived, but gravity should be settable |

### Implementation notes

Every dynamic body gets an auto-attached `b2FrictionJoint` to the ground body.
This provides top-down friction (prevents infinite sliding). The friction
joint's max force and torque are derived from the body's mass and inertia
multiplied by a hardcoded gravity constant of 10.0. This is a reasonable
default for top-down games but wrong for side-view games and should be
optional.

## Engine survey

### Love2D (love.physics)

The most relevant comparison: Lua scripting, Box2D backend, same target
audience.

**Abstraction level**: Extremely thin. Exposes the full Body/Shape/Fixture/Joint
model directly. This is the most powerful approach but also the most verbose.
Creating a simple bouncing ball requires: `newWorld` -> `newBody` -> `newCircleShape`
-> `newFixture` -> set density, set restitution. Five objects for one circle.

**Body types**: Static, dynamic, kinematic. All three exposed.

**Shapes**: Circle, rectangle, polygon (convex, max 8 vertices), edge, chain.
All Box2D shapes exposed.

**Joints**: All 11 Box2D joint types: distance, friction, gear, motor, mouse,
prismatic, pulley, revolute, rope, weld, wheel. Each is a separate constructor
with full parameter control.

**Material properties**: Set on Fixtures (density, friction, restitution).
Configurable per-fixture, not per-body.

**Collision filtering**: Box2D native 16-bit category/mask/group system on
fixtures. Also supports a custom Lua filter function via
`World:setContactFilter()`.

**Callbacks**: Four callbacks on the World: `beginContact`, `endContact`,
`preSolve`, `postSolve`. The world is locked during callbacks (cannot
create/destroy bodies). This is a major pain point.

**Body API (51 methods)**: Position, angle, velocity (linear + angular),
damping, mass/inertia, coordinate transforms (local/world), gravity scale,
bullet mode (CCD), fixed rotation, sleeping, active/inactive.

**World API (21 methods)**: Gravity, sleeping, `queryBoundingBox`,
`rayCast`, `translateOrigin`, iteration counts.

**What users complain about**:
- Fixture/Shape/Body split is too verbose for common cases (GitHub #1130)
- Category/mask bits are unintuitive; users want named collision groups
- World-locked callbacks prevent natural create/destroy patterns
- No built-in debug drawing
- No simplified "add physics to this object" shorthand

**Key takeaway**: Full power, poor ergonomics. Our API should expose the same
features with fewer objects.

### Defold

**Abstraction level**: High. Physics objects are editor components, not
programmatic. Lua API is limited to property queries and joint creation.

**Body types**: Static, dynamic, kinematic. Set in editor as collision object
type.

**Shapes**: Box, sphere, capsule. Set in editor. A newer `b2d` Lua extension
(added 1.8.0) exposes more Box2D features directly.

**Joints (6)**: Fixed (rope), hinge (revolute), weld, spring (distance),
slider (prismatic), wheel. Created via `physics.create_joint()` with Lua
tables.

**Collision filtering**: Named groups. Each collision object belongs to a group
and lists groups it collides with. More intuitive than bitmasks but less
flexible.

**Callbacks**: Message-passing (`collision_response`, `contact_point_response`,
`trigger_response` messages to game objects).

**Key takeaway**: Named collision groups are more user-friendly than bitmasks.
The limited joint selection (6 of 11) covers 95% of use cases.

### Solar2D (formerly Corona SDK)

**Abstraction level**: High. Any display object becomes physical with
`physics.addBody(object, type, {properties})`. No separate Body/Shape/Fixture
management.

**Body types**: Static, dynamic, kinematic.

**Material properties**: Passed as a table to `addBody`:
`{density=1, friction=0.3, bounce=0.5, radius=30}`. Multiple shapes per body
via multiple property tables.

**Joints (10)**: pivot, distance, piston, friction, weld, wheel, pulley,
touch, rope, gear. Created via `physics.newJoint("type", ...)`.

**Collision filtering**: `categoryBits`, `maskBits`, `groupIndex` on each
shape. Standard Box2D model with positive/negative group override.

**Callbacks**: Three event types (`collision`, `preCollision`,
`postCollision`) with local or global listeners. Cannot create/destroy in
handlers (world lock).

**Debug drawing**: Built-in `physics.setDrawMode("hybrid"|"debug"|"normal")`.

**Key takeaway**: The "make any object physical" approach is the gold standard
for API ergonomics. Material properties as a plain table is clean. The debug
draw toggle is a must-have.

### Cocos2d-x / Cocos Creator

**Abstraction level**: Medium-high. Unified `PhysicsBody`/`PhysicsShape`/
`PhysicsJoint` API that abstracts over both Chipmunk and Box2D backends.

**Body types**: Static, dynamic, kinematic. Bodies attach to scene nodes.

**Joints (7)**: Distance, fixed, hinge, relative, slider, spring, wheel.

**Collision filtering**: `CategoryBitmask`, `ContactTestBitmask`,
`CollisionBitmask` (32-bit) plus a group override.

**Key takeaway**: The dual-backend abstraction validates that a simplified API
can work over Box2D. Component attachment to scene graph nodes is natural.

### Godot

**Abstraction level**: Very high. Custom physics engine (not Box2D), but the
API design is instructive.

**Body types**: Specialized node types rather than mode flags:
- `StaticBody2D`: Immovable.
- `RigidBody2D`: Fully simulated.
- `CharacterBody2D`: Code-controlled with `move_and_slide()`.
- `AnimatableBody2D`: Static body that can be animated.
- `Area2D`: Detection/trigger zones.

**Joints (3)**: PinJoint2D, DampedSpringJoint2D, GrooveJoint2D. Notably
limited -- a common complaint.

**Collision filtering**: 32 named layers. Each body has `collision_layer` (what
I am) and `collision_mask` (what I detect). Layers are named in project
settings.

**Key takeaway**: `CharacterBody2D` with `move_and_slide()` is a killer
feature -- but we already have this in `G.collision`. Named collision layers
are far more usable than raw bitmasks but require project-level configuration.

### high_impact

The most architecturally similar engine: C, minimal, 2D action games, same
author as the QOA codec we use. high_impact is Dominic Szablewski's C port of
his original ImpactJS engine (~4000 lines total). It deliberately avoids
Box2D.

**Abstraction level**: No physics engine at all. Custom AABB trace + entity
resolution system covers platformers, shooters, and top-down games in a
fraction of Box2D's complexity. The original ImpactJS had an optional Box2D
plugin, but it was never ported to C.

**Body types**: Not body types per se, but two orthogonal enum settings on
entities:
- *Movement physics*: `NONE` (stationary), `MOVE` (velocity integration
  only), `WORLD` (movement + tilemap collision).
- *Collision physics*: `LITE` (only this entity pushed), `PASSIVE` (responds
  to ACTIVE/FIXED), `ACTIVE` (two-way, mass-weighted), `FIXED` (immovable).

This is more expressive than static/dynamic/kinematic in some ways. A
projectile is `WORLD + LITE` (collides with tiles, pushes nothing). A moving
platform is `WORLD + FIXED` (collides with tiles, pushes everything). A player
is `WORLD + ACTIVE` (collides with tiles, mass-weighted entity resolution).

**Shapes**: Only AABBs. Every entity has `pos` (top-left) and `size` (width,
height). No circles, polygons, or compound shapes.

**Joints**: None. No constraint system of any kind.

**Collision detection**: Two-phase:
1. *Entity vs tilemap*: AABB sweep trace against collision map tiles. Handles
   full solid tiles and slope tiles (defined at compile time with precomputed
   normals). When a trace hits a surface, remaining velocity slides along the
   tangent via a second trace. Bounce uses velocity reflection scaled by
   restitution.
2. *Entity vs entity*: Sweep-and-prune broad phase (insertion sort on a
   configurable axis, exploiting temporal coherence). Resolution separates on
   the minimum-overlap axis with mass-weighted push distribution.

**Collision groups**: Entities belong to groups (`PLAYER`, `NPC`, `ENEMY`,
`ITEM`, `PROJECTILE`, `PICKUP`, `BREAKABLE`) and define `check_against`
bitmask. Group membership triggers `touch()` callbacks. Physics resolution is
separate from group checks.

**Entity properties**: `vel`, `accel`, `friction` (per-axis damping),
`gravity` (multiplier on global gravity), `mass`, `restitution`,
`max_ground_normal` (slope threshold for `on_ground` flag).

**Integration**: Half-step velocity integration (average of old and new
velocity), giving better accuracy than Euler at zero cost. Global gravity
applied per-frame, friction as exponential decay.

**Callbacks**: Two per entity: `touch(self, other)` fires on AABB overlap
when groups match, `collide(self, normal, trace)` fires on tilemap or entity
collision.

**Key design decisions**:
- Compile-time slope definitions with Newton's method `sqrt` in macros.
- Insertion sort broad phase exploits frame-to-frame coherence (O(n) average).
- Fixed entity pool (1024 max), no dynamic allocation during gameplay.
- Configurable sweep axis (`ENTITY_SWEEP_AXIS x` or `y`).

**Key takeaway**: high_impact proves that for the majority of 2D action games,
Box2D is overkill. The trace-based AABB system handles platformers and
shooters with far less complexity. Our `G.collision` system already fills this
role. The physics expansion should focus on the genres that *require* rigid
body simulation: destruction games, vehicle games, ragdolls, Rube Goldberg
machines, pinball. high_impact's movement/collision physics split (two
orthogonal enums) is an elegant alternative to Box2D's body types, but not
applicable here since we already have Box2D.

## Comparison matrix

| Feature | This engine | Love2D | Defold | Solar2D | Godot | high_impact |
|---|---|---|---|---|---|---|
| **Body types** | Dynamic, static | All 3 | All 3 | All 3 | 4 specialized | 2 orthogonal enums |
| **Shapes** | Box, circle | All 5 | Box, sphere, capsule | All via Box2D | 8 types | AABB only |
| **Joints** | None exposed | 11 | 6 | 10 | 3 | None |
| **Material props** | Hardcoded | Per-fixture | Per-component | Per-shape table | Per-body | Per-entity fields |
| **Collision filter** | None | Category/mask/group | Named groups | Category/mask/group | 32 named layers | Group bitmask |
| **Callbacks** | Begin only | Begin/end/pre/post | Messages | 3 types | Signals | touch + collide |
| **Sensors** | No | Yes (fixture) | Yes (trigger) | Yes (isSensor) | Area2D | Via groups |
| **Debug draw** | No | No | Editor only | Yes, built-in | Yes, built-in | No |
| **World queries** | No | AABB query, raycast | No | No | Full suite | Trace (sweep) |
| **Gravity control** | Hardcoded | World + per-body scale | World | World | World + per-body + areas | World + per-entity |
| **Velocity access** | No | Full | Limited | Limited | Full | Direct field |
| **Damping** | Via friction joint | Per-body | Per-component | Per-body | Per-body | Per-axis friction |
| **CCD (bullet)** | No | Per-body toggle | No | World toggle | Per-body | No |
| **Fixed rotation** | No | Per-body toggle | Per-component | No | Per-body | N/A (no rotation) |
| **Sleeping** | No control | World + per-body | No control | No control | Per-body | No |

### Cross-cutting themes

1. **The Body/Shape/Fixture trichotomy is universally disliked.** Every engine
   that wraps Box2D either hides it (Solar2D, Defold, Cocos) or has users
   requesting it be hidden (Love2D GitHub #1130). The common pattern is a
   single "body" object with shapes attached directly.

2. **String/named collision groups beat raw bitmasks.** Godot's 32 named
   layers and Defold's named groups are preferred over raw `categoryBits` /
   `maskBits`. Love2D's community explicitly requested this. However, our
   engine already uses category/mask in `G.collision` and changing models
   would create inconsistency, so we keep bitmasks.

3. **Most 2D games don't need Box2D at all.** high_impact proves that AABB
   traces + entity resolution cover platformers, shooters, and top-down games
   with far less complexity. Our `G.collision` system already serves this
   role. The physics expansion should focus narrowly on genres that *require*
   rigid body simulation: destruction, vehicles, ragdolls, Rube Goldberg,
   pinball.

4. **World-locked callbacks are a universal pain point.** Every Box2D wrapper
   has to deal with the fact that you cannot modify the world during collision
   callbacks. Solar2D and Cocos2d-x solve this with deferred action queues.
   We should too.

5. **Debug drawing is table stakes.** Solar2D's `setDrawMode("debug")` and
   Godot's built-in debug overlay are considered essential. Engines without
   them get complaints. Love2D and high_impact both lack it and users notice.

6. **Per-entity physics properties matter.** high_impact's per-entity
   `gravity`, `friction`, `mass`, and `restitution` fields are simple and
   powerful. Love2D's per-body `gravityScale`, `linearDamping`, etc. serve
   the same purpose. Our current system hardcodes everything -- this is the
   lowest-effort, highest-impact improvement.

7. **Joints are the killer feature of Box2D.** Without joints, there is no
   reason to choose Box2D over a simpler system like high_impact's or our own
   `G.collision`. Joints are what enable destruction games, vehicles, bridges,
   ragdolls, and mechanical puzzles. They should be the centerpiece of the
   expansion.

## Design principles

Based on the survey, the following principles guide the expansion:

1. **Hide fixtures, expose shapes on bodies.** The Body/Shape/Fixture split is
   universally disliked. Our API attaches shapes directly to bodies with
   material properties as parameters. Internally we still create fixtures.

2. **Tables for configuration, handles for identity.** Material properties,
   filter settings, and joint parameters are passed as Lua tables with sensible
   defaults. Bodies and joints are opaque handles. This matches the engine's
   existing collision API and Lua conventions.

3. **Same collision filter model as G.collision.** The collision system already
   uses `category`/`mask` (16-bit). The physics system should use the same
   model and the same vocabulary. Games that use both systems don't need to
   learn two filtering schemes.

4. **Deferred destruction pattern for world-locked callbacks.** Bodies and
   joints cannot be created or destroyed during Box2D callbacks. Instead of
   exposing this footgun, we queue destruction requests and process them after
   the step completes. This is how Solar2D and Cocos2d-x solve the problem.

5. **Debug drawing is mandatory.** Every engine that ships without it gets
   complaints. Wire-frame rendering of all shapes, joints, AABBs, and contact
   points. Toggle via `physics.set_draw_mode()`.

6. **Keep the simple path simple.** Creating a single dynamic box should
   remain a one-liner. The full API is opt-in complexity.

## Proposed API

### Phase 1: Body and shape expansion

Priority: Expose the core Box2D features that are trivially missing.

#### World configuration

```lua
-- Configure the physics world (call before creating bodies)
physics.set_gravity(0, 980)          -- pixels/sec^2, converted internally
physics.set_pixels_per_meter(64)     -- default: 60
physics.set_iterations(8, 3)         -- velocity, position iterations

local gx, gy = physics.get_gravity()
```

#### Body creation

```lua
-- Dynamic body (default)
local body = physics.new_body("dynamic", {
  x = 100, y = 200,
  angle = 0,                   -- radians, default 0
  linear_damping = 0.1,        -- default 0
  angular_damping = 0.1,       -- default 0
  gravity_scale = 1.0,         -- default 1.0 (0 = no gravity)
  fixed_rotation = false,      -- default false
  bullet = false,              -- default false (CCD)
})

-- Static body
local wall = physics.new_body("static", { x = 0, y = 500 })

-- Kinematic body (moves at set velocity, not affected by forces)
local platform = physics.new_body("kinematic", {
  x = 200, y = 300,
  linear_velocity = { 50, 0 }, -- pixels/sec
})
```

#### Shape attachment

Bodies start with no shape. Attach one or more shapes to create compound
bodies. Each shape carries its own material properties and filter.

```lua
-- Attach shapes to bodies. Returns a shape handle for later removal.
local s1 = body:add_circle({
  radius = 20,
  density = 1.0,        -- default 1.0
  friction = 0.3,       -- default 0.3
  restitution = 0.5,    -- default 0.0
  sensor = false,       -- default false
  category = 0x0001,    -- default 0x0001
  mask = 0xFFFF,        -- default 0xFFFF
})

local s2 = body:add_box({
  width = 40, height = 60,
  -- offset from body center:
  offset_x = 0, offset_y = 0,  -- default 0, 0
  angle = 0,                    -- local rotation, default 0
  density = 2.0,
})

local s3 = body:add_polygon({
  -- convex polygon vertices (local coordinates, max 8)
  vertices = { -20, -10, 20, -10, 15, 10, -15, 10 },
  density = 1.0,
})

local s4 = body:add_edge({
  x1 = 0, y1 = 0, x2 = 100, y2 = 0,
})

local s5 = body:add_chain({
  vertices = { 0, 0, 50, -20, 100, 0, 150, -20, 200, 0 },
  loop = false,   -- default false
})

-- Remove a shape from its body
body:remove_shape(s1)
```

#### Convenience constructors

For the common case of one shape per body:

```lua
-- These create a body + shape in one call, returning both handles
local body, shape = physics.new_box("dynamic", {
  x = 100, y = 200,
  width = 40, height = 60,
  density = 1.0,
  friction = 0.3,
  restitution = 0.5,
})

local body, shape = physics.new_circle("dynamic", {
  x = 100, y = 200,
  radius = 20,
  density = 1.0,
})
```

#### Body property access

```lua
-- Position and angle
local x, y = body:get_position()
body:set_position(100, 200)
local a = body:get_angle()
body:set_angle(math.pi / 4)

-- Velocity
local vx, vy = body:get_linear_velocity()
body:set_linear_velocity(100, 0)
local av = body:get_angular_velocity()
body:set_angular_velocity(3.14)

-- Mass (computed from shapes)
local mass = body:get_mass()
local inertia = body:get_inertia()

-- Per-body properties
body:set_gravity_scale(0.5)
body:set_linear_damping(0.2)
body:set_angular_damping(0.1)
body:set_fixed_rotation(true)
body:set_bullet(true)         -- enable CCD
body:set_active(false)        -- disable from simulation
body:set_awake(true)          -- wake from sleeping

-- Type
local t = body:get_type()     -- "dynamic", "static", "kinematic"
body:set_type("kinematic")    -- can change at runtime

-- Coordinate transforms
local lx, ly = body:get_local_point(world_x, world_y)
local wx, wy = body:get_world_point(local_x, local_y)
local lvx, lvy = body:get_local_vector(world_vx, world_vy)
local wvx, wvy = body:get_world_vector(local_vx, local_vy)
```

#### Forces and impulses

```lua
-- At center of mass (existing)
body:apply_force(fx, fy)
body:apply_linear_impulse(ix, iy)
body:apply_torque(torque)
body:apply_angular_impulse(impulse)

-- At a world point (new -- critical for realistic physics)
body:apply_force_at(fx, fy, px, py)
body:apply_linear_impulse_at(ix, iy, px, py)
```

#### Destruction

```lua
body:destroy()   -- queued, safe to call in callbacks
```

### Phase 2: Joints

Priority: The primary missing feature. Joints enable the most interesting
physics gameplay.

#### Joint creation

All joints are created via `physics.new_joint(type, body_a, body_b, params)`.
The table-based params approach avoids Love2D's problem of 10+ positional
arguments.

```lua
-- Revolute joint (hinge, pivot)
-- Use cases: ragdoll limbs, doors, flippers, wheels
local j = physics.new_joint("revolute", body_a, body_b, {
  anchor_x = 100, anchor_y = 200, -- world-space anchor point
  enable_limit = false,
  lower_angle = -math.pi / 4,     -- optional angle limits
  upper_angle = math.pi / 4,
  enable_motor = false,
  motor_speed = 0,                 -- rad/sec
  max_motor_torque = 100,
  collide_connected = false,       -- default false
})

-- Distance joint (spring)
-- Use cases: soft bridges, bungee cords, suspension
local j = physics.new_joint("distance", body_a, body_b, {
  anchor_a_x = 50, anchor_a_y = 100,  -- world-space anchor on body A
  anchor_b_x = 150, anchor_b_y = 100, -- world-space anchor on body B
  length = 100,        -- rest length (default: current distance)
  frequency = 4.0,     -- Hz, spring stiffness (0 = rigid)
  damping_ratio = 0.5, -- 0..1
})

-- Prismatic joint (slider, piston)
-- Use cases: elevators, pistons, sliding doors
local j = physics.new_joint("prismatic", body_a, body_b, {
  anchor_x = 100, anchor_y = 200,
  axis_x = 1, axis_y = 0,     -- slide axis (world-space direction)
  enable_limit = true,
  lower_translation = -50,     -- pixels
  upper_translation = 50,
  enable_motor = true,
  motor_speed = 100,           -- pixels/sec
  max_motor_force = 500,
})

-- Weld joint (rigid connection)
-- Use cases: breakable structures, sticky bombs
local j = physics.new_joint("weld", body_a, body_b, {
  anchor_x = 100, anchor_y = 200,
  frequency = 0,       -- Hz (0 = perfectly rigid)
  damping_ratio = 0,
})

-- Wheel joint (vehicle suspension)
-- Use cases: cars, bikes, carts
local j = physics.new_joint("wheel", body_a, body_b, {
  anchor_x = 100, anchor_y = 200,
  axis_x = 0, axis_y = 1,     -- suspension axis
  enable_motor = true,
  motor_speed = 360,           -- rad/sec
  max_motor_torque = 200,
  frequency = 4.0,             -- suspension spring Hz
  damping_ratio = 0.7,
})

-- Rope joint (max distance constraint)
-- Use cases: chains, ropes, tethers
local j = physics.new_joint("rope", body_a, body_b, {
  anchor_a_x = 50, anchor_a_y = 100,
  anchor_b_x = 150, anchor_b_y = 100,
  max_length = 200,
})

-- Pulley joint
-- Use cases: elevators, counterweights, drawbridges
local j = physics.new_joint("pulley", body_a, body_b, {
  ground_a_x = 50, ground_a_y = 0,    -- pulley anchor A
  ground_b_x = 200, ground_b_y = 0,   -- pulley anchor B
  anchor_a_x = 50, anchor_a_y = 100,
  anchor_b_x = 200, anchor_b_y = 100,
  ratio = 1.0,                         -- mechanical advantage
})

-- Mouse joint (drag to target)
-- Use cases: mouse/touch interaction, object dragging
local j = physics.new_joint("mouse", body_a, body_b, {
  target_x = 200, target_y = 300,     -- target point
  max_force = 1000,
  frequency = 5.0,
  damping_ratio = 0.7,
})

-- Gear joint (couples two revolute or prismatic joints)
-- Use cases: gears, rack-and-pinion, coupled mechanisms
local j = physics.new_joint("gear", body_a, body_b, {
  joint_a = revolute_joint,
  joint_b = prismatic_joint,
  ratio = 2.0,    -- gear ratio
})

-- Friction joint (top-down friction)
-- Use cases: top-down car friction, drag
local j = physics.new_joint("friction", body_a, body_b, {
  anchor_x = 100, anchor_y = 200,
  max_force = 100,
  max_torque = 50,
})

-- Motor joint (relative position/angle target)
-- Use cases: character aim, turrets, smooth repositioning
local j = physics.new_joint("motor", body_a, body_b, {
  linear_offset_x = 0, linear_offset_y = 0,
  angular_offset = 0,
  max_force = 100,
  max_torque = 50,
  correction_factor = 0.3,
})
```

#### Joint property access

```lua
-- Common to all joints
local ba, bb = j:get_bodies()
local ax, ay, bx, by = j:get_anchors()
j:destroy()  -- queued, safe in callbacks

-- Revolute joint
j:set_motor_speed(speed)
j:set_max_motor_torque(torque)
j:enable_motor(true)
j:set_limits(lower, upper)
j:enable_limit(true)
local angle = j:get_joint_angle()
local speed = j:get_joint_speed()

-- Distance joint
j:set_length(length)
j:set_frequency(hz)
j:set_damping_ratio(ratio)

-- Prismatic joint
j:set_motor_speed(speed)
j:set_max_motor_force(force)
j:enable_motor(true)
j:set_limits(lower, upper)
j:enable_limit(true)
local translation = j:get_joint_translation()

-- Mouse joint
j:set_target(x, y)
j:set_max_force(force)

-- Wheel joint
j:set_motor_speed(speed)
j:set_max_motor_torque(torque)
j:enable_motor(true)
j:set_spring_frequency(hz)
j:set_spring_damping_ratio(ratio)

-- Weld joint
j:set_frequency(hz)
j:set_damping_ratio(ratio)
```

#### Joint priority

Based on the survey and common game genres, joints should be implemented in
this order:

| Priority | Joint | Rationale |
|---|---|---|
| 1 | Revolute | Most common. Ragdolls, doors, flippers, wheels, pendulums. |
| 2 | Distance | Springs, bridges, bungee. Easy to implement, high payoff. |
| 3 | Weld | Breakable structures. Simple (rigid connection). |
| 4 | Prismatic | Elevators, pistons, sliding doors. |
| 5 | Mouse | Essential for mouse/touch physics interaction. |
| 6 | Wheel | Vehicles. Combines revolute + prismatic. |
| 7 | Rope | Chains, tethers. Simple max-length constraint. |
| 8 | Pulley | Elevators, counterweights. Niche but unique. |
| 9 | Friction | Already used internally. Expose to Lua for top-down games. |
| 10 | Motor | Turrets, smooth repositioning. Advanced. |
| 11 | Gear | Gear trains. Requires existing revolute/prismatic joints. Very niche. |

### Phase 3: Callbacks and queries

#### Collision callbacks

```lua
-- Four callback types (matching Box2D's contact listener)
physics.on_begin_contact(function(body_a, body_b, contact)
  -- Two bodies started touching
  -- contact: { normal_x, normal_y, points = {{x, y}, ...} }
end)

physics.on_end_contact(function(body_a, body_b)
  -- Two bodies stopped touching
end)

physics.on_pre_solve(function(body_a, body_b, contact)
  -- Called before physics response. Return false to disable contact.
  if body_a == player and body_b == one_way_platform then
    local vx, vy = player:get_linear_velocity()
    if vy < 0 then return false end  -- falling through
  end
  return true
end)

physics.on_post_solve(function(body_a, body_b, contact)
  -- Called after physics response.
  -- contact: { normal_impulse, tangent_impulse }
  if contact.normal_impulse > 500 then
    -- Big impact! Play sound, spawn particles, break joint
  end
end)
```

The `pre_solve` callback is particularly valuable: it enables one-way
platforms, selective collision disabling, and impact-dependent response. Love2D
exposes this but cannot modify the contact inside the callback. Our API lets
the callback return false to disable the contact for that frame.

#### Deferred destruction

```lua
-- Safe to call inside any callback:
body:destroy()   -- queued until after physics step
joint:destroy()  -- queued until after physics step

-- Also safe: creating bodies and joints inside callbacks
-- (queued until after physics step)
```

Implementation: maintain a `DynArray<Action>` of deferred operations. Process
after `b2World::Step()` returns. This is strictly better than Love2D's "crash
if you create/destroy in callbacks" behavior.

#### World queries

```lua
-- Raycast (returns first hit)
local hit = physics.raycast(x1, y1, x2, y2, {
  mask = 0xFFFF,  -- optional filter
})
-- hit: { body, x, y, normal_x, normal_y, fraction } or nil

-- Raycast all (returns all hits sorted by distance)
local hits = physics.raycast_all(x1, y1, x2, y2, {
  mask = 0xFFFF,
})

-- AABB query (returns all bodies overlapping a rectangle)
local bodies = physics.query_rect(x1, y1, x2, y2, {
  mask = 0xFFFF,
})

-- Point query (returns all bodies containing a point)
local bodies = physics.query_point(x, y, {
  mask = 0xFFFF,
})
```

### Phase 4: Debug drawing and quality of life

#### Debug draw

```lua
physics.set_draw_mode("normal")   -- default, no debug rendering
physics.set_draw_mode("debug")    -- only wire-frame physics shapes
physics.set_draw_mode("hybrid")   -- game rendering + wire-frame overlay
```

Debug drawing renders:
- **Shapes**: Green wire-frame for dynamic, gray for static, blue for
  kinematic, yellow for sensors
- **Joints**: Lines between anchor points, with motor/limit indicators
- **Contact points**: Red dots at contact locations
- **AABBs**: Optional bounding box outlines
- **Center of mass**: Small crosshair on each body

Implementation: Implement `b2Draw` interface. Rendered as a separate overlay
pass after the game's `draw()` returns, so debug lines are never occluded by
game sprites. Uses the existing batch renderer (`G.renderer`) with line and
circle primitives (`draw_line`, `draw_circle_outline`, `draw_rect_outline`).

#### Top-down mode

The current auto-friction-joint behavior is useful for top-down games but
wrong for side-view games. A world-level mode controls the default:

```lua
-- Side-view (default): no auto-friction joints, gravity pulls downward
physics.set_mode("sideview")

-- Top-down: auto-attach friction joints to new dynamic bodies
physics.set_mode("topdown")
```

In top-down mode, each new dynamic body gets a `b2FrictionJoint` to the ground
(matching the current behavior). In side-view mode, use `linear_damping` and
`angular_damping` on individual bodies for drag effects:

```lua
local body = physics.new_body("dynamic", {
  x = 100, y = 200,
  linear_damping = 5.0,
  angular_damping = 5.0,
})
```

#### Edge/chain shape helpers for level geometry

```lua
-- Create static ground from a set of points (common pattern)
local ground = physics.new_ground({
  vertices = { 0, 500, 200, 450, 400, 480, 600, 500, 800, 500 },
  loop = false,
  friction = 0.6,
  restitution = 0.1,
})

-- Create static box boundary (replaces current create_ground)
physics.create_boundary(width, height, {
  friction = 0.3,
  restitution = 0.4,
})
```

## Implementation plan

### Phase 1: Core expansion (bodies, shapes, properties)

**C++ changes** (`physics.h`, `physics.cc`):
- Add `AddPolygon`, `AddEdge`, `AddChain` methods
- Add kinematic body support (body type parameter)
- Make material properties (density, friction, restitution) parameters
- Add per-body property getters/setters (velocity, damping, mass, gravity
  scale, fixed rotation, bullet, active, awake)
- Add `ApplyForceAt`, `ApplyLinearImpulseAt`, `ApplyAngularImpulse`
- Add coordinate transform methods (local/world)
- Add collision filter support (category/mask on shapes)
- Add sensor support

**Lua changes** (`lua_physics.cc`):
- Replace `add_box`/`add_circle` with `new_body` + `add_circle`/`add_box`/etc.
- Keep `add_box`/`add_circle` as convenience wrappers for backwards compat
- Add body method table (metatable with `__index`)
- Add world configuration functions

**Handle system change**: The current handle is just a raw `b2Body*` pointer
wrapped in userdata. This is unsafe (dangling pointer after destroy). Replace
with a generation-indexed handle like the collision system uses. The `Physics`
class maintains a `FixedArray<BodySlot>` with generation counters.

### Phase 2: Joints

**C++ changes**:
- Add `JointHandle` with generation counter
- Add `AddRevoluteJoint`, `AddDistanceJoint`, etc. (one per joint type)
- Add joint property getters/setters
- Add `DestroyJoint`

**Lua changes**:
- Add `physics.new_joint(type, body_a, body_b, params)` dispatcher
- Add `physics_joint` userdata type with metatable
- Joint methods on the metatable

### Phase 3: Callbacks and queries

**C++ changes**:
- Implement deferred action queue (create/destroy during callbacks)
- Add `preSolve` and `postSolve` contact listener overrides
- Expose contact data (normal, points, impulses)
- Add `Raycast`, `QueryAABB`, `QueryPoint` world query methods

**Lua changes**:
- Add `physics.on_begin_contact`, `on_end_contact`, `on_pre_solve`,
  `on_post_solve`
- Add `physics.raycast`, `physics.raycast_all`, `physics.query_rect`,
  `physics.query_point`

### Phase 4: Debug drawing

**C++ changes**:
- Implement `b2Draw` subclass that renders via `Renderer`
- Add draw mode state to `Physics`

**Lua changes**:
- Add `physics.set_draw_mode(mode)`

## Migration

The existing API (`physics.add_box`, `physics.add_circle`, etc.) should
continue to work unchanged. The new API is additive. The old functions become
thin wrappers around `new_body` + `add_box`/`add_circle`.

The auto-friction-joint behavior for `add_box`/`add_circle` is preserved
regardless of mode. New bodies created via `new_body` get auto-friction-joints
only when `physics.set_mode("topdown")` is active.

## Game-driven requirements

The original sections of this document argued for the physics expansion from
first principles and from engine-survey parity. This section grounds the plan
in *actual* games we care about supporting: the two a327ex titles analysed in
[[BYTEPATH and SNKRX porting analysis]] and the in-tree demo game
`games/space-garbage`. Where the first-principles plan and the game-driven
requirements disagree, the game-driven requirements should win — we'd rather
ship the joints SNKRX needs than the joints that round out a feature matrix.

### What BYTEPATH demands

BYTEPATH uses Windfield, which is a thin Box2D wrapper with named collision
classes. Its physics surface is narrow:

- Dynamic bodies only, with circular and rectangular shapes.
- Per-body linear/angular damping, fixed rotation, bullet flag.
- `beginContact` and `endContact` on sensors (aggro zones around enemies).
- `distance` and `rope` joints for tethered attacks and chained projectiles.
- Raycast against the world for target-finding.
- Per-fixture collision categories (hitboxes vs hurtboxes on the same body).

Every one of these is already in the proposed Phase 1/2 API. BYTEPATH does
**not** need kinematic bodies, wheel joints, prismatic joints, or pre-/post-
solve callbacks. If scheduling forces us to cut scope, those four can slide
to a later phase without blocking BYTEPATH.

### What SNKRX demands

SNKRX is the more physics-intensive of the two and defines the minimum bar
for a "real" physics expansion:

- **Revolute joints** are load-bearing: the player snake is a chain of
  circular bodies connected by revolute joints with soft limits. Without
  revolute joints we cannot port SNKRX at all.
- **Polygon and chain shapes** for arena walls and obstacles.
- **Sensors with begin- and end-contact callbacks.** Enemies flip state
  when entering/leaving sensor rings; missing `endContact` leaves them
  stuck.
- **Per-fixture filters.** Each link in the snake has different collision
  rules from its neighbours.
- **Linear/angular damping tuning** per-body for the chain's trailing feel.
- **`setBullet`** on fast projectiles to prevent tunnelling.

SNKRX does not use pre-/post-solve callbacks, mouse joints, wheel joints,
gear joints, friction joints, or the motor joint. It does not need world
raycasts.

### What `games/space-garbage` currently uses

The demo game exercises only the legacy `G.physics` surface: `add_box`,
`create_ground`, `on_begin_contact`, plus `apply_force`,
`apply_torque`, `apply_linear_impulse`, `rotate`, `position`, `angle`, and
the velocity/damping helpers added during the test-input coroutine work.
It uses four named categories (`player`, `meteor`, `bullet`, `powerup`) and
a single begin-contact callback.

Notable quirks:

- Every entity is a box, including the triangular player ship and the
  (visually) round meteors. Collisions are noticeably loose on the ship's
  nose and on meteors at glancing angles.
- The powerup entity does **not** use physics at all. It manually distance-
  checks against the player in `Powerup:check_pickup`. A sensor fixture
  would eliminate the bespoke check and let powerups be queried via the
  normal contact callback.
- Meteors drift through `apply_force` every frame because the world has
  zero gravity and because `set_linear_velocity` landed only recently.
- The player calls `apply_torque` for steering even though a kinematic-
  style `set_angular_velocity` would be a better fit for the tight arcade
  handling.
- All physics happens on an 8000×6000 toroidal world that Lua wraps
  manually. Box2D itself is blissfully unaware of the wrapping.

### What space-garbage would improve with the expanded API

Concrete improvements unlocked by Phase 1/2 of this document:

1. **Polygon ship hull.** Replace the player's bounding box with a 5–8
   vertex convex polygon matching the sprite silhouette. Meteors can stay
   as circles (via `new_circle`) for better glancing-collision behaviour.
   Estimated code change: ~20 lines in `player.lua` and `meteor.lua`.
2. **Sensor-based powerups.** Give each powerup a circular sensor fixture
   and react via the collision callback. Deletes
   `Powerup:check_pickup` and the per-frame sweep in `G1:update`.
3. **Material properties per shape.** The ship should be heavier than
   bullets, meteors should have higher restitution than the ship, and the
   ground should have proper friction rather than the current
   auto-friction-joint hack.
4. **`endContact` on bullet sensors.** Bullets currently self-destruct on
   begin-contact. With `endContact` we could implement piercing powerups
   (bullet lingers until it leaves the target volume) cleanly.
5. **`set_bullet(true)` on bullets.** Space-garbage bullets travel fast
   enough that tunnelling is a latent bug; flagging them as Box2D bullets
   fixes it with no Lua code change.
6. **Damping on drifting meteors** instead of per-frame `apply_force`. This
   both matches Box2D idioms and lets `set_linear_velocity` work without
   being immediately overwritten.
7. **Raycast for the aim line.** The player currently draws a dotted aim
   line of fixed length. A physics raycast to the nearest meteor along
   that ray would let us snap the line to real targets and draw a tiny
   reticle at the hit point.
8. **Compound player body.** If we want the wing guns to have their own
   hitboxes (e.g. for a future shield shape that only covers the front),
   a single body with multiple fixtures is the right shape.

None of these are blockers — space-garbage ships today — but they serve as
validation targets: after Phase 1/2 lands, we should port space-garbage to
the new API as a regression test for both ergonomics and behaviour.

### Phase re-prioritisation

Cross-referencing the three games above against the original Phase 1–4
plan suggests a small re-ordering:

- **Phase 1 stays as-is** (body/shape split, material properties,
  polygon/edge/chain, damping/mass, world config). All three games need
  this; nothing can ship without it.
- **Phase 2 should lead with `endContact`, sensors, revolute joints, and
  per-fixture filters** — the SNKRX minimum. Distance and rope joints
  follow for BYTEPATH. Weld, prismatic, wheel, gear, friction, and motor
  joints can move later in Phase 2 or into a Phase 2.5; they are needed
  for the Siege/Wheelie test games but not for any real game we're
  tracking.
- **Phase 3** (raycast, world queries, pre-/post-solve, contact filtering
  callbacks) keeps raycast at the top because BYTEPATH and space-garbage
  both want it. Pre-/post-solve remains low priority — neither real game
  uses them.
- **Phase 4** (debug draw) is unchanged in importance but becomes *more*
  valuable now that real games will be stressing the system: porting
  space-garbage to the new API is much safer with debug rendering of
  polygon hulls.

### Test game status

The originally proposed "Siege" and "Wheelie" test games remain useful
because together they exercise every joint type, every body type, and both
callback paths. However, **porting `games/space-garbage` to the new API is
now the primary Phase 1/2 acceptance test**, because it is real game code
we already ship, already exercise, and already have bugs in. Siege and
Wheelie become Phase 2/3 exercises — valuable for coverage but no longer
the first thing we build.

## Test games

### Test game 1: Angry Birds clone ("Siege")

A catapult-based destruction game that exercises bodies, shapes, joints, and
collision callbacks.

**Physics features exercised**:
- **Multiple shape types**: Boxes for wooden planks, circles for boulders,
  polygons for irregular debris and stone blocks
- **Joints**: Revolute joints for the catapult arm pivot; distance joint as
  the slingshot elastic; weld joints holding the structure together (break on
  high-impulse contact via `on_post_solve`)
- **Material properties**: Wood (low density, medium friction, low
  restitution), stone (high density, high friction, zero restitution), rubber
  (medium density, low friction, high restitution), ice (low density, low
  friction, medium restitution)
- **Collision callbacks**: `on_post_solve` detects impact force to break weld
  joints and trigger destruction animations. `on_begin_contact` detects
  projectile hitting enemies.
- **Kinematic bodies**: Moving platforms that shift the structure
- **Sensors**: Victory zone that detects when all enemies are destroyed
- **World queries**: Raycast from catapult to aim assist line. Point query for
  click-to-select.
- **Debug draw**: Essential during level design to visualize shape placement

**Gameplay**:
1. Player aims catapult by dragging (mouse joint on the projectile)
2. Release launches projectile along elastic trajectory
3. Projectile hits a structure of welded-together blocks
4. High-impulse contacts break weld joints, structure collapses
5. Goal: destroy all enemy targets (circles with sensor shapes for detection)

**Level data** (Lua):

```lua
local G = G

local siege = {}

function siege.init()
  -- World setup
  physics.set_gravity(0, 980)

  -- Ground
  physics.create_boundary(800, 600)

  -- Catapult base (static)
  local base = physics.new_box("static", {
    x = 100, y = 480, width = 60, height = 40,
    friction = 0.8,
  })

  -- Catapult arm (dynamic, hinged to base)
  local arm = physics.new_box("dynamic", {
    x = 100, y = 440, width = 10, height = 80,
    density = 2.0,
  })
  local pivot = physics.new_joint("revolute", base, arm, {
    anchor_x = 100, anchor_y = 480,
    enable_limit = true,
    lower_angle = -math.pi / 3,
    upper_angle = math.pi / 12,
  })

  -- Build target structure from level data
  local planks = {}
  local structure = {
    -- {type, x, y, w, h, material}
    {"box", 600, 560, 20, 80, "wood"},
    {"box", 680, 560, 20, 80, "wood"},
    {"box", 640, 510, 100, 20, "stone"},
    {"circle", 640, 490, 15, nil, "rubber"},
  }

  local materials = {
    wood  = { density = 0.5, friction = 0.4, restitution = 0.1 },
    stone = { density = 3.0, friction = 0.8, restitution = 0.0 },
    rubber = { density = 0.8, friction = 0.3, restitution = 0.7 },
  }

  -- Create structure with weld joints
  for _, piece in ipairs(structure) do
    local mat = materials[piece[6]]
    local body
    if piece[1] == "box" then
      body = physics.new_box("dynamic", {
        x = piece[2], y = piece[3],
        width = piece[4], height = piece[5],
        density = mat.density,
        friction = mat.friction,
        restitution = mat.restitution,
      })
    else
      body = physics.new_circle("dynamic", {
        x = piece[2], y = piece[3],
        radius = piece[4],
        density = mat.density,
        friction = mat.friction,
        restitution = mat.restitution,
      })
    end
    table.insert(planks, body)
  end

  -- Weld adjacent pieces together (break on impact)
  local welds = {}
  for i = 1, #planks - 1 do
    local j = physics.new_joint("weld", planks[i], planks[i + 1], {
      anchor_x = 640, anchor_y = 535,
      frequency = 8.0,      -- slightly flexible
      damping_ratio = 0.5,
    })
    table.insert(welds, j)
  end

  -- Break welds on heavy impact
  physics.on_post_solve(function(body_a, body_b, contact)
    if contact.normal_impulse > 300 then
      for i, w in ipairs(welds) do
        local ba, bb = w:get_bodies()
        if ba == body_a or ba == body_b or bb == body_a or bb == body_b then
          w:destroy()
          table.remove(welds, i)
          break
        end
      end
    end
  end)
end

function siege.update(t, dt)
  -- Mouse joint for aiming would go here
end

function siege.draw()
  -- Draw sprites at body positions/angles
  for _, body in ipairs(all_bodies) do
    local x, y = body:get_position()
    local a = body:get_angle()
    G.graphics.draw(sprite, x, y, a)
  end
end

return siege
```

### Test game 2: Physics platformer with vehicles ("Wheelie")

A side-scrolling platformer where the player drives a vehicle over terrain,
crosses bridges, and navigates obstacles. Exercises wheel joints, chain shapes,
distance joints, and kinematic bodies.

**Physics features exercised**:
- **Wheel joints**: Vehicle chassis + two wheels with motor and suspension
- **Chain shapes**: Terrain contours (static body with chain shape)
- **Distance joints**: Rope bridges that flex under the vehicle's weight
- **Revolute joints**: Spinning windmill obstacles, hinged drawbridges
- **Prismatic joints**: Elevator platforms (motor-driven slider)
- **Kinematic bodies**: Moving platforms on fixed paths
- **Per-body gravity scale**: Zero-gravity zones
- **Collision filtering**: Vehicle wheels collide with terrain but not with
  each other. Bridge segments collide with vehicle but not with each other.
- **Debug draw**: Visualize suspension compression, bridge flex, terrain
  collision chain
- **Sensors**: Checkpoint zones, hazard zones, finish line

**Gameplay**:
1. Player controls a 2-wheeled vehicle (accelerate, brake, lean)
2. Terrain is a chain shape with hills and valleys
3. Rope bridges sag and swing as the vehicle crosses
4. Elevators (prismatic joints) carry the vehicle between levels
5. Windmill obstacles (revolute joints with motors) must be timed
6. Zero-gravity zones require careful momentum management
7. Goal: reach the finish line sensor

**Vehicle construction** (Lua):

```lua
local G = G

local wheelie = {}

function wheelie.init()
  physics.set_gravity(0, 600)

  -- Terrain (static body with chain shape)
  local terrain = physics.new_body("static", { x = 0, y = 0 })
  terrain:add_chain({
    vertices = {
      0, 400,  100, 380,  200, 390,  300, 350,
      400, 370,  500, 300,  600, 310,  700, 400,
      800, 400,  900, 380,  1000, 400,
    },
    loop = false,
    friction = 0.8,
  })

  -- Vehicle chassis
  local chassis = physics.new_box("dynamic", {
    x = 100, y = 340,
    width = 60, height = 20,
    density = 1.0,
  })

  -- Front wheel
  local front_wheel = physics.new_circle("dynamic", {
    x = 130, y = 360,
    radius = 15,
    density = 0.5,
    friction = 0.9,
    category = 0x0002,
  })

  -- Rear wheel (driven)
  local rear_wheel = physics.new_circle("dynamic", {
    x = 70, y = 360,
    radius = 15,
    density = 0.5,
    friction = 0.9,
    category = 0x0002,
  })

  -- Wheel joints (suspension + motor)
  local front_axle = physics.new_joint("wheel", chassis, front_wheel, {
    anchor_x = 130, anchor_y = 360,
    axis_x = 0, axis_y = 1,     -- vertical suspension
    frequency = 4.0,
    damping_ratio = 0.7,
    enable_motor = false,
  })

  local rear_axle = physics.new_joint("wheel", chassis, rear_wheel, {
    anchor_x = 70, anchor_y = 360,
    axis_x = 0, axis_y = 1,
    frequency = 4.0,
    damping_ratio = 0.7,
    enable_motor = true,
    motor_speed = 0,
    max_motor_torque = 200,
  })

  -- Rope bridge (chain of small boxes connected by distance joints)
  local bridge_links = {}
  local link_width = 30
  local bridge_start_x = 500
  local bridge_y = 300

  -- Anchor posts (static)
  local post_a = physics.new_box("static", {
    x = bridge_start_x, y = bridge_y,
    width = 10, height = 40,
  })
  local post_b = physics.new_box("static", {
    x = bridge_start_x + 8 * link_width, y = bridge_y,
    width = 10, height = 40,
  })

  -- Bridge segments
  for i = 0, 7 do
    local link = physics.new_box("dynamic", {
      x = bridge_start_x + i * link_width + link_width / 2,
      y = bridge_y,
      width = link_width - 2, height = 6,
      density = 0.3,
      friction = 0.6,
      category = 0x0004,
      mask = 0xFFFB,    -- don't collide with other bridge pieces (0x0004)
    })
    table.insert(bridge_links, link)
  end

  -- Connect bridge links with distance joints
  local prev = post_a
  for i, link in ipairs(bridge_links) do
    local px, _ = prev:get_position()
    local lx, _ = link:get_position()
    physics.new_joint("distance", prev, link, {
      anchor_a_x = px + link_width / 2, anchor_a_y = bridge_y,
      anchor_b_x = lx - link_width / 2, anchor_b_y = bridge_y,
      frequency = 3.0,
      damping_ratio = 0.4,
    })
    prev = link
  end
  -- Connect last link to post_b
  local lx, _ = prev:get_position()
  physics.new_joint("distance", prev, post_b, {
    anchor_a_x = lx + link_width / 2, anchor_a_y = bridge_y,
    anchor_b_x = bridge_start_x + 8 * link_width, anchor_b_y = bridge_y,
    frequency = 3.0,
    damping_ratio = 0.4,
  })

  -- Elevator (prismatic joint with motor)
  local elevator_platform = physics.new_box("kinematic", {
    x = 800, y = 400,
    width = 80, height = 10,
  })
  local elevator_anchor = physics.new_body("static", { x = 800, y = 300 })
  local elevator_joint = physics.new_joint("prismatic", elevator_anchor,
    elevator_platform, {
    anchor_x = 800, anchor_y = 350,
    axis_x = 0, axis_y = 1,
    enable_limit = true,
    lower_translation = -100,
    upper_translation = 100,
    enable_motor = true,
    motor_speed = 50,
    max_motor_force = 1000,
  })

  -- Spinning windmill obstacle
  local windmill_base = physics.new_body("static", { x = 900, y = 300 })
  local windmill_blade = physics.new_box("dynamic", {
    x = 900, y = 300,
    width = 120, height = 10,
    density = 0.5,
  })
  physics.new_joint("revolute", windmill_base, windmill_blade, {
    anchor_x = 900, anchor_y = 300,
    enable_motor = true,
    motor_speed = 3.0,
    max_motor_torque = 500,
  })

  -- Store references for update
  wheelie.rear_axle = rear_axle
  wheelie.chassis = chassis
end

function wheelie.update(t, dt)
  -- Vehicle controls
  if G.input.is_down("right") then
    wheelie.rear_axle:set_motor_speed(-30)
  elseif G.input.is_down("left") then
    wheelie.rear_axle:set_motor_speed(30)
  else
    wheelie.rear_axle:set_motor_speed(0)
  end

  -- Lean control
  if G.input.is_down("up") then
    wheelie.chassis:apply_torque(-100)
  elseif G.input.is_down("down") then
    wheelie.chassis:apply_torque(100)
  end
end

function wheelie.draw()
  -- Camera follows chassis
  local cx, cy = wheelie.chassis:get_position()
  G.graphics.set_camera(cx - 400, cy - 300)

  -- Draw terrain, vehicle, bridges, obstacles...
  -- (sprite drawing at body positions)
end

return wheelie
```

**Why these two games?**

| Feature | Siege (Angry Birds) | Wheelie (Vehicle platformer) |
|---|---|---|
| Body types | Dynamic, static, kinematic | Dynamic, static, kinematic |
| Shapes | Box, circle, polygon | Box, circle, chain |
| Revolute joint | Catapult pivot | Windmill obstacle |
| Distance joint | Slingshot elastic | Rope bridge |
| Weld joint | Breakable structure | -- |
| Prismatic joint | -- | Elevator |
| Wheel joint | -- | Vehicle suspension |
| Mouse joint | Aiming | -- |
| Rope joint | -- | Tether (optional) |
| Sensors | Victory zone | Checkpoints |
| Collision filter | Projectile vs structure | Wheels vs bridge |
| Post-solve callback | Break on impact | -- |
| Pre-solve callback | -- | One-way platforms (optional) |
| Debug draw | Level design | Suspension tuning |
| Kinematic bodies | Moving platforms | -- |
| Chain shapes | -- | Terrain |
| Gravity scale | -- | Zero-G zones |

Together they exercise every joint type at priority 1-6, all three body types,
five shape types, both callback systems, collision filtering, sensors, world
queries, and debug drawing. They represent two of the most popular 2D game
genres that require physics.

## Decisions

1. **No compound shape convenience constructors.** Compound bodies are
   uncommon enough that `new_body` + multiple `add_*` calls is fine. Keeps the
   convenience constructors (`new_box`, `new_circle`) simple.

2. **Top-down mode via world setting.** Add `physics.set_mode("topdown")` to
   control whether new bodies get automatic friction joints. Default is
   `"sideview"` (no auto-friction). The old `add_box`/`add_circle` API
   preserves its existing behavior regardless of mode.

3. **Debug draw as separate overlay pass.** Rendered after the game's `draw()`
   returns, so debug lines are never occluded by game sprites. Uses the batch
   renderer but in a dedicated pass.

4. **Simplified contact data.** Expose only the first contact point, normal,
   and total impulse. This covers 99% of use cases (impact detection, surface
   normal for effects). Can expand to full manifold later if needed.

5. **Fixed timestep already handled.** The engine already uses a fixed
   timestep accumulator with scaling, so Box2D gets stable fixed-size steps.
   No additional work needed here.

6. **Platformers should use G.collision.** One-way platforms, `move_and_slide`,
   and tile collision are all better served by the collision system. The
   physics expansion targets genres that need rigid body simulation:
   destruction, vehicles, ragdolls, pinball. Document this guidance clearly in
   the Lua API docs.