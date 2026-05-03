---
status: in-design
tags: [lua-api, tooling, llm, developer-experience]
---

# LLM Ergonomics

Making the engine easier for LLMs to use correctly. Motivated by real
experience: while building Space Garbage!, Claude repeatedly got directions,
forces, and coordinate conventions wrong, producing enemies that moved
backwards, shots that fired in the wrong direction, and physics that
behaved erratically.

## Problem

LLMs have well-documented weaknesses in spatial reasoning. Research shows
that accuracy drops significantly with increased spatial complexity, and
errors go beyond simple coordinate misidentification to include fundamental
misunderstandings of geometric relationships. In game development
specifically, this manifests as:

1. **Y-axis confusion**: Screen coordinates are Y-down (top=0), but LLMs
   default to Y-up from math training. "Move up" becomes `y += speed`
   instead of `y -= speed`.

2. **Coordinate frame mismatch**: `apply_force()` uses body-local
   coordinates, `apply_linear_impulse()` uses world coordinates. LLMs
   assume both use the same frame.

3. **Angle direction flip**: Graphics `rotate()` is clockwise-positive
   (screen space), but physics angles follow Box2D's counter-clockwise-
   positive convention. LLMs mix them freely.

4. **Color range inconsistency**: `set_color()` uses 0-255, `clear()` uses
   0-1 floats, particles use 0-1 floats. LLMs generalize from whichever
   they see first.

5. **Unit confusion**: Physics internally scales by pixels-per-meter (60:1).
   Density, force, and gravity values that "look right" in SI units produce
   wrong behavior at pixel scale.

6. **Implicit ordering**: `create_ground()` must be called before adding
   bodies, `set_gravity()` should be called before `create_ground()`. No
   enforcement or documentation of this.

7. **Shape definition ambiguity**: `add_box(tx, ty, bx, by)` uses
   top-left/bottom-right corners. LLMs often assume center+size or
   position+dimensions.

These are not bugs in the engine. They are API design choices that assume
spatial intuition the LLM does not have.

## Survey

### Research findings

Explicit mathematical representations (JSON coordinates, structured
formats) yield higher success rates than natural-language descriptions for
spatial tasks. Cartesian formats outperform topographic and prose variants.
This suggests that **structured, unambiguous API documentation** is more
valuable than explanatory prose.

Iterative refinement with validation feedback significantly reduces error
rates. When LLM output is detected as invalid, systems that automatically
refine the prompt with additional constraints and repeat up to five
iterations produce dramatically better results.

### OpenGame framework (2026)

The OpenGame agentic framework for web game creation introduces two
relevant concepts:

- **Template Skill**: A growing library of project skeletons that provide
  stable scaffolding, so the LLM starts from a known-good architecture
  rather than generating from scratch.

- **Debug Skill**: A living protocol of verified fixes that enables
  systematic repair of integration errors rather than ad-hoc patching.

Both are applicable to our engine: template scripts give the LLM correct
starting points, and a debug/validation protocol catches spatial errors
before they reach the player.

### Screenshot-based verification

Several projects use screenshot analysis as a feedback loop: run the game,
capture the screen, analyze with a vision model, and feed corrections back.
This is particularly powerful for spatial bugs (wrong position, wrong
direction) because they are visually obvious but hard to detect from code
alone.

## Design

The solution has four layers, from simplest to most ambitious:

### Layer 1: Direction constants and semantic helpers (Lua API)

Eliminate Y-down confusion mechanically. These are useful for humans too.

```lua
-- Direction constants (screen-space, Y-down)
G.math.UP    = G.math.v2(0, -1)
G.math.DOWN  = G.math.v2(0, 1)
G.math.LEFT  = G.math.v2(-1, 0)
G.math.RIGHT = G.math.v2(1, 0)

-- "Move body toward target at speed" — no angle/force math needed
G.physics.move_toward(handle, target_x, target_y, speed)

-- "Set body angle to face target" — handles atan2 and Y-down
G.physics.look_at(handle, target_x, target_y)

-- "Apply force in world direction" — avoids body-local confusion
G.physics.apply_force_world(handle, fx, fy)

-- angle_between that returns the angle from one point to another
-- (handles Y-down atan2 correctly)
G.math.angle_between(x1, y1, x2, y2) -> radians
```

These helpers don't replace the existing low-level API. They provide a
"pit of success" for the common cases that LLMs get wrong.

**Priority**: High. Small implementation, large impact. Every direction
constant prevents a class of bugs.

### Layer 2: Documentation in type stubs (game.lua)

The type stubs in `definitions/game.lua` are the primary context LLMs see
when writing scripts. They should encode spatial conventions explicitly.

Changes:

1. **Add a coordinate system header** at the top of `game.lua`:
   ```lua
   -- COORDINATE SYSTEM:
   --   Origin: top-left corner of the screen
   --   X axis: left (0) to right (width)
   --   Y axis: top (0) to bottom (height)
   --   Angles: radians, clockwise-positive in graphics,
   --           counter-clockwise-positive in physics
   --   Colors: G.graphics uses 0-255 RGBA
   --           G.graphics.clear() uses 0-1 RGBA floats
   --           Particle color ramps use 0-1 RGBA floats
   --   Physics: all positions in pixels, internally scaled by
   --            pixels_per_meter (default 60)
   ```

2. **Annotate force/impulse functions** with coordinate frame:
   ```lua
   ---@note BODY-LOCAL coordinates: (1,0) is "forward" relative to body rotation
   function G.physics.apply_force(handle, x, y) end

   ---@note WORLD coordinates: (1,0) is always screen-right regardless of body rotation
   function G.physics.apply_linear_impulse(handle, x, y) end
   ```

