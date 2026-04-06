---
status: implemented
tags: [physics, collision]
---

# Collision Detection System

This document researches 2D collision detection approaches across game engines and libraries, analyzes the engine's current physics integration, and proposes a standalone collision detection library for the engine that supports broad phase, narrow phase, spatial queries, and collision filtering — without requiring full physics simulation.

## Motivation

The engine currently has a Box2D wrapper (`physics.h`, `physics.cc`) that provides rigid body physics with collision callbacks. This works well for physics-driven gameplay (gravity, impulses, bouncing) but is a poor fit for many common 2D game scenarios:

- **Platformers**: Need `move_and_slide` collision resolution without physics simulation. Box2D's solver fights against direct position control.
- **Top-down games**: Need overlap detection and custom response (e.g. slide along walls) without gravity or rigid body dynamics.
- **Bullet hells / shoot-em-ups**: Need fast overlap tests for thousands of projectiles. Box2D's solver overhead is unnecessary.
- **Trigger zones**: Need "sensor" regions that detect entry/exit without physical response. Box2D supports sensors but requires creating full bodies and fixtures.
- **Spatial queries**: Need raycasts, area queries, and point-in-shape tests exposed to Lua. The current API exposes none of these.

A dedicated collision detection library complements Box2D: games that need physics use `G.physics`, games that need collision detection only use `G.collision`.

## Current physics system

The engine wraps Box2D 2.x in `src/physics.h` (254 lines) and `src/physics.cc` (221 lines), exposed to Lua via `src/lua_physics.cc` (207 lines).

### What exists today

```cpp
class Physics : public b2ContactListener {
  static constexpr float kPixelsPerMeter = 60;

  Handle AddBox(FVec2 top_left, FVec2 bottom_right, float angle, uintptr_t userdata);
  Handle AddCircle(FVec2 position, double radius, uintptr_t userdata);
  void DestroyHandle(Handle handle);

  FVec2 GetPosition(Handle handle) const;
  float GetAngle(Handle handle) const;

  void ApplyLinearImpulse(Handle handle, FVec2 v);
  void ApplyForce(Handle handle, FVec2 v);
  void ApplyTorque(Handle handle, float torque);

  void SetBeginContactCallback(ContactCallback, void*);
  void SetEndContactCallback(ContactCallback, void*);

  void Update(float dt);
};
```

### Limitations for collision-only use

| Limitation | Impact |
|---|---|
| Only boxes and circles | No capsules, polygons, line segments, or rays |
| No spatial queries | Cannot raycast, query a region, or test point-in-shape |
| No collision filtering | All dynamic bodies collide with all other dynamic bodies |
| Requires physics simulation | Must call `Update(dt)` with gravity, solver iterations, etc. |
| Position in meters | Must convert to/from pixels through kPixelsPerMeter scaling |
| No trigger/sensor support | Every body participates in physical response |
| No direct position control | Moving a body directly breaks the solver; must use forces/impulses |

## Comparison with other engines

### Godot

Godot's 2D physics provides a rich collision system built on a custom physics engine (not Box2D).

**Shapes**: CircleShape2D, RectangleShape2D, CapsuleShape2D, SegmentShape2D, ConvexPolygonShape2D, ConcavePolygonShape2D, SeparationRayShape2D, WorldBoundaryShape2D. Performance hierarchy: Circle > Rectangle > Capsule > Segment > ConvexPolygon >> ConcavePolygon.

**Broad phase**: Dynamic BVH (bounding volume hierarchy), replacing an older hash grid in Godot 3.4+. The BVH showed ~30-35% improvement in physics tick times across benchmarks (500 to 125,000 bodies). Template-based code handles both 2D and 3D. Uses pooled allocation for cache coherency and fixed margin values for controlling collision "stickiness."

**Narrow phase**: SAT (Separating Axis Theorem) primarily. Specialized fast paths for primitive shape pairs (circle-circle, circle-rectangle, rectangle-rectangle). GJK-EPA fallback for complex curved shapes, though this is rarely needed in 2D.

**API model**: Node-based. Every collision-capable node (`Area2D`, `StaticBody2D`, `RigidBody2D`, `CharacterBody2D`) requires a `CollisionShape2D` child. `CharacterBody2D.move_and_slide()` provides the archetypal "game-feel" movement with automatic sliding along surfaces. `Area2D` provides trigger/sensor zones with `body_entered`/`body_exited` signals.

**Collision filtering**: 32-layer bitmask system. Each object has a `collision_layer` (what I am) and `collision_mask` (what I detect). Two objects collide if `(A.layer & B.mask) != 0 OR (B.layer & A.mask) != 0`. Bitwise AND operations make this nearly free.

**Spatial queries**: `PhysicsDirectSpaceState2D` provides `intersect_ray()`, `intersect_point()`, `intersect_shape()`, `cast_motion()`, and `collide_shape()`. Must be called within `_physics_process()`.

### Unity 2D

Unity's 2D physics is built on Box2D.

**Shapes**: BoxCollider2D, CircleCollider2D, CapsuleCollider2D, PolygonCollider2D (auto-decomposed into convex sub-polygons, max 8 vertices each), EdgeCollider2D (line chain), CompositeCollider2D (merges child colliders), TilemapCollider2D.

