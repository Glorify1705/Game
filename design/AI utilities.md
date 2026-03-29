
# AI Utilities

Reusable primitives for enemy and NPC behavior in 2D games: pathfinding, steering,
spatial awareness, state machines, and scheduling. These utilities build on the
existing collision, math, and timer systems.

## Glossary

**Steering behavior**: A function that computes a force or velocity vector to move
an agent toward a goal. Craig Reynolds coined the term in 1987. "Seek" steers
toward a target, "Flee" steers away, "Arrive" steers toward but decelerates near
the target. Steering behaviors are the bread and butter of 2D enemy AI — nearly
every action game uses some combination of them.

**Finite state machine (FSM)**: A model where an entity is in exactly one state at
a time (idle, chase, attack, flee) and transitions between states based on
conditions. The dominant AI pattern in 2D action games because it is simple,
debuggable, and maps directly to how designers think about enemy behavior.

**Behavior tree**: A tree of nodes (selectors, sequences, conditions, actions)
evaluated top-down each tick. More composable than FSMs for complex AI but heavier
and harder to debug. Common in 3D AAA games, rare in 2D indie games.

**A\* (A-star)**: A graph search algorithm that finds the shortest path between two
nodes using a heuristic to guide exploration. The standard pathfinding algorithm
for grid-based 2D games since 1968.

**Navigation grid**: A 2D grid overlaid on the game world where each cell is marked
walkable or blocked. A* runs on this grid. GameMaker, Godot, and most 2D engines
use this approach rather than navmeshes (which suit 3D better).