3. **Add "when to use" guidance** for force vs impulse vs velocity:
   ```lua
   -- MOVEMENT GUIDE:
   --   Instant teleport:     G.physics.set_position(h, x, y)
   --   Smooth direct control: G.physics.set_linear_velocity(h, vx, vy)
   --   Gradual acceleration:  G.physics.apply_force(h, fx, fy)      -- body-local
   --   Instant kick:          G.physics.apply_linear_impulse(h, ix, iy) -- world
   --   For enemies chasing player: use set_linear_velocity or move_toward
   --   For projectiles: use set_linear_velocity at spawn time
   --   For explosions: use apply_linear_impulse on nearby bodies
   ```

4. **Add worked examples** for common patterns:
   ```lua
   -- EXAMPLE: Enemy chasing player
   --   local px, py = G.physics.position(player)
   --   local ex, ey = G.physics.position(enemy)
   --   local dir = G.math.direction(ex, ey, px, py)  -- unit vector toward player
   --   G.physics.set_linear_velocity(enemy, dir.x * speed, dir.y * speed)
   --   -- To also face the player:
   --   local angle = G.math.angle(ex, ey, px, py)
   --   G.physics.set_angle(enemy, angle)
   ```

**Priority**: High. Zero runtime cost, directly in the context window.

### Layer 3: Validation harness (test infrastructure)

A script-level validation system that catches spatial errors at runtime.
This is the engine equivalent of OpenGame's "Debug Skill".

```lua
-- In test mode (--test flag), enable spatial assertions:
G.test.assert_moving_toward(handle, target_x, target_y)
G.test.assert_facing(handle, target_x, target_y, tolerance)
G.test.assert_in_bounds(handle, x1, y1, x2, y2)
G.test.assert_velocity_direction(handle, expected_angle, tolerance)

-- Automatic warnings (always active in debug builds):
-- "Body X has velocity pointing away from its facing direction"
-- "Force applied to body X is perpendicular to its facing direction"
-- "Body X moved off-screen"
```

The harness runs alongside the game and reports when spatial invariants
are violated, giving the LLM immediate feedback without requiring visual
inspection.

**Priority**: Medium. Requires new test API surface, but catches bugs the
LLM cannot self-diagnose from code alone.

### Layer 4: Screenshot feedback loop (agentic workflow)

The most ambitious layer: automatically capture screenshots, analyze them,
and feed corrections back to the LLM. This is the self-correcting loop
that research shows dramatically reduces spatial errors.

**Workflow**:
1. LLM writes/modifies game script
2. Engine runs for N frames with `--test` flag
3. Engine captures screenshot at key moments
4. Screenshot is analyzed (by the same LLM via vision, or by a dedicated
   check script)
5. If spatial errors are detected, corrections are fed back

**Implementation options**:

A. **Claude Code hook**: A `post-tool` hook that, after writing a Lua file,
   automatically runs `game run --test <scene> --frames 60 --screenshot`,
   reads the screenshot, and warns about visible issues. This is the
   lightest touch — it uses existing infrastructure.

B. **Dedicated test scene**: A `testharness.lua` that spawns entities at
   known positions, applies common movement patterns, and asserts they
   behave correctly. The LLM runs this after making physics changes.

C. **Visual regression**: Compare screenshots against reference images
   (already partially supported via the test input system). Flag pixel
   differences above a threshold.

**Priority**: Low initially, but high long-term value. Option A (Claude
Code hook) could be prototyped quickly. Option C requires the screenshot
diffing infrastructure from the test input system design doc.

## Implementation plan

### Phase 1: Constants and stubs (small, high impact)

- Add `G.math.UP/DOWN/LEFT/RIGHT` direction constants
- Add `G.math.angle_between(x1, y1, x2, y2)` if not already present
- Add coordinate system header to `definitions/game.lua`
- Annotate all force/impulse/velocity functions with coordinate frame
- Add movement guide and worked examples to stubs
- Update README physics section with conventions

### Phase 2: Semantic helpers (medium effort)

- Add `G.physics.move_toward(handle, tx, ty, speed)`
- Add `G.physics.look_at(handle, tx, ty)`
- Add `G.physics.apply_force_world(handle, fx, fy)`
- Add `G.physics.face_direction(handle, angle)` (graphics-convention angle)

### Phase 3: Validation harness (medium effort)

- Extend `G.test` with spatial assertions
- Add debug-mode warnings for common spatial mistakes
- Create `testharness.lua` with standard movement/direction tests

### Phase 4: Screenshot feedback (larger effort, depends on Phase 3)

- Add `--frames N --screenshot` flags to `game run`
- Prototype Claude Code hook for auto-screenshot analysis
- Build visual regression comparison

## Decisions

1. **Don't change existing API semantics.** The low-level API (`apply_force`
   body-local, `apply_linear_impulse` world) stays as-is. Changing it would
   break existing games. New helpers are additive.

2. **Direction constants are vec2, not numbers.** `G.math.UP` is
   `v2(0, -1)`, not `-1`. This makes them composable: `UP * speed` works.

3. **Documentation over enforcement.** For most conventions (color ranges,
   coordinate frames), clear documentation is better than runtime
   validation. Reserve runtime checks for things that are always wrong
   (body off-screen, zero-length velocity in move_toward).

4. **Stubs are the primary LLM interface.** The `definitions/game.lua` file
   is loaded into context automatically. Any convention that matters must be
   documented there, not just in README or CLAUDE.md.