**Broad phase**: Dynamic AABB Tree (Box2D's `b2DynamicTree`). Uses "fat" AABBs inflated by a margin factor — small movements don't trigger tree updates. Surface area heuristic (SAH) guides insertion. O(k log n) query performance.

**Narrow phase**: SAT for polygon-polygon. Analytical fast paths for circle-circle, circle-polygon. GJK for distance queries and Time of Impact (TOI) calculations. Contact manifolds with up to 2 points per pair.

**API model**: Component-based. `Collider2D` + `Rigidbody2D` (Dynamic/Kinematic/Static). Trigger mode (`isTrigger = true`) provides sensor behavior without physical response. Callbacks: `OnCollisionEnter2D`/`OnTriggerEnter2D`, `OnCollisionStay2D`/`OnTriggerStay2D`, `OnCollisionExit2D`/`OnTriggerExit2D`.

**Collision filtering**: Layer collision matrix (32 layers, grid of checkboxes). Evaluated before any geometry tests. `ContactFilter2D` provides per-query filtering by layer, depth range, normal angle.

**Spatial queries**: `Physics2D.Raycast`, `Physics2D.OverlapCircle`, `Physics2D.OverlapBox`, `Physics2D.OverlapCapsule`, `Physics2D.OverlapPoint`, `Physics2D.BoxCast`, `Physics2D.CircleCast`, `Physics2D.CapsuleCast`, `Physics2D.Linecast`. All accept layer masks and `ContactFilter2D`.

### GameMaker

GameMaker uses a fundamentally different approach: function-based collision API with no physics engine. The programmer handles all movement and response.

**Shapes**: Sprite-based collision masks. Types: Rectangle (fastest, AABB, does NOT rotate), Rotated Rectangle, Ellipse, Diamond, Precise (pixel-perfect), Precise Per Frame. Mixed mask types fall back to precise checking.

**Broad phase**: R-Tree for spatial indexing. Each instance has a `collision_space` variable for coarse filtering. Tile collisions bypass the R-Tree entirely — they use O(1) grid lookups and are significantly faster than instance collisions.

**Narrow phase**: Bounding box or pixel-precise mask comparison. No SAT or GJK — shapes are either axis-aligned boxes or pixel masks.

**API model**: Function calls, not components:
```gml
// Boolean check: would I collide at this position?
if (place_meeting(x + hspd, y, obj_wall)) {
    while (!place_meeting(x + sign(hspd), y, obj_wall))
        x += sign(hspd);
    hspd = 0;
}
x += hspd;

// Shape queries (independent of calling instance's mask):
collision_circle(x, y, radius, obj, prec, notme)
collision_rectangle(x1, y1, x2, y2, obj, prec, notme)
collision_line(x1, y1, x2, y2, obj, prec, notme)  // raycast equivalent

// Geometry-only (no instances):
point_in_rectangle(px, py, x1, y1, x2, y2)
point_in_circle(px, py, cx, cy, r)
```

**Key design insight**: The `filter` parameter pattern (specifying `prec` and `notme` per call) gives fine-grained control without layer setup. The `_list` variants return all collisions rather than just the first. `instance_place` returns the colliding instance ID while `place_meeting` returns only a boolean — both are useful at different times.

### Love2D ecosystem

Love2D's built-in `love.physics` wraps Box2D, similar to this engine. More interesting are the community collision libraries:

**bump.lua** — AABB-only grid-based collision:
```lua
local world = bump.newWorld(64)          -- cell size = 64
world:add(item, x, y, w, h)             -- register
local ax, ay, cols, len = world:move(item, goalX, goalY, filter)
-- filter returns "slide", "cross", "touch", "bounce", or nil
```
Spatial hash grid: objects stored in every cell their AABB overlaps. O(1) neighbor lookup. The `filter` function provides collision filtering and response type selection in a single callback. Anti-tunneling: traces the full movement path rather than testing only the final position. **Limitation**: AABB only — no circles, polygons, or rotation.

**HC (Hardon Collider)** — Polygon/circle collision with GJK+EPA:
```lua
local rect = HC.rectangle(x, y, w, h)
local circle = HC.circle(cx, cy, r)
local poly = HC.polygon(x1,y1, x2,y2, x3,y3, ...)
for other, sep in pairs(HC.collisions(shape)) do
    shape:move(-sep.x, -sep.y)  -- sep is the MTV
end
```
Uses GJK for overlap detection, EPA for MTV computation. Spatial hash broad phase (cell size 100). Supports arbitrary convex/concave polygons and circles. Returns MTV directly — no built-in response types (you implement your own).

**windfield** — Convenience wrapper over love.physics/Box2D adding collision classes:
```lua
world:addCollisionClass('Player')
world:addCollisionClass('Ghost', {ignores={'Solid'}})
collider:enter('ClassName')  -- event query
```

### cute_c2.h (Randy Gaul)

A header-only C narrow-phase collision library (~4000 lines, no dependencies). Strictly narrow-phase — no broad-phase acceleration structure included.

**Shapes**: `c2Circle` (center + radius), `c2AABB` (min/max corners), `c2Capsule` (segment + radius), `c2Poly` (convex polygon, max 8 vertices with precomputed normals), `c2Ray` (origin + direction + max distance).

**Collision tests**: Boolean overlap for all 10 shape-pair combinations. Generic dispatch via `c2Collided(a, ax, aType, b, bx, bType)`. Manifold generation (up to 2 contact points with depths and normal) for all pairs via `c2Collide()`.

**Raycasting**: `c2CastRay(ray, shape, transform, type, &result)` for all shape types. Returns time of impact and surface normal.

**GJK**: `c2GJK()` computes closest distance and closest points between two disjoint shapes. Supports warm-starting via `c2GJKCache`.

**TOI (Time of Impact)**: `c2TOI()` for continuous collision detection between two moving shapes. Uses GJK-Raycast (Erin Catto's algorithm).

**Transform instancing**: Shapes defined in local space, instanced at world positions via `c2x` (position + rotation as cos/sin pair). One polygon definition can be used at multiple positions without duplicating vertex data.

**Numerical note**: GJK is sensitive to shape magnitudes. Shapes should be kept between 1.0 and 10.0 units for numerical stability.

**Key insight for this engine**: cute_c2 is designed for exactly the use case we need — narrow-phase collision detection in a custom engine that provides its own broad phase. It could be vendored as a single header file.

### Chipmunk2D

Full 2D physics in C99, MIT licensed. Relevant broad-phase design:

**Two spatial indexes** (interchangeable):
1. AABB Tree (default): Bounding box hierarchy with temporal coherence. No tuning needed.
2. Spatial Hash: Better for thousands of uniformly-sized objects. Requires manual tuning of cell size (`dim`) and hash table size (`count`).

**Collision filtering pipeline** (cheapest first): bounding box overlap → category/mask bitmask → joint constraints → collision handler callbacks. Multi-stage rejection minimizes expensive narrow-phase tests.

### Summary matrix

| Feature | Godot | Unity | GameMaker | bump.lua | HC | cute_c2 |
|---|---|---|---|---|---|---|
| Broad phase | Dynamic BVH | Dynamic AABB Tree | R-Tree | Spatial hash grid | Spatial hash | None (you provide) |
| Narrow phase | SAT, GJK-EPA | SAT, GJK | AABB/pixel masks | AABB overlap | GJK+EPA | GJK+EPA, specific pairs |
| Shapes | 8 types | 8 types | Sprite masks | AABB only | Circle, polygon | Circle, AABB, capsule, polygon, ray |
| Filtering | 32-layer bitmask | Layer matrix (32) | Object parents, spaces | Filter callback | Manual | None (you provide) |
| Response | Solver + signals | Solver + callbacks | Manual | slide/cross/touch/bounce | MTV | Manifold |
| Spatial queries | Ray, point, shape, motion | Ray, overlap, cast, line | Collision functions | queryRect, queryPoint, querySegment | collisions() | Ray, GJK distance |
| Physics required? | Optional (Area2D) | Optional (trigger) | No | No | No | No |

## Algorithm primer

This section explains the core collision detection algorithms at an intuitive level. The later sections in this document reference these algorithms in the context of specific design decisions; this primer provides the conceptual foundation.

### The two-phase pipeline: broad phase and narrow phase

Collision detection in games is split into two stages to avoid the O(n²) problem of testing every object against every other object:

1. **Broad phase**: Quickly eliminates pairs that *cannot possibly* collide using cheap approximations (usually axis-aligned bounding boxes). A scene with 1000 objects has ~500,000 possible pairs, but the broad phase might reduce that to a few hundred candidate pairs.

2. **Narrow phase**: Tests the actual geometry of each candidate pair to determine if they truly overlap, and if so, computes the collision normal, penetration depth, and contact point.

The broad phase trades accuracy for speed (false positives are OK, false negatives are not). The narrow phase is exact but expensive, so it only runs on the small set of candidates that survived the broad phase.

```
All objects (n)
    │
    ▼
┌──────────────────┐
│   Broad Phase    │  Spatial hash, BVH, quadtree, etc.
│  (AABB overlap)  │  Fast, approximate — O(n) to O(n log n)
└──────────────────┘
    │
    ▼
Candidate pairs (k << n²)
    │
    ▼
┌──────────────────┐
│  Narrow Phase    │  SAT, GJK+EPA, specific pair tests
│ (exact geometry) │  Precise — runs only on k pairs
└──────────────────┘
    │
    ▼
Collision results (normal, depth, contact point)
```

### Axis-Aligned Bounding Boxes (AABBs)

An AABB is the tightest rectangle that encloses a shape while keeping its edges parallel to the coordinate axes. It is defined by just two points: `(min_x, min_y)` and `(max_x, max_y)`.

```
       min
        ┌───────┐
        │  ╱╲   │
        │ ╱  ╲  │
        │╱    ╲ │
        │╲    ╱ │
        │ ╲  ╱  │
        │  ╲╱   │
        └───────┘
	            max

  The diamond is the actual shape.
  The rectangle is its AABB.
```

Two AABBs overlap if and only if they overlap on *both* axes — four comparisons total:

```
overlap = (a.max_x > b.min_x) && (a.min_x < b.max_x)
       && (a.max_y > b.min_y) && (a.min_y < b.max_y)
```

This is the cheapest possible 2D overlap test, which is why every broad phase structure uses AABBs as its fundamental primitive. The downside is that AABBs are a loose fit for non-rectangular or rotated shapes, producing false positives that the narrow phase must reject.

### Spatial hash grid

A spatial hash grid divides the world into a uniform grid of square cells (e.g., 64×64 pixels each). Each object is inserted into every cell that its AABB overlaps. To find collision candidates for an object, you look up which cells it occupies and collect all other objects in those same cells.

```
    0     1     2     3
  ┌─────┬─────┬─────┬─────┐
0 │     │     │     │     │
  │     │  A  │  A  │     │
  ├─────┼─────┼─────┼─────┤
1 │     │  A  │  A,B│  B  │
  │     │     │     │     │
  ├─────┼─────┼─────┼─────┤
2 │  C  │     │  B  │  B  │
  │     │     │     │     │
  ├─────┼─────┼─────┼─────┤
3 │     │     │     │     │
  │     │     │     │     │
  └─────┴─────┴─────┴─────┘

  A and B share cell (1,2), so they're a candidate pair.
  C shares no cells with A or B, so it's immediately ruled out.
```

The "hash" part means you don't actually allocate a 2D array for the whole world. Instead, cell coordinates are hashed to a fixed-size table: `bucket = hash(cell_x, cell_y) % table_size`. This gives infinite world support — any coordinates work, even negative ones.

**Choosing cell size**: The cell size should be roughly 2× the radius of the most common object. Too small and objects span many cells (wasting insertion time). Too large and cells contain too many objects (defeating the purpose of spatial partitioning).

**Why it works well for games**: Most 2D games have objects of roughly similar size (characters, bullets, tiles). The grid is rebuilt from scratch every frame, which sounds expensive but is actually very fast — it's a single O(n) pass with excellent cache locality (iterating flat arrays). The simplicity also means fewer bugs.

**When it breaks down**: If your game has both tiny bullets and enormous boss monsters, the fixed cell size is a poor fit. Either the cells are too small for the boss (it spans hundreds of cells) or too large for the bullets (too many bullets per cell). In this case, a dynamic AABB tree handles mixed sizes better.

**Further reading**:
- [Spatial Hashing (GameDev.net)](https://www.gamedev.net/tutorials/programming/general-and-gameplay-programming/spatial-hashing-r2697/) — good introduction to the concept
- [Red Blob Games: Spatial Hash](https://www.redblobgames.com/x/1730-spatial-hash/) — interactive browser demo by Amit Patel
- [Metanet Software: Broad-Phase Collision](https://www.metanetsoftware.com/2016/n-tutorial-b-broad-phase-collision) — practical grid-based broad phase from the developers of N++

### Dynamic AABB tree (Bounding Volume Hierarchy)

A dynamic AABB tree is a binary tree where each leaf node holds one object's AABB, and each internal node holds the union AABB of its two children. To query "what might overlap this region?", you walk the tree from the root: if the query region doesn't overlap a node's AABB, you skip its entire subtree.

```
            ┌───────────────────┐
            │     Root AABB     │
            │ (covers everything) │
            └─────────┬─────────┘
                 ╱         ╲
        ┌───────┴──┐    ┌──┴───────┐
        │  Left    │    │  Right   │
        │  subtree │    │  subtree │
        └────┬─────┘    └────┬─────┘
           ╱   ╲           ╱   ╲
         [A]   [B]       [C]   [D]    ← leaf nodes (actual objects)
```

If your query rectangle doesn't overlap "Right subtree," you skip C and D entirely — testing 1 AABB instead of 2. In a balanced tree with 1000 objects, a query typically touches ~20-30 nodes instead of all 1000.

**"Fat" AABBs**: When an object moves slightly, you don't want to update the tree every frame. Each leaf's AABB is inflated ("fattened") by a small margin. As long as the object stays within its fat AABB, no tree update is needed. Only when it exits does a remove-and-reinsert happen. This exploits temporal coherence — most objects move incrementally between frames.

**Surface Area Heuristic (SAH)**: When inserting a new leaf, the tree walks down and picks the child that minimizes the total surface area increase. This keeps the tree well-structured so that queries prune large portions of the tree. Box2D also applies tree rotations to maintain balance.

**Why you might want this over a spatial hash**: Dynamic AABB trees handle variable-sized objects naturally, are excellent for raycasting (the tree prunes entire regions), and don't require tuning a cell size parameter. The trade-off is significantly more implementation complexity (~500+ lines vs. ~200 for a spatial hash).

**Further reading**:
- [Erin Catto: Dynamic Bounding Volume Hierarchies (GDC 2019)](https://box2d.org/files/ErinCatto_DynamicBVH_GDC2019.pdf) — the definitive practical resource, from the creator of Box2D
- [Allen Chou: Dynamic AABB Tree](https://allenchou.net/2014/02/game-physics-broadphase-dynamic-aabb-tree/) — blog post with clear explanations and code

### SAT (Separating Axis Theorem)

The Separating Axis Theorem states: **two convex shapes do not overlap if and only if there exists an axis along which their projections (shadows) don't overlap.** If no such separating axis exists, the shapes must be intersecting.

```
    Axis ────────────────────────────►

    Shape A projects to:    [========]
    Shape B projects to:                [========]
                                 gap!
    → Not colliding (this axis separates them)


    Axis ────────────────────────────►

    Shape A projects to:    [============]
    Shape B projects to:         [============]
                                 ◄──overlap──►
    → Overlapping on this axis (but must check ALL axes)
```

The key insight is that you don't need to test *every possible* axis — only the edge normals of both polygons. For two rectangles, that's just 4 axes (2 per rectangle, since parallel edges share normals). For two arbitrary convex polygons with m and n edges, it's m + n axes.

**Finding the collision response (MTV)**: If the shapes overlap on *all* tested axes, the axis with the *smallest* overlap gives the **Minimum Translation Vector** — the shortest push needed to separate the shapes. This is exactly what you need for collision resolution.

**Why SAT is popular for 2D games**: It's straightforward to implement, fast for low-vertex-count polygons (rectangles, hexagons), and directly produces the MTV you need for physics response. It's "optimistic about separation" — it exits early as soon as any separating axis is found, so non-colliding pairs are very cheap.

**Limitation**: SAT only works for convex shapes. Concave shapes must be decomposed into convex sub-polygons first. For shapes with many vertices, the number of axes grows linearly, making SAT slower than GJK.

**Further reading**:
- [dyn4j: SAT (Separating Axis Theorem)](https://dyn4j.org/2010/01/sat/) — the best written SAT tutorial online, with diagrams and step-by-step examples
- [Metanet Software: Collision Detection and Response](https://www.metanetsoftware.com/2016/n-tutorial-a-collision-detection-and-response) — SAT explained in the context of a real shipped game (N++)

### Minkowski difference (the idea behind GJK)

Before understanding GJK, you need the Minkowski difference. Given two shapes A and B, the Minkowski difference `A ⊖ B` is a new shape formed by taking every point in A and subtracting every point in B:

```
A ⊖ B  =  { a - b  |  a ∈ A, b ∈ B }
```

The critical property: **A and B overlap if and only if the Minkowski difference contains the origin (0, 0).**

```
    Shape A          Shape B          A ⊖ B
    ┌───┐                             ┌───────────┐
    │   │            ┌──┐             │           │
    │   │            │  │             │     ●     │  ← origin is INSIDE
    └───┘            └──┘             │   (0,0)   │     → A and B overlap
                                      └───────────┘

    Shape A          Shape B          A ⊖ B
    ┌───┐                             ┌───────────┐
    │   │                 ┌──┐        │           │
    │   │                 │  │        │           │     ● (0,0) is OUTSIDE
    └───┘                 └──┘        └───────────┘       → A and B don't overlap
```

You could compute the full Minkowski difference and test if the origin is inside, but that's expensive (O(m × n) vertices). GJK's insight is that you can test whether the origin is inside *without ever building the full shape*.

**Further reading**:
- [hamaluik.ca: Simple AABB Collision Using Minkowski Difference](https://blog.hamaluik.ca/posts/simple-aabb-collision-using-minkowski-difference/) — interactive demo showing the concept visually
- [CSE 442: GJK Distance Algorithm (Interactive)](https://cse442-17f.github.io/Gilbert-Johnson-Keerthi-Distance-Algorithm/) — step-through animation of Minkowski sum construction

### GJK (Gilbert-Johnson-Keerthi)

GJK answers the question: **does the Minkowski difference of two convex shapes contain the origin?** It does this without computing the full Minkowski difference, using a clever iterative approach.

**Support functions**: Instead of enumerating all points, GJK uses a *support function* that returns the farthest point of a shape in a given direction. For the Minkowski difference `A ⊖ B`, the support function is:

```
support(direction) = farthest_point_of_A(direction) - farthest_point_of_B(-direction)
```

This gives you a single point on the boundary of the Minkowski difference, computed in O(n) time (just scan vertices for the max dot product). For a circle, it's O(1): `center + radius * normalize(direction)`.

**The algorithm**: GJK builds a *simplex* — a progressively larger shape (point → line → triangle in 2D) — trying to enclose the origin:

```
Step 1: Pick any direction, get a support       Step 2: Direction toward origin,
        point. Simplex = one point.                      get another support point.
                                                         Simplex = a line segment.
        ●  A                                          ● ─── ●
                     ● origin                                        ● origin


Step 3: Direction toward origin from              Step 4: Origin is inside the
        closest edge, get third point.                     triangle → COLLISION!
        Simplex = triangle.
              ●                                         ●
             ╱ ╲                                       ╱ ╲
            ╱   ╲        ● origin                     ╱ ● ╲
           ╱     ╲                                   ╱     ╲
          ● ───── ●                                 ● ───── ●
```

At each step, GJK checks: is the origin inside the current simplex? If yes → collision. If no → it determines which region of the simplex is closest to the origin, discards vertices that aren't helping, picks a new search direction toward the origin, and gets a new support point. If the new support point doesn't pass the origin (dot product with search direction is negative), the shapes are provably disjoint.

**GJK converges in very few iterations** — typically 2-4 for 2D shapes. It works with *any* convex shape that has a support function, including circles, capsules, and rounded shapes that SAT can't handle elegantly.

**What GJK does NOT give you**: GJK only answers "do they overlap?" (boolean) and, for disjoint shapes, "how far apart are they?". It does **not** tell you the penetration depth or collision normal for overlapping shapes. For that, you need EPA.

**Further reading**:
- [Casey Muratori: Implementing GJK](https://caseymuratori.com/blog_0003) — widely considered the best intuitive explanation; praised in *Game Engine Architecture* by Jason Gregory
- [Reducible: "A Strange But Elegant Approach to a Surprisingly Hard Problem" (YouTube)](https://www.youtube.com/watch?v=ajv46BSqcK4) — high-quality educational video with excellent visualizations
- [dyn4j: GJK (Gilbert-Johnson-Keerthi)](https://dyn4j.org/2010/04/gjk-gilbert-johnson-keerthi/) — thorough written tutorial with diagrams
- [winter.dev: GJK Algorithm in 2D/3D](https://winter.dev/articles/gjk-algorithm) — clean, well-illustrated article
- [Erin Catto: Computing Distance (GDC 2010)](https://box2d.org/files/ErinCatto_GJK_GDC2010.pdf) — GDC slides from the Box2D creator, focused on using GJK for distance computation

### EPA (Expanding Polytope Algorithm)

EPA picks up where GJK leaves off. When GJK determines that two shapes overlap (the simplex contains the origin), EPA takes that final simplex and expands it to find the **penetration depth** and **collision normal** — together called the Minimum Translation Vector (MTV).

**The idea**: GJK's final simplex (a triangle in 2D) is inscribed inside the Minkowski difference. EPA expands this polytope outward toward the true boundary of the Minkowski difference, one vertex at a time. The closest edge of the polytope to the origin tells you the penetration direction and depth.

```
Step 1: Start with GJK's          Step 2: Find closest edge        Step 3: Get support point
        final triangle                     to origin                        along that edge's
                                                                            normal, insert it
        ●                                 ●                                ●
       ╱ ╲                               ╱ ╲                              ╱ ╲
      ╱ ● ╲  ← origin                  ╱ ● ╲  ← this edge              ╱ ● ╲
     ╱     ╲                           ╱ ↑   ╲    is closest           ╱  ↑  ╲
    ● ───── ●                         ● ─┼─── ●                      ● ──┼── ●
                                         │                                │
                                      closest                          ● new vertex
                                      edge                         (on Minkowski boundary)

Step 4: Repeat until the new support point doesn't extend
        beyond the closest edge (within tolerance).
        The closest edge's normal = collision normal.
        The closest edge's distance to origin = penetration depth.
```

Each iteration adds one vertex to the polytope, making it a better approximation of the Minkowski difference boundary near the origin. The algorithm converges when the support point in the closest-edge-normal direction doesn't extend the polytope further (within a tolerance like 0.0001).

**In practice**: EPA typically converges in 2-6 iterations for 2D shapes. The combined GJK+EPA pipeline is the standard approach for general convex collision detection in modern engines and physics libraries.

**Further reading**:
- [dyn4j: EPA (Expanding Polytope Algorithm)](https://dyn4j.org/2010/05/epa-expanding-polytope-algorithm/) — companion to the dyn4j GJK tutorial, with step-by-step diagrams
- [winter.dev: EPA Collision Response for 2D/3D](https://winter.dev/articles/epa-algorithm) — follows naturally from their GJK article
- [Allen Chou: Contact Generation — EPA](https://allenchou.net/2013/12/game-physics-contact-generation-epa/) — EPA as part of a coherent game physics series

### Specific shape-pair tests

For common primitive shapes, hand-written analytical tests are faster than general algorithms (SAT, GJK) because they exploit the shape's mathematical properties directly:

- **Circle vs. Circle**: Compute the distance between centers. If it's less than the sum of radii, they overlap. Use squared distances to avoid a square root for the boolean test.
- **AABB vs. AABB**: Four comparisons on min/max coordinates. The overlap axis with the smallest penetration gives the MTV.
- **Circle vs. AABB**: Clamp the circle's center to the AABB's bounds to find the closest point on the AABB. Test the distance from that point to the circle center against the radius.
- **Ray vs. AABB (slab method)**: For each axis, compute the t-values where the ray enters and exits the AABB's slab. The ray hits if `max(all entry t's) < min(all exit t's)`.

These fast paths handle the most common cases in typical 2D games (hitboxes are usually circles or rectangles). General algorithms like SAT or GJK serve as fallbacks for less common shape combinations (arbitrary polygons, capsules).

### How they all fit together

A typical collision detection system combines these algorithms in a pipeline:

```
1. Every object gets an AABB (cheap bounding box around its actual shape)

2. Broad phase (spatial hash or AABB tree):
   "Which AABBs overlap?" → candidate pairs

3. Collision filtering (category/mask bitmask):
   "Should these two objects even interact?" → filtered pairs

4. Narrow phase (per candidate pair):
   - Circle vs Circle?  → analytical distance test
   - AABB vs AABB?      → min/max overlap test
   - Polygon vs Polygon? → SAT
   - Capsule vs anything? → GJK + EPA
   → collision normal, depth, contact point

5. Response:
   - Slide along surface (platformer movement)
   - Bounce (physics)
   - Take damage (gameplay trigger)
   - etc.
```

Steps 2-3 reject the vast majority of pairs cheaply. Step 4 only runs on the handful of pairs that survived. This is how games with hundreds or thousands of objects maintain 60 FPS.

### Comprehensive reference collections

These resources cover multiple algorithms in one place:

- [Allen Chou: Game Physics Series](https://allenchou.net/game-physics-series/) — complete blog series covering broad phase, GJK, EPA, and dynamics
- [dyn4j.org tutorials](https://dyn4j.org/tags.html) — SAT, GJK, EPA, contact clipping, all with diagrams
- [Box2D publications](https://box2d.org/publications/) — all of Erin Catto's GDC presentations (GJK, continuous collision, dynamic BVH, constraints)
- [Randy Gaul: Collision Detection in 2D](https://randygaul.github.io/collision-detection/2019/06/19/Collision-Detection-in-2D-Some-Steps-for-Success.html) — opinionated guide on structuring a collision system, from the author of cute_c2.h
- [Toptal: Video Game Physics Part II](https://www.toptal.com/game/video-game-physics-part-ii-collision-detection-for-solid-objects) — comprehensive article covering the full pipeline from broad to narrow phase

## Broad phase algorithms

### Quadtree

Recursively subdivides 2D space into four quadrants. Each node holds up to a threshold of objects (8-16); exceeding this triggers a split (up to a max depth of 6-8 levels). Objects spanning multiple quadrants stay in the parent node.

**Pros**: Adapts to non-uniform density; handles variable-sized objects naturally; good for range queries.
**Cons**: Boundary objects create hotspots in parent nodes; pointer-chasing is cache-unfriendly; more complex than spatial hashing.

**Rebuild vs. update**: Full rebuild each frame is simple but wasteful (OK for <500 objects). Incremental update (track which node owns each object, remove+reinsert on move) is faster with temporal coherence. Hybrid: rebuild periodically, incremental between.

### Spatial hash grid

Uniform grid of cells. Hash function maps cell coordinates to buckets: `hash(cx, cy) = (cx * P1 ^ cy * P2) % tableSize`. Objects inserted into every cell their AABB overlaps. Two objects can only collide if they share a bucket.

**Pros**: Extremely simple; excellent cache performance (arrays/hash tables); O(1) per cell; very fast for uniform-sized objects; trivial to rebuild each frame.
**Cons**: Poor with wildly varying object sizes; fixed cell size is a trade-off; no adaptive subdivision.

**Cell size**: Slightly larger than the diameter of the most common object. Too small: objects span many cells. Too large: too many objects per cell. Rule of thumb: 2x average object radius.

### Sweep and prune (SAP)

Project every AABB onto one or more axes. Maintain sorted endpoint lists. Because objects move incrementally, the lists are nearly sorted — insertion sort runs in O(n). When endpoints swap, update pair tracking.

**Pros**: Excellent temporal coherence; near-linear in practice; low memory overhead.
**Cons**: Degrades to O(n^2) when objects cluster on one axis; adding/removing objects requires list maintenance; single-axis SAP misses cases.

### Dynamic AABB tree (BVH)

Binary tree where leaf nodes hold object AABBs and internal nodes hold the union AABB of children. Used by Box2D and Godot.

**Insertion**: Surface area heuristic (SAH) — descend to the sibling that minimizes total surface area increase, then refit ancestors.
**Fattened AABBs**: Leaf AABBs inflated ~5% so small movements need no tree update. Remove+reinsert only when object exits its fat AABB.
**Tree rotations**: Maintain balance by rotating subtrees to minimize height difference, guided by surface area.

**Pros**: Best general-purpose broadphase; handles any object size; no world bounds needed; excellent for raycasting and region queries.
**Cons**: Most complex to implement; pointer-based tree is less cache-friendly; insertion heuristic quality matters.

### Comparison

| Criterion | Quadtree | Spatial hash | SAP | Dynamic AABB tree |
|---|---|---|---|---|
| Best for | Variable density, mixed sizes | Uniform-size objects (particles, bullets) | High temporal coherence | General purpose |
| Time complexity | O(n log n) | O(n) | O(n) incremental | O(n log n) |
| Memory | Moderate | Low | Low | Moderate |
| Cache friendliness | Poor | Good | Good | Poor |
| World bounds needed? | Yes | No (hash-based) | No | No |
| Variable sizes | Good | Poor | Good | Good |
| Ray casting | Good | Moderate | Poor | Excellent |
| Implementation complexity | Moderate | Simple | Moderate | Complex |

## Narrow phase algorithms

### SAT (Separating Axis Theorem)

Two convex shapes do NOT intersect if and only if there exists a line along which their projections do not overlap. For two polygons with m and n vertices: test m+n axes (edge normals from both polygons). For each axis, project all vertices and check overlap. If overlap exists on all axes, the axis with minimum overlap gives the MTV.

**Circle handling**: Circle-circle needs only the center-to-center axis. Circle-polygon tests all polygon edge normals plus the closest-vertex-to-center axis.

**Performance**: "Optimistic" about no collision — exits as soon as any separating axis is found. For rectangles: only 4 axis tests (2 per rectangle, since parallel edges share normals). Temporal coherence: cache the last separating axis and test it first.

### GJK (Gilbert-Johnson-Keerthi)

Determines if the Minkowski difference of two convex shapes contains the origin. Uses support functions (farthest point in a direction) rather than computing the full Minkowski difference. Iteratively builds a simplex (point → line → triangle in 2D) trying to enclose the origin.

**Support function**: For a polygon, iterate vertices and return the one with maximum dot product. For a circle, return center + radius * normalized direction.

**vs SAT**: GJK naturally handles circles and curved shapes; works with any convex shape that has a support function; computes closest distance for disjoint shapes. But only outputs a boolean — needs EPA for penetration depth/normal.

### EPA (Expanding Polytope Algorithm)

Takes GJK's final simplex (which contains the origin) and expands it toward the Minkowski difference boundary. Each iteration: find the edge closest to the origin, get a support point in that edge's normal direction, and insert it into the polytope. Converges when the support point doesn't extend beyond the closest edge (within tolerance ~0.0001). The closest edge's normal and distance give the collision normal and penetration depth.

### Specific shape pair tests

Optimized analytical tests avoid the overhead of general algorithms:

- **Circle-Circle**: Distance between centers vs. sum of radii. Compare squared distances to avoid sqrt for the boolean test.
- **AABB-AABB**: Four comparisons on min/max coordinates. MTV along the axis with minimum overlap.
- **Circle-AABB**: Clamp circle center to AABB bounds → closest point. Test distance to closest point vs. radius. Edge case: center inside AABB requires finding the nearest edge.
- **Ray-AABB (slab method)**: For each axis, compute entry/exit t-values. Ray hits if max(all entries) < min(all exits) and exit > 0.
- **Ray-Circle**: Solve quadratic equation |origin + t*dir - center|^2 = r^2.
- **Segment-Segment**: Cross-product parametric intersection test.

### Contact manifold generation

For physics response (if needed), the clipping method (used by Box2D) generates 1-2 contact points in 2D:
1. Find the reference edge (most perpendicular to collision normal) and incident edge.
2. Clip the incident edge against the reference edge using Sutherland-Hodgman clipping.
3. Discard clipped points that are not penetrating.

## Proposed design

The collision system is a new module (`G.collision`) independent of the existing `G.physics` module. It provides shape-based collision detection, spatial queries, and collision response helpers — without physics simulation.

### Design goals

1. **No physics required**: Detection and response without gravity, solvers, or rigid body dynamics.
2. **Multiple shape types**: Circle, AABB, capsule, convex polygon. Ray as a query primitive.
3. **Broad phase acceleration**: Spatial hash grid for the initial implementation (simplest, fastest for typical 2D game object counts and sizes). Can add dynamic AABB tree later if needed.
4. **Collision filtering**: Category/mask bitmask system (16 bits each, matching Box2D/Godot convention).
5. **Spatial queries**: Raycast, point query, region query (AABB and circle), shape overlap query.
6. **Movement with collision**: `move_and_slide` style function that moves an object and resolves collisions.
7. **Trigger support**: Shapes that detect overlap but don't participate in movement resolution.
8. **Lua-first API**: All functionality exposed to Lua. C++ API exists but Lua is the primary consumer.

### Shape types

```cpp
enum class CollisionShapeType {
  Circle,    // center + radius
  AABB,      // min/max corners (axis-aligned, no rotation)
  Capsule,   // segment (a, b) + radius
  Polygon,   // convex polygon, max 8 vertices, CCW winding
};

struct CollisionShape {
  CollisionShapeType type;
  union {
    struct { FVec2 center; float radius; } circle;
    struct { FVec2 min; FVec2 max; } aabb;
    struct { FVec2 a; FVec2 b; float radius; } capsule;
    struct { FVec2 verts[8]; FVec2 normals[8]; int count; } polygon;
  };
};
```

### Collision world

```cpp
class CollisionWorld {
public:
  struct Handle {
    uint32_t id;          // Internal ID
    uintptr_t userdata;   // User-defined (Lua registry ref)
  };

  struct Filter {
    uint16_t category = 0x0001;  // What I am
    uint16_t mask     = 0xFFFF;  // What I detect
  };

  // Object management
  Handle Add(CollisionShape shape, FVec2 position, float angle,
             Filter filter, bool is_trigger, uintptr_t userdata);
  void Remove(Handle handle);
  void SetPosition(Handle handle, FVec2 position);
  void SetAngle(Handle handle, float angle);
  void SetShape(Handle handle, CollisionShape shape);
  void SetFilter(Handle handle, Filter filter);
  FVec2 GetPosition(Handle handle) const;
  float GetAngle(Handle handle) const;

  // Movement with collision resolution
  struct MoveResult {
    FVec2 position;           // Final position after resolution
    struct Contact {
      Handle other;           // What was hit
      FVec2 normal;           // Surface normal at contact
      FVec2 touch;            // Contact point
      float depth;            // Penetration depth
    };
    FixedArray<Contact, 8> contacts;
  };
  MoveResult MoveAndSlide(Handle handle, FVec2 velocity);
  MoveResult MoveAndCollide(Handle handle, FVec2 velocity);

  // Overlap queries
  struct OverlapResult {
    Handle handle;
    FVec2 normal;             // Separation normal
    float depth;              // Penetration depth
  };
  void QueryOverlaps(Handle handle, FixedArray<OverlapResult, 32>& out) const;

  // Spatial queries
  struct RaycastHit {
    Handle handle;
    FVec2 point;              // Hit position
    FVec2 normal;             // Surface normal at hit
    float t;                  // Distance along ray (0..maxDist)
  };
  bool Raycast(FVec2 origin, FVec2 direction, float maxDist,
               uint16_t mask, RaycastHit& out) const;
  void RaycastAll(FVec2 origin, FVec2 direction, float maxDist,
                  uint16_t mask, FixedArray<RaycastHit, 32>& out) const;

  void QueryPoint(FVec2 point, uint16_t mask,
                  FixedArray<Handle, 32>& out) const;
  void QueryRect(FVec2 min, FVec2 max, uint16_t mask,
                 FixedArray<Handle, 32>& out) const;
  void QueryCircle(FVec2 center, float radius, uint16_t mask,
                   FixedArray<Handle, 32>& out) const;

  // Trigger callbacks (set from Lua)
  using TriggerCallback = void(*)(Handle a, Handle b, void* ctx);
  void SetTriggerEnterCallback(TriggerCallback cb, void* ctx);
  void SetTriggerExitCallback(TriggerCallback cb, void* ctx);

  // Must be called each frame to update broad phase and detect trigger events
  void Update();

private:
  SpatialHashGrid broad_phase_;
  // Internal: narrow-phase tests, movement resolution, trigger tracking
};
```

### Broad phase: spatial hash grid

The initial implementation uses a spatial hash grid. Rationale:

1. **Simplicity**: ~200 lines of code vs. ~500+ for a dynamic AABB tree.
2. **Performance**: For typical 2D games (50-500 active colliders of roughly similar size), spatial hashing is the fastest approach with excellent cache behavior.
3. **Rebuild each frame**: The grid is cleared and rebuilt each `Update()`. This is simpler than incremental updates and fast enough for the target object counts.
4. **No world bounds**: Hash-based, so infinite world support comes for free.

```cpp
class SpatialHashGrid {
public:
  SpatialHashGrid(float cell_size = 64.0f, size_t table_size = 1024);

  void Clear();
  void Insert(uint32_t id, FVec2 aabb_min, FVec2 aabb_max);

  // Fills `out` with IDs sharing cells with the query region
  void Query(FVec2 aabb_min, FVec2 aabb_max,
             FixedArray<uint32_t, 64>& out) const;

  // Ray query: steps through cells along the ray
  void QueryRay(FVec2 origin, FVec2 direction, float max_dist,
                FixedArray<uint32_t, 64>& out) const;

private:
  float cell_size_;
  size_t table_size_;
  struct Cell { FixedArray<uint32_t, 16> ids; };
  std::vector<Cell> table_;

  size_t Hash(int cx, int cy) const;
};
```

Cell size should default to 64 pixels (roughly matching the engine's `kPixelsPerMeter = 60`). Can be configured at world creation.

### Narrow phase: specific pair tests + SAT

For the initial implementation, use optimized specific-pair tests for all primitive combinations, with SAT as the general polygon-polygon solver:

| Shape A | Shape B | Algorithm |
|---|---|---|
| Circle | Circle | Distance check |
| Circle | AABB | Closest point clamping |
| Circle | Capsule | Closest point on segment + distance |
| Circle | Polygon | SAT (edge normals + closest vertex axis) |
| AABB | AABB | Min/max overlap |
| AABB | Capsule | Decompose to segment-AABB + expand by radius |
| AABB | Polygon | SAT |
| Capsule | Capsule | Closest point between two segments + distance |
| Capsule | Polygon | GJK (or decompose to expanded polygon) |
| Polygon | Polygon | SAT |

All tests return a `CollisionResult`:

```cpp
struct CollisionResult {
  bool hit;
  FVec2 normal;    // Collision normal (A → B)
  float depth;     // Penetration depth
  FVec2 contact;   // Contact point (world space)
};
```

**Future**: If more shape types or curved shapes are needed, switch to GJK+EPA as the general solver (like cute_c2.h does). The specific-pair tests remain as fast paths.

### Collision filtering

16-bit category/mask system (matching Box2D and Godot convention):

```cpp
struct Filter {
  uint16_t category = 0x0001;
  uint16_t mask     = 0xFFFF;
};

bool ShouldCollide(Filter a, Filter b) {
  return (a.category & b.mask) != 0 && (b.category & a.mask) != 0;
}
```

The filter check happens between broad phase and narrow phase — after spatial candidates are found but before any geometry tests. This is a bitwise AND: essentially free.

### Movement resolution

Two movement styles, inspired by Godot's `CharacterBody2D` and GameMaker's `place_meeting` pattern:

**MoveAndSlide**: Moves the object by `velocity`, resolving collisions by sliding along surfaces. Iterates up to 4 times (for corner sliding):
1. Attempt to move by remaining velocity.
2. If collision, push out by MTV and subtract the velocity component along the collision normal.
3. Repeat with remaining velocity.
4. Return final position and all contacts.

**MoveAndCollide**: Moves until the first collision and stops. Returns the collision info. The caller decides how to respond (bounce, stop, etc.). This is the building block for custom movement.

Both functions query overlaps after each sub-step and use the narrow phase to compute exact MTV for resolution. Triggers are reported but do not affect movement.

### Trigger tracking

Triggers detect overlaps without physical response. The system tracks which trigger pairs are currently overlapping:

1. Each `Update()`, compute all overlapping pairs involving at least one trigger.
2. Compare against the previous frame's overlapping trigger pairs.
3. New pairs → call trigger enter callback.
4. Removed pairs → call trigger exit callback.
5. Store the current set for next frame.

The Lua API fires `on_trigger_enter(other)` and `on_trigger_exit(other)` on the collider's userdata object.

### Lua API

```lua
-- World creation
local world = G.collision.new_world()              -- default cell size
local world = G.collision.new_world(cell_size)      -- custom cell size

-- Shape constructors
local circle  = G.collision.circle(radius)
local aabb    = G.collision.aabb(width, height)
local capsule = G.collision.capsule(length, radius)
local polygon = G.collision.polygon(x1,y1, x2,y2, x3,y3, ...)

-- Adding objects
local handle = world:add(shape, x, y, {
  angle    = 0,            -- rotation in radians
  category = 0x0001,       -- collision layer
  mask     = 0xFFFF,       -- collision filter
  trigger  = false,        -- trigger mode
  userdata = entity,       -- associated object
})

-- Object manipulation
world:set_position(handle, x, y)
world:set_angle(handle, angle)
world:set_shape(handle, new_shape)
world:set_filter(handle, category, mask)
world:remove(handle)
local x, y = world:get_position(handle)
local angle = world:get_angle(handle)

-- Movement with collision
local x, y, contacts = world:move_and_slide(handle, vx, vy)
-- contacts is an array of {other=handle, nx=, ny=, depth=, tx=, ty=}

local x, y, contact = world:move_and_collide(handle, vx, vy)
-- contact is nil or {other=, nx=, ny=, depth=, tx=, ty=}

-- Overlap queries
local overlaps = world:get_overlaps(handle)
-- array of {other=handle, nx=, ny=, depth=}

-- Spatial queries
local hit = world:raycast(ox, oy, dx, dy, max_dist, mask)
-- nil or {handle=, x=, y=, nx=, ny=, t=}

local hits = world:raycast_all(ox, oy, dx, dy, max_dist, mask)
-- array of hit results, sorted by t

local handles = world:query_point(x, y, mask)
local handles = world:query_rect(x1, y1, x2, y2, mask)
local handles = world:query_circle(cx, cy, radius, mask)

-- Trigger callbacks
world:on_trigger_enter(function(a, b) ... end)
world:on_trigger_exit(function(a, b) ... end)

-- Update (call once per frame)
world:update()

-- Pure geometry tests (no world needed)
local hit, nx, ny, depth = G.collision.test(shape_a, ax, ay, aa,
                                            shape_b, bx, by, ba)
```

### Usage examples

**Platformer movement**:
```lua
function Player:update(dt)
  -- Apply gravity
  self.vy = self.vy + 980 * dt

  -- Move with collision
  local x, y, contacts = world:move_and_slide(self.handle,
                                               self.vx * dt, self.vy * dt)
  self.x, self.y = x, y

  -- Check if grounded
  self.grounded = false
  for _, c in ipairs(contacts) do
    if c.ny < -0.7 then  -- hit something above us (floor normal points up)
      self.grounded = true
      self.vy = 0
    end
  end
end
```

**Bullet hell overlap detection**:
```lua
function update_bullets(dt)
  for _, bullet in ipairs(bullets) do
    world:set_position(bullet.handle, bullet.x, bullet.y)
  end
  world:update()
  for _, bullet in ipairs(bullets) do
    local overlaps = world:get_overlaps(bullet.handle)
    for _, o in ipairs(overlaps) do
      local target = world:get_userdata(o.other)
      target:take_damage(bullet.damage)
      bullet:destroy()
      break
    end
  end
end
```

**Line of sight raycast**:
```lua
function can_see(enemy, player)
  local dx = player.x - enemy.x
  local dy = player.y - enemy.y
  local dist = math.sqrt(dx*dx + dy*dy)
  local hit = world:raycast(enemy.x, enemy.y, dx/dist, dy/dist,
                            dist, WALL_MASK)
  return hit == nil  -- no wall between enemy and player
end
```

**Trigger zone**:
```lua
local zone = world:add(G.collision.circle(200), 400, 300, {
  trigger = true,
  category = 0x0010,
  mask = 0x0001,  -- detect players only
})

world:on_trigger_enter(function(a, b)
  local entity = world:get_userdata(b)
  if entity.type == "player" then
    show_dialogue("Welcome to the village!")
  end
end)
```

## Memory allocation

The engine uses a single upfront `malloc(4 GiB)` as an `ArenaAllocator`, with sub-arenas and specialized allocators carved from it. No system `malloc`/`free` during gameplay. The collision system must follow this pattern exactly.

### Allocation landscape

| Subsystem | Allocator | Notes |
|---|---|---|
| Engine core | 4 GiB `ArenaAllocator` (bump allocator) | Dealloc is a no-op unless freeing the most recent allocation |
| Lua | 64 MiB `MimallocAllocator` (mimalloc heap on arena-backed memory) | General-purpose alloc/dealloc for GC churn |
| Per-frame scratch | 128 MiB `ArenaAllocator`, `Reset()` every frame | Vertex/index buffers, temporary arrays |
| Hot-reload scratch | 128 MiB `ArenaAllocator`, `Reset()` per reload check | Asset processing |
| Box2D | Main arena via `b2SetAllocator` (global function pointers) | All Box2D allocations flow through engine arena |
| SQLite | 16 MiB `memsys5` buddy allocator on arena-backed buffer | Separate CLI arena |
| Sound decoders | Inline 256 KiB `ArenaAllocator` per decoder + `FreeList<T>` for decoder objects | drwav/stb_vorbis callbacks wired to per-decoder arenas |

Key constraint: `ArenaAllocator::Dealloc` only reclaims memory if the freed block is the most recently allocated one (`p + size == pos_`). Otherwise it is a no-op — memory is effectively leaked until `Reset()`. This means the collision system cannot rely on arena dealloc for individual object removal.

### Strategy for the collision system

The collision world has two allocation patterns:

1. **Long-lived storage** (collider slots, shapes, trigger pair sets): Allocated once, modified in place, freed only on world destruction or hot-reload reset. The arena is fine for initial allocation, but individual `Remove()` calls cannot reclaim memory.

2. **Per-frame scratch** (broad phase grid, query results, contact arrays): Rebuilt every frame. Perfect for the frame allocator.

**Proposed approach**:

```
4 GiB engine arena
  |-- CollisionWorld (allocated once at world creation)
  |   |-- Slot map backing array: FixedArray<ColliderSlot, kMaxColliders>
  |   |   (allocated from engine arena, fixed capacity)
  |   |-- Free list for slot reuse: intrusive linked list within slot array
  |   |   (no allocation — uses a next-free index in each slot)
  |   |-- Trigger pair set: FixedArray<TriggerPair, kMaxTriggerPairs>
  |   |   (allocated from engine arena, fixed capacity)
  |
  |-- Per-frame (from frame_allocator, reset every frame):
      |-- SpatialHashGrid cell arrays (rebuilt each Update())
      |-- Query result scratch buffers
      |-- MoveAndSlide iteration buffers
```

### Collider slot map

The central data structure is a **slot map** — a fixed-capacity array of collider slots with an intrusive free list for O(1) add/remove without per-object heap allocation:

```cpp
struct ColliderSlot {
  // Active collider data (when in use)
  CollisionShape shape;
  FVec2 position;
  float angle;
  Filter filter;
  bool is_trigger;
  uintptr_t userdata;    // Lua registry ref
  uint32_t generation;   // Incremented on reuse to detect stale handles

  // Free list link (when not in use — overlaps shape/position via union or flag)
  uint32_t next_free;
  bool active;
};

class CollisionWorld {
  FixedArray<ColliderSlot, kMaxColliders> slots_;  // One arena allocation
  uint32_t first_free_;  // Head of intrusive free list
  uint32_t count_;       // Number of active colliders
};
```

`Add()` pops from the free list (O(1), no allocation). `Remove()` pushes onto the free list (O(1), no deallocation). The slot array is allocated once from the engine arena and never grows. The generation counter detects use-after-remove: handles carry `(index, generation)` and are validated before use.

This is the same pattern used by ECS systems and handle-based engines. It is fully compatible with the arena allocator because it never needs per-object alloc/dealloc.

**Capacity**: `kMaxColliders` should be 4096 initially (sizeof(ColliderSlot) is ~128 bytes, so 4096 slots = 512 KiB). This is configurable at world creation if needed.

### Spatial hash grid as per-frame data

The spatial hash grid is rebuilt every frame during `Update()`, making it a natural fit for the frame allocator:

```cpp
class SpatialHashGrid {
public:
  // Does not own memory — caller provides a scratch allocator
  void Rebuild(Allocator* frame_alloc, const ColliderSlot* slots,
               uint32_t count, float cell_size, size_t table_size);
  void Query(FVec2 aabb_min, FVec2 aabb_max,
             FixedArray<uint32_t, 64>& out) const;

private:
  float cell_size_;
  size_t table_size_;

  // Parallel arrays allocated from frame_alloc each Rebuild():
  uint32_t* cell_counts_;   // Number of entries per bucket
  uint32_t* cell_offsets_;  // Start offset in entries_ for each bucket
  uint32_t* entries_;       // Flat array of collider IDs, grouped by bucket
};
```

Using a flat parallel-array layout (counts → offsets → entries) instead of per-cell linked lists gives excellent cache performance and works naturally with the frame allocator since it is one contiguous allocation per array. The alternative of per-cell `FixedArray` would waste arena memory on unused cell capacity.

The rebuild each frame is: count insertions per bucket → prefix-sum for offsets → scatter IDs into entries. This is O(n) where n = number of colliders and is cache-friendly.

### Query results

Query result buffers (raycast hits, overlap results, etc.) are stack-allocated `FixedArray`s with small fixed capacities. These never touch the allocator:

```cpp
// In MoveAndSlide:
FixedArray<OverlapResult, 32> overlaps;  // Stack, no heap allocation
broad_phase_.Query(aabb_min, aabb_max, candidates);
```

For Lua-facing queries that return variable-length results, the results are pushed directly onto the Lua stack as tables, which goes through Lua's mimalloc allocator. No C++ heap allocation needed.

### Interaction with Box2D's allocator

The collision system is fully independent of Box2D. It does not call `b2SetAllocator` or interact with Box2D's global allocator state. The two systems coexist because:

- Box2D uses `b2SetAllocator` with the main arena → Physics module.
- The collision system takes an `Allocator*` constructor parameter → same main arena, but through its own path.
- No shared state, no contention.

### Summary

| Allocation | Source | Lifetime |
|---|---|---|
| `CollisionWorld` object | Engine arena | World creation → destruction |
| Slot map array | Engine arena | World creation → destruction |
| Trigger pair tracking | Engine arena | World creation → destruction |
| Spatial hash grid arrays | Frame allocator | One frame (reset per frame) |
| Query scratch buffers | Stack (FixedArray) | Function scope |
| Lua result tables | Lua's mimalloc | Lua GC managed |
| Shape constructors (Lua userdata) | Lua's mimalloc | Lua GC managed |

Zero system `malloc`/`free` calls during gameplay. All allocations go through the engine's `Allocator` interface or Lua's mimalloc heap (which is itself backed by arena memory).

## Hot code reloading

The engine's hot-reload model is "full re-init": the Lua VM persists, but `_Game.init()` is called again from scratch. The collision system must handle this correctly.

### Current reload sequence

```
1. Background thread detects file changes (rapidhash checksums)
2. Main loop sees PendingChanges()
3. e_->lua.ClearError()
4. e_->Reload()           // Stops all sounds, reloads assets from DB
5. e_->lua.LoadMain()     // Re-executes main.lua, replaces _Game
6. e_->lua.Init()         // Calls _Game.init()
7. e_->MarkChangesAsProcessed()
```

**What persists**: The Lua VM (`lua_State*`), all C++ engine objects (`Physics`, `Renderer`, `Sound`, etc.), all `luaL_ref` registry references. **What is reset**: `_Game` is replaced, `package.loaded` entries for changed scripts are cleared, `_Game.init()` runs again.

### The problem with Physics today

The current Physics/Box2D system has a known issue with hot reload: bodies created before reload are **never cleaned up**. When `init()` runs again and creates new bodies, the old bodies accumulate in the `b2World`. Collision callback `luaL_ref` values pointing to old Lua functions are leaked in the registry. This is a bug, not a feature.

### Strategy for the collision system

The collision world should be **fully owned by Lua** as a userdata object. This gives us clean hot-reload semantics for free:

```lua
-- In main.lua / init():
local world = G.collision.new_world(64)

-- Add colliders...
local player_handle = world:add(G.collision.circle(16), 100, 200, { ... })
```

**On hot reload**:

1. `_Game` is replaced with the new module's return value.
2. The old `_Game` table (and all its references) becomes garbage.
3. If the old `world` userdata is only referenced from the old `_Game` table and its closures, it becomes eligible for GC.
4. The world's `__gc` metamethod fires, which:
   - Unrefs all Lua registry references held by collider userdata values
   - Unrefs trigger callback registry references
   - Returns the slot map memory to the arena (though in practice, arena dealloc is a no-op for non-recent allocations — this is acceptable because the memory will be reclaimed when the arena itself is reset, or simply reused by the next world creation)

**This is the correct design**: the collision world's lifetime is tied to the Lua code that created it. When code is reloaded and `init()` creates a new world, the old world is garbage collected. No manual cleanup needed. No accumulation of stale state.

### Preventing the Physics bug

The collision system avoids the Physics system's hot-reload bug because:

| Aspect | Physics (Box2D) | Collision (proposed) |
|---|---|---|
| C++ object lifetime | `EngineModules` member — lives forever | Lua userdata — GC'd when unreferenced |
| Body/collider lifetime | Must be manually destroyed | Slot map — all slots freed when world is GC'd |
| Callback refs | Stored as raw `luaL_ref`, never unref'd on reload | Stored as `luaL_ref`, unref'd in `__gc` |
| State on reload | Accumulates old bodies | Old world is garbage collected entirely |

### Implementation of `__gc`

```cpp
int collision_world_gc(lua_State* state) {
  auto* world = CheckUserdata<CollisionWorld>(state, 1);

  // Unref all active collider userdata values
  for (uint32_t i = 0; i < world->capacity(); i++) {
    if (world->IsActive(i)) {
      luaL_unref(state, LUA_REGISTRYINDEX, world->GetLuaRef(i));
    }
  }

  // Unref trigger callbacks
  if (world->trigger_enter_ref() != LUA_NOREF)
    luaL_unref(state, LUA_REGISTRYINDEX, world->trigger_enter_ref());
  if (world->trigger_exit_ref() != LUA_NOREF)
    luaL_unref(state, LUA_REGISTRYINDEX, world->trigger_exit_ref());

  // Destructor (no arena memory is actually reclaimed — this is fine)
  world->~CollisionWorld();
  return 0;
}
```

The memory for the `CollisionWorld` itself and its slot map is allocated from Lua's mimalloc allocator (via `lua_newuserdata`), so it IS properly freed by the GC. The spatial hash grid uses the frame allocator (reset every frame), so it needs no cleanup.

### World as Lua userdata vs. engine member

An alternative design is making `CollisionWorld` a member of `EngineModules` (like Physics). This would require explicit `Clear()` or `Reset()` calls during reload. The Lua-userdata approach is strictly better because:

1. **Multiple worlds**: Games can create multiple collision worlds (e.g., one for gameplay, one for UI hit testing). An `EngineModules` member would be a singleton.
2. **Automatic cleanup**: GC handles lifecycle. No manual clear/reset code paths.
3. **No stale state**: Impossible to have leftover colliders from a previous reload.
4. **Matches the frame allocator pattern**: The spatial hash grid already uses the frame allocator. The world itself being Lua-managed completes the "no persistent C++ state" design.

The downside is that `CollisionWorld` must be allocated from Lua's mimalloc rather than the engine arena. Since `lua_newuserdata` goes through Lua's custom allocator (which is the 64 MiB `MimallocAllocator` backed by arena memory), this is still arena-backed memory — just managed by mimalloc for proper alloc/dealloc support. The slot map array (512 KiB for 4096 slots) is a single allocation from mimalloc, well within its capabilities.

### Userdata memory layout

The world userdata must embed a pointer to the frame allocator (for per-frame grid rebuilds) since the frame allocator is an `EngineModules` member:

```cpp
// In lua_collision.cc:
int new_world(lua_State* state) {
  float cell_size = luaL_optnumber(state, 1, 64.0f);

  // Allocate world as Lua userdata (goes through Lua's mimalloc)
  auto* world = NewUserdata<CollisionWorld>(state, cell_size,
      Registry<Allocator>::Retrieve(state));  // frame allocator passed via registry

  luaL_setmetatable(state, "collision_world");
  return 1;
}
```

The `Registry<Allocator>` pattern (already used for Physics, Sound, etc.) provides the frame allocator pointer to the Lua binding without the world needing to know about `EngineModules`.

### Handling mid-frame hot reload

Hot reload happens in the main loop between frames — after `Update()` and before the next `StartFrame()`. The frame allocator is reset at `StartFrame()`. Since the collision world's spatial hash grid is rebuilt at the start of each `Update()` call, there is no stale grid data to worry about. The sequence is:

```
Frame N:
  StartFrame()          // frame_allocator.Reset()
  _Game.update(dt)      // Lua calls world:update(), grid rebuilt from frame_alloc
  _Game.draw()
  EndFrame()

[Hot reload detected]:
  Reload()              // sounds stopped, assets reloaded
  LoadMain()            // new _Game created, old _Game unreferenced
  Init()                // _Game.init() — may create new collision world
                        // Old world becomes GC-eligible
                        // Old world's grid pointers are into frame_alloc memory
                        //   that will be reset next frame — no dangling pointer
                        //   issue because grid is rebuilt each Update()

Frame N+1:
  StartFrame()          // frame_allocator.Reset() — old grid memory reclaimed
  _Game.update(dt)      // new world:update() rebuilds grid fresh
  ...
```

No special handling needed. The design composes correctly with the existing reload sequence.

## Implementation plan

### Phase 1: Core collision detection

**Files**: `src/collision.h`, `src/collision.cc`

1. Shape type definitions (`CollisionShape`, `CollisionShapeType`)
2. All narrow-phase pair tests (circle-circle, circle-AABB, AABB-AABB, circle-capsule, circle-polygon, AABB-polygon, capsule-capsule, polygon-polygon) returning `CollisionResult`
3. SAT implementation for general convex polygon pairs
4. Unit tests for all pair combinations (edge cases: containment, tangent, near-miss)

### Phase 2: Broad phase

**Files**: `src/spatial_hash.h`, `src/spatial_hash.cc`

1. Spatial hash grid with configurable cell size
2. Insert, clear, query (AABB region), query (ray stepping through cells)
3. Unit tests: insertion, region query, ray query, hash distribution

### Phase 3: Collision world

**Files**: `src/collision_world.h`, `src/collision_world.cc`

1. `CollisionWorld` class with add/remove/update/query operations
2. Handle management (ID generation, slot map for O(1) lookup)
3. Collision filtering (category/mask)
4. Spatial queries: raycast, point query, region query
5. Trigger tracking (enter/exit detection across frames)

### Phase 4: Movement resolution

**Files**: extend `src/collision_world.cc`

1. `MoveAndSlide` with iterative MTV resolution (up to 4 iterations)
2. `MoveAndCollide` with single-step collision
3. Edge case handling: corners, steep slopes, crush scenarios

### Phase 5: Lua bindings

**Files**: `src/lua_collision.cc`

1. Shape constructors as Lua functions
2. World as Lua userdata with metatable methods
3. Handle as Lua userdata
4. Callback registration for triggers
5. Pure geometry test function (`G.collision.test`)

### Future work

- **Dynamic AABB tree**: Replace or augment spatial hash for games with extreme size variation or heavy raycasting. Box2D's `b2DynamicTree` could be extracted from the vendored library.
- **GJK+EPA**: Add as alternative narrow phase for rounded/complex shapes. Consider vendoring cute_c2.h as a single header dependency.
- **Continuous collision detection**: Time of impact calculation for fast-moving objects (bullets through thin walls). Use conservative advancement or bilateral bisection.
- **Tilemap collision**: O(1) grid-based collision for tile maps (like GameMaker), separate from the shape-based system.
- **Contact manifolds**: Full 2-point manifold generation for physics-quality contacts, if needed for stacking or resting contact scenarios.

## References

**Engines and libraries**:
- [Godot Physics Documentation](https://docs.godotengine.org/en/stable/tutorials/physics/physics_introduction.html)
- [Unity 2D Physics Documentation](https://docs.unity3d.com/Manual/Physics2DReference.html)
- [GameMaker Collisions Manual](https://manual.gamemaker.io/monthly/en/GameMaker_Language/GML_Reference/Movement_And_Collisions/Collisions/Collisions.htm)
- [bump.lua](https://github.com/kikito/bump.lua) — AABB grid-based collision for Lua
- [HC (Hardon Collider)](https://github.com/vrld/HC) — Polygon/circle SAT collision for Lua
- [cute_c2.h](https://github.com/RandyGaul/cute_headers/blob/master/cute_c2.h) — Header-only C collision library
- [Chipmunk2D](https://github.com/slembcke/Chipmunk2D) — Full 2D physics in C
- [Box2D](https://box2d.org/) — 2D physics engine (vendored in this project)

**Algorithms**:
- [Erin Catto, Dynamic BVH (GDC 2019)](https://box2d.org/files/ErinCatto_DynamicBVH_GDC2019.pdf)
- [Erin Catto, Continuous Collision (GDC 2013)](https://box2d.org/files/ErinCatto_ContinuousCollision_GDC2013.pdf)
- [dyn4j SAT Tutorial](https://dyn4j.org/2010/01/sat/)
- [dyn4j GJK Tutorial](https://dyn4j.org/2010/04/gjk-gilbert-johnson-keerthi/)
- [dyn4j EPA Tutorial](https://dyn4j.org/2010/05/epa-expanding-polytope-algorithm/)
- [dyn4j Contact Points Using Clipping](https://dyn4j.org/2011/11/contact-points-using-clipping/)
- [pvigier's Quadtree for Collision Detection](https://pvigier.github.io/2019/08/04/quadtree-collision-detection.html)
- [Allen Chou, Dynamic AABB Tree](https://allenchou.net/2014/02/game-physics-broadphase-dynamic-aabb-tree/)