**Line of sight (LOS)**: A raycast between two points to check whether a straight
line is occluded by obstacles. Used for enemy detection ("can this enemy see the
player?"), shooting AI, and alert propagation.

**Vision cone**: A sector-shaped detection area defined by a direction, half-angle,
and range. An enemy "sees" the player only if the player is within the cone AND
passes a line-of-sight check. The combination of angle + distance + LOS is the
standard detection model in stealth and action games.

**Separation**: A steering behavior that pushes agents apart to prevent stacking.
The single most commonly needed group behavior — without it, enemies pile into the
same pixel when they all chase the same target.

**Path following**: Smoothly moving an agent along a list of waypoints. Given a
path from A*, the agent needs to steer toward successive waypoints with
configurable look-ahead distance.

**Tick throttling**: Running expensive AI logic less frequently than every frame.
If 200 enemies each run A* every frame, performance collapses. Staggering updates
(enemy 0 on frame 0, enemy 1 on frame 1, ...) spreads the cost.

## Motivation

The engine provides strong low-level building blocks: collision queries (raycast,
circle/rect overlap, spatial hash), vector math (v2 with dot/normalize/length),
timers with easing, and seeded RNG. But writing enemy AI still requires
significant boilerplate:

- **Steering**: Computing a seek vector means manually subtracting positions,
  normalizing, scaling by speed, and clamping. Arrive requires additional
  deceleration math. Wander requires maintaining a random angle offset. Every game
  reimplements these same vector operations.

- **Pathfinding**: No path exists from "enemy at A, player at B, walls in between"
  to "list of waypoints." Users must implement grid creation, A*, and path
  smoothing from scratch — or pull in a Lua library and integrate it with the
  collision system.

- **Detection**: Checking "can enemy E see player P within 120° and 200px?" requires
  combining `G.math.v2` subtraction, `math.atan2`, angle wrapping, distance
  checks, and `world:raycast`. This is ~15 lines of careful math every time.

- **State management**: Lua FSMs are trivial in theory (table of functions) but
  in practice every project writes slightly different boilerplate for enter/exit
  callbacks, transition conditions, and debug visualization.

These are not complex systems. They are small, well-understood utilities that every
2D action game needs. Providing them saves users from writing the same 50-200 lines
of math/logic per project and reduces the surface area for subtle bugs (angle
wrapping, deceleration curves, grid alignment).

## Engine survey

### Pathfinding

| Feature | GameMaker | Godot | Love2D (libs) | Unity | Defold |
|---|---|---|---|---|---|
| **Grid A\*** | `mp_grid_path()` | `NavigationServer2D` (navmesh) | Jumper, Luafinding | NavMesh (3D only) | defold-astar (ext) |
| **Grid creation** | `mp_grid_create(x,y,w,h,cw,ch)` | `NavigationPolygon` resource | Manual Lua tables | Bake in editor | Manual |
| **Obstacle marking** | `mp_grid_add_instances()` | Navigation regions | Manual cell marking | NavMeshObstacle | Manual |
| **Path smoothing** | Built-in | Built-in (funnel) | pathfun (funnel) | Built-in | None |
| **Dynamic updates** | `mp_grid_clear_cell()` | Region rebake | Re-run search | Carve at runtime | Re-run |
| **Cost per cell** | No (binary) | Yes (cost map) | Jumper supports | Yes (area cost) | No |
| **Diagonal movement** | Yes | N/A (navmesh) | Configurable | N/A | Configurable |

GameMaker's `mp_grid` API is the best reference for a 2D engine: simple grid
creation, mark cells from collision geometry, compute path, done. Godot's navmesh
approach is more powerful but far heavier — overkill for tile-based or arena 2D
games. Love2D has no built-in pathfinding; the community uses Jumper (pure Lua A*
with JPS optimization) or pathfun (funnel algorithm ported from Godot).

#### Key takeaways

1. **Grid-based A\* is the standard for 2D.** Every major 2D engine either
   provides it or has a dominant community library for it. Navmesh is a 3D
   solution awkwardly ported to 2D.

2. **Auto-populating the grid from collision data is high value.** GameMaker's
   `mp_grid_add_instances()` and the ability to mark cells from existing collision
   geometry eliminates tedious manual setup. Our spatial hash already knows where
   obstacles are.

3. **Binary walkability is sufficient for most 2D games.** Weighted costs add
   complexity. Start with walkable/blocked and add cost support later if needed.

4. **JPS (Jump Point Search) is a significant optimization** over plain A* on
   uniform-cost grids. Jumper and other fast implementations use it. Worth
   considering for the C++ implementation.

### Steering behaviors

| Feature | gdx-ai (LibGDX) | GDQuest (Godot addon) | Karai (Love2D) | GameMaker |
|---|---|---|---|---|
| **Seek** | Yes | Yes | Yes | No |
| **Flee** | Yes | Yes | Yes | No |
| **Arrive** | Yes (deceleration radius) | Yes | Yes | No |
| **Wander** | Yes (circle projection) | Yes | Yes | No |
| **Pursue** | Yes (velocity prediction) | Yes | No | No |
| **Evade** | Yes (velocity prediction) | Yes | No | No |
| **Separation** | Yes | Yes | No | No |
| **Alignment** | Yes | Yes | No | No |
| **Cohesion** | Yes | Yes | No | No |
| **Obstacle avoidance** | Yes (raycast) | Yes (raycast) | No | `mp_potential_step()` |
| **Path following** | Yes (param-based) | Yes | No | `mp_linear_step()` |
| **Blending/priority** | Yes (weighted, priority) | Yes | No | No |
| **Formation** | Yes (slots) | No | No | No |

No major engine ships steering behaviors as built-in. They are always addons or
libraries. However, the core six (Seek, Flee, Arrive, Wander, Pursue, Evade) are
trivially small — each is 5-15 lines of vector math. The value is in providing a
consistent, tested, tunable API rather than the algorithmic complexity.

GameMaker's `mp_potential_step(x, y, speed, solid_only)` is notable: it combines
seeking toward a target with basic obstacle avoidance in a single function. Simple
and extremely useful for quick prototyping.

#### Key takeaways

1. **Seek, Flee, Arrive, Wander are table stakes.** These cover 80% of 2D enemy
   movement. Arrive (smooth deceleration) and Wander (natural-looking randomness)
   are the two that users get wrong most often.

2. **Pursue and Evade add significant intelligence cheaply.** Predicting where the
   target *will be* (based on velocity) makes enemies feel much smarter than
   chasing the target's current position.

3. **Separation is the most important group behavior.** Alignment and cohesion
   matter for flocking simulations but not for typical enemy groups. Separation
   alone prevents the "enemy stack" problem that plagues every action game.

4. **Steering output should be a velocity or acceleration vector**, not a direct
   position change. This lets users combine multiple behaviors (seek + separation +
   obstacle avoidance) by summing weighted vectors.

### State machines

| Feature | Godot | Unity | GameMaker | Love2D (libs) |
|---|---|---|---|---|
| **Built-in FSM** | AnimationTree only | Animator only | No | No |
| **Logic FSM** | No (trivial in GDScript) | No (trivial in C#) | No (switch/case) | knife, hump.gamestate |
| **Enter/exit callbacks** | Animation events | StateMachineBehaviour | Manual | Library-dependent |
| **Visual editor** | AnimationTree graph | Animator graph | No | No |
| **Hierarchical** | Via AnimationTree blend | Via sub-state machines | No | No |

No engine provides a general-purpose logic FSM as a built-in. The pattern is too
simple to justify framework weight in most languages. In Lua specifically, a state
is a table with `enter`, `update`, `exit` functions — 20 lines of glue code.

But those 20 lines of glue are worth providing because: (a) every project writes
them, (b) consistent structure enables debug visualization, and (c) hot-reload
interacts with state transitions in subtle ways (the engine should re-enter the
current state on reload).

### Spatial awareness

| Feature | Godot | Unity 2D | GameMaker | Our engine |
|---|---|---|---|---|
| **Raycast** | `intersect_ray()` | `Physics2D.Raycast()` | `collision_line()` | `world:raycast()` |
| **Circle query** | `Area2D` overlap | `OverlapCircle()` | `collision_circle_list()` | `world:query_circle()` |
| **Rect query** | `intersect_shape()` | `OverlapBox()` | `collision_rectangle()` | `world:query_rect()` |
| **Nearest of type** | Manual loop | Manual loop | `instance_nearest()` | Not provided |
| **Distance to** | `position.distance_to()` | `Vector2.Distance()` | `distance_to_object()` | Manual (v2 subtraction + len) |
| **Vision cone** | Manual | Manual | Manual | Manual |
| **LOS check** | Raycast + layer mask | Raycast + layer mask | `collision_line()` | Raycast + mask |

Our engine already provides the raw spatial queries (raycast, circle/rect overlap).
What's missing are the higher-level wrappers: "find nearest enemy within 300px,"
"can this entity see that entity within a 90° cone," "get all entities within
radius sorted by distance."

#### Key takeaways

1. **`find_nearest` is the most-wanted missing query.** GameMaker provides it as
   `instance_nearest()` and it is used constantly. Wrapping `query_circle` + min
   distance in C++ avoids a Lua loop over results.

2. **Vision cone is a composite query** (angle check + distance check + raycast).
   Every stealth/action game implements it. Providing it as a single call
   eliminates angle-wrapping bugs.

3. **Results sorted by distance** are often needed. Circle queries currently return
   unsorted handles. Adding a `query_circle_sorted` or a sort option would help.

### Behavior trees

No major 2D engine provides built-in behavior trees. Godot, Unity, and GameMaker
all rely on addons. LibGDX's gdx-ai is the only framework with a complete BT
implementation, but LibGDX targets a more enterprise/framework-heavy audience.

For 2D action games, behavior trees are rarely the right tool. They shine for
complex NPC routines (open-world RPG villagers, RTS unit AI) but add unnecessary
abstraction layers for pattern-based boss fights or simple chase/attack enemies.

**Recommendation**: Do not provide behavior trees. An FSM covers 90% of 2D AI
needs. Users who need BTs can implement them in Lua — the tree structure maps
naturally to Lua tables.

## Proposed API

### `G.steer` — Steering behaviors

All steering functions return `(vx, vy)` — a velocity vector that can be applied
directly or combined with other steering outputs. The agent's current velocity is
passed in to enable smooth behaviors (arrive deceleration, pursue prediction).

```lua
-- Basic movement
vx, vy = G.steer.seek(ax, ay, tx, ty, max_speed)
vx, vy = G.steer.flee(ax, ay, tx, ty, max_speed)

-- Decelerates smoothly within slow_radius of target
vx, vy = G.steer.arrive(ax, ay, tx, ty, max_speed, slow_radius)

-- Wander: returns a velocity that drifts naturally. The rng and state table
-- maintain continuity between frames (the wander angle persists).
-- circle_dist: how far ahead the wander circle is projected
-- circle_radius: radius of the wander circle (larger = more erratic)
-- jitter: max random angle change per call (radians)
vx, vy = G.steer.wander(ax, ay, rng, state, max_speed, circle_dist,
                         circle_radius, jitter)

-- Predictive: leads the target based on its velocity
vx, vy = G.steer.pursue(ax, ay, tx, ty, tvx, tvy, max_speed)
vx, vy = G.steer.evade(ax, ay, tx, ty, tvx, tvy, max_speed)

-- Group: push away from nearby neighbors. positions is {{x, y}, ...}
-- desired_separation is the minimum comfortable distance.
vx, vy = G.steer.separate(ax, ay, positions, desired_separation, max_speed)

-- Combining multiple behaviors (weighted sum)
vx, vy = G.steer.combine({
  {G.steer.seek(ax, ay, tx, ty, speed),   weight = 1.0},
  {G.steer.separate(ax, ay, neighbors, 30, speed), weight = 1.5},
})
```

#### Why C++ instead of Lua?

Steering functions are called per-entity per-frame. For 200 enemies, that is
200 `normalize` + `length` + `atan2` calls per frame. The vector math is
trivial in C++ and avoids Lua userdata allocation overhead. Wander and Pursue
need `atan2`/`cos`/`sin` which are C library calls anyway.

That said, steering functions are simple enough to prototype in Lua first and
move to C++ only if profiling shows a bottleneck.

### `G.nav` — Grid pathfinding

```lua
-- Create a navigation grid over a world region.
-- cell_size should match or be a multiple of your tile size.
grid = G.nav.grid(world_x, world_y, world_w, world_h, cell_size)

-- Mark cells as blocked. Three methods:
grid:block_rect(x, y, w, h)           -- Block a rectangle (world coords)
grid:block_circle(cx, cy, r)          -- Block a circle
grid:block_from_world(collision_world, mask)  -- Auto-block from collision data

-- Clear previously blocked cells
grid:clear_rect(x, y, w, h)
grid:clear_all()

-- Find a path. Returns a list of waypoints {{x, y}, ...} in world coords,
-- or nil if no path exists. Waypoints are cell centers.
path = grid:find_path(start_x, start_y, goal_x, goal_y)

-- Options table for find_path:
path = grid:find_path(start_x, start_y, goal_x, goal_y, {
  diagonal = true,      -- Allow diagonal movement (default: true)
  max_iterations = 1000, -- Cap search iterations (default: 1000)
})

-- Check if a specific cell is walkable
walkable = grid:is_walkable(world_x, world_y)

-- Debug: draw the grid (blocked cells in red, path in green)
grid:debug_draw()
```

#### Implementation notes

- **Algorithm**: A* with JPS (Jump Point Search) optimization for uniform-cost
  grids. JPS skips intermediate nodes on straight-line paths, reducing open-set
  operations by 10-30x on typical game maps.

- **Integration with collision**: `block_from_world` iterates the spatial hash
  and marks cells that overlap with static colliders. This bridges the collision
  system and pathfinding automatically.

- **Memory**: Grid stored as a flat bitfield. A 1000x1000 grid is ~125KB. Paths
  stored as `DynArray<FVec2>` and returned to Lua as a table of `{x, y}` pairs.

- **Budget**: `max_iterations` prevents A* from stalling on unsolvable or very
  long paths. When the budget is exhausted, `find_path` returns nil.

- **Path smoothing**: Optional string-pulling pass that removes redundant
  intermediate waypoints on straight segments. Produces cleaner paths for
  steering-based following.

### `G.awareness` — Spatial awareness helpers

Higher-level queries that combine existing collision primitives with distance
and angle math. These are the queries every AI system needs.

```lua
-- Find nearest collider to a point within radius, filtered by mask.
-- Returns handle, distance (or nil if none found).
handle, dist = G.awareness.nearest(world, x, y, radius, mask)

-- Find all colliders within radius, sorted by distance.
-- Returns {{handle, dist}, ...}
results = G.awareness.nearest_all(world, x, y, radius, mask)

-- Line of sight: can a straight line from (ax,ay) to (bx,by) pass
-- without hitting anything in the given mask? Returns bool.
clear = G.awareness.line_of_sight(world, ax, ay, bx, by, mask)

-- Vision cone: is target (tx,ty) visible from (ax,ay) facing direction
-- (dx,dy) within half_angle (radians) and max_range?
-- Combines angle check + distance check + LOS raycast.
visible = G.awareness.in_vision_cone(world, ax, ay, dx, dy,
                                      tx, ty, half_angle, max_range, mask)

-- Distance and angle between two points (convenience wrappers)
dist = G.awareness.distance(ax, ay, bx, by)
dist_sq = G.awareness.distance_squared(ax, ay, bx, by)
angle = G.awareness.angle_to(ax, ay, bx, by)  -- radians, atan2 convention
```

#### Why these belong in C++

`nearest` and `nearest_all` iterate collision query results and compute distances.
Doing this in Lua means pulling every handle across the Lua/C boundary, querying
each position, computing distance, and sorting. A C++ implementation queries the
spatial hash directly and returns only the final result.

`in_vision_cone` is a composite of three operations (angle, distance, raycast)
that would require three separate Lua API calls. Fusing them into one call is both
faster and less error-prone (angle wrapping is a common source of bugs).

### `G.fsm` — Finite state machine

A lightweight FSM helper. States are plain Lua tables with optional callbacks.
The FSM manages transitions and calls enter/exit hooks.

```lua
local machine = G.fsm.new({
  -- Define states as tables with callbacks
  idle = {
    enter = function(self, prev_state) end,
    update = function(self, dt)
      if self:distance_to_player() < 200 then
        self.fsm:transition("chase")
      end
    end,
    exit = function(self, next_state) end,
  },

  chase = {
    enter = function(self)
      self.chase_timer = 0
    end,
    update = function(self, dt)
      self.chase_timer = self.chase_timer + dt
      -- Move toward player
      local vx, vy = G.steer.pursue(
        self.x, self.y,
        player.x, player.y, player.vx, player.vy,
        self.speed
      )
      self.x = self.x + vx * dt
      self.y = self.y + vy * dt

      if self:distance_to_player() < 40 then
        self.fsm:transition("attack")
      elseif self:distance_to_player() > 400 then
        self.fsm:transition("idle")
      end
    end,
  },

  attack = {
    enter = function(self)
      self.attack_cooldown = 0.5
    end,
    update = function(self, dt)
      self.attack_cooldown = self.attack_cooldown - dt
      if self.attack_cooldown <= 0 then
        self:deal_damage()
        self.fsm:transition("chase")
      end
    end,
  },
}, "idle")  -- Initial state

-- Each frame:
machine:update(entity, dt)

-- Manual transition:
machine:transition("flee")

-- Query:
machine:current()  -- Returns state name string
```

#### Implementation: Lua or C++?

This should be a **Lua module** shipped with the engine (like `classic.lua` or
`lume.lua`), not a C++ binding. Reasons:

1. FSMs are pure logic — no math hotpath, no spatial queries, no per-frame
   cost worth optimizing.
2. Users will want to customize the FSM pattern (add hierarchical states, add
   transition guards, add history). A Lua module is trivially extensible; a C++
   binding is not.
3. The implementation is ~40 lines of Lua.

### `G.nav.follow_path` — Path following

```lua
-- Create a path follower for an entity
follower = G.nav.follow_path(path, {
  look_ahead = 32,    -- How far ahead on the path to steer toward
  arrival_dist = 8,   -- Distance to waypoint before advancing to next
})

-- Each frame: returns a steering velocity toward the next waypoint
vx, vy = follower:steer(x, y, max_speed)

-- Query state
done = follower:is_done()
wx, wy = follower:current_waypoint()
progress = follower:progress()  -- 0.0 to 1.0
```

## What NOT to provide

These were considered and intentionally excluded:

- **Behavior trees**: Too heavy for 2D action games. FSMs cover 90% of use cases.
  Users who need BTs can compose them in Lua tables — the structure maps naturally.

- **Blackboard systems**: Only useful alongside behavior trees. Without BTs, a
  plain Lua table serves the same purpose.

- **Utility AI / scoring systems**: Interesting but niche. The evaluation function
  is always game-specific. A generic framework adds API surface without saving
  meaningful work.

- **Navmesh pathfinding**: Designed for 3D environments with irregular geometry.
  Grid-based A* is simpler, faster, and a better fit for 2D tile/pixel art games.

- **RVO/ORCA obstacle avoidance**: Useful for crowd simulation (50+ agents in
  tight spaces). Overkill for typical 2D action games where separation +
  steering is sufficient.

- **Formation movement**: Very specific to RTS games. Not general-purpose enough
  for the engine.

- **AI tick throttling**: Better handled by the user with `G.timer.every()` or a
  simple frame counter. A built-in throttler would impose opinions about update
  frequency that vary per game.

## Implementation priority

**Phase 1** — Immediate high value, low effort:

1. `G.awareness` (nearest, line_of_sight, vision_cone, distance/angle helpers).
   These are thin wrappers over existing collision queries. Small C++ surface,
   large usability improvement.

2. `G.steer` (seek, flee, arrive, wander, pursue, evade, separate).
   Pure vector math. Can prototype in Lua first, move to C++ if needed.

3. FSM Lua module shipped in `assets/`.

**Phase 2** — High value, moderate effort:

4. `G.nav.grid` + `G.nav.find_path` (A* with JPS on a navigation grid).
   Requires new C++ code for the grid data structure and pathfinder. The
   `block_from_world` integration with the spatial hash is the hardest part.

5. `G.nav.follow_path` (path following with look-ahead).
   Can be Lua initially, wrapping `G.steer.arrive` on successive waypoints.

## Full example: basic enemy AI

Putting it all together — an enemy that patrols, detects the player, chases,
attacks, and retreats when low on health:

```lua
local fsm = require("fsm")

function Enemy:init(x, y, world)
  self.x, self.y = x, y
  self.vx, self.vy = 0, 0
  self.hp = 100
  self.speed = 80
  self.world = world
  self.wander_state = {}
  self.rng = G.random.from_seed(x * 1000 + y)

  self.fsm = fsm.new({
    patrol = {
      update = function(_, dt)
        -- Wander randomly
        self.vx, self.vy = G.steer.wander(
          self.x, self.y, self.rng, self.wander_state,
          self.speed * 0.5, 40, 20, 0.5
        )
        -- Check for player in vision cone
        local dx, dy = self.vx, self.vy  -- Face movement direction
        if G.awareness.in_vision_cone(
          self.world, self.x, self.y, dx, dy,
          player.x, player.y, math.pi / 3, 200, MASK_WALLS
        ) then
          self.fsm:transition("chase")
        end
      end,
    },

    chase = {
      update = function(_, dt)
        self.vx, self.vy = G.steer.combine({
          {G.steer.pursue(self.x, self.y, player.x, player.y,
                          player.vx, player.vy, self.speed), weight = 1.0},
          {G.steer.separate(self.x, self.y, nearby_enemies,
                            30, self.speed),                  weight = 1.2},
        })

        if G.awareness.distance(self.x, self.y, player.x, player.y) < 40 then
          self.fsm:transition("attack")
        elseif not G.awareness.line_of_sight(
          self.world, self.x, self.y, player.x, player.y, MASK_WALLS
        ) then
          -- Lost sight, pathfind to last known position
          self.path = nav_grid:find_path(self.x, self.y, player.x, player.y)
          if self.path then
            self.follower = G.nav.follow_path(self.path)
            self.fsm:transition("pathfind")
          else
            self.fsm:transition("patrol")
          end
        end
      end,
    },

    pathfind = {
      update = function(_, dt)
        self.vx, self.vy = self.follower:steer(self.x, self.y, self.speed)
        if self.follower:is_done() then
          self.fsm:transition("patrol")
        end
        -- Re-acquire player if visible again
        if G.awareness.in_vision_cone(
          self.world, self.x, self.y, self.vx, self.vy,
          player.x, player.y, math.pi / 3, 200, MASK_WALLS
        ) then
          self.fsm:transition("chase")
        end
      end,
    },

    attack = {
      enter = function()
        self.attack_timer = 0.4
      end,
      update = function(_, dt)
        self.vx, self.vy = 0, 0
        self.attack_timer = self.attack_timer - dt
        if self.attack_timer <= 0 then
          -- Deal damage, check health
          if self.hp < 30 then
            self.fsm:transition("flee")
          else
            self.fsm:transition("chase")
          end
        end
      end,
    },

    flee = {
      update = function(_, dt)
        self.vx, self.vy = G.steer.flee(
          self.x, self.y, player.x, player.y, self.speed * 1.2
        )
        if G.awareness.distance(self.x, self.y, player.x, player.y) > 300 then
          self.fsm:transition("patrol")
        end
      end,
    },
  }, "patrol")
end

function Enemy:update(dt)
  self.fsm:update(self, dt)
  -- Apply velocity via collision system
  self.x, self.y = self.world:move_and_slide(self.handle, self.vx * dt, self.vy * dt)
end
```

## Open questions

- **Should `G.steer` return acceleration or velocity?** Velocity is simpler
  (apply directly). Acceleration allows mass-based movement and smoother blending
  but requires the user to maintain velocity state. Leaning toward velocity for
  simplicity — users can divide by dt for acceleration if needed.

- **Should the nav grid support weighted costs?** Binary (walkable/blocked)
  covers most 2D games. Weighted costs enable "prefer roads over grass" but add
  API complexity and prevent JPS optimization (JPS requires uniform cost). Start
  with binary, add weights as a later extension if demanded.

- **Should `G.awareness` functions accept `v2` userdata or `x, y` pairs?** The
  rest of the engine's Lua API uses `x, y` pairs (collision, physics, graphics).
  Consistency says pairs. But `v2` is cleaner for combining with steering output.
  Could accept both via type checking, but overloaded APIs are harder to document.
  Leaning toward `x, y` pairs for consistency, with `v2` overloads as a future
  convenience.

- **Path caching / invalidation?** If the nav grid changes (door opens, wall
  destroyed), existing paths may become invalid. Should paths auto-invalidate, or
  should users re-query? Leaning toward explicit re-query — implicit invalidation
  requires bookkeeping that adds complexity for a rare case.
