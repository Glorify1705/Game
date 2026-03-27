# Timer and Tween System

Delayed callbacks, repeating timers, tweens with easing, cooldowns, and spring
physics for game scripting.

## Glossary

**Timer**: A countdown that fires a callback after a specified duration. The
simplest building block: "do X after 2 seconds." Used for delayed spawns,
screen transitions, invincibility frames, dialogue pacing.

**Repeating timer**: A timer that re-arms itself after firing. "Do X every 0.5
seconds." Used for spawn waves, periodic effects, heartbeat logic.

**Tween (in-betweening)**: Smoothly interpolating a value from A to B over a
duration. Instead of snapping `player.x` from 100 to 300, a tween moves it
there over 0.5 seconds following a curve. The term comes from traditional
animation, where senior artists drew *key frames* and junior artists drew the
*in-between* frames.

**Easing function**: A curve that controls *how* a tween progresses. Linear
easing moves at constant speed. "Ease out quad" starts fast and decelerates
(like a ball rolling to a stop). "Ease in out cubic" accelerates then
decelerates (smooth start and stop). Robert Penner codified the standard set in
2001; virtually every game engine uses them.

**Cooldown**: A timer that fires only when both a time threshold AND a condition
are met. "The player can attack every 0.3 seconds, but only while the attack
button is pressed." More than a simple timer -- it gates on external state.

**Spring**: A physics-based animation primitive. Instead of following a
predetermined curve (like a tween), a spring simulates a damped oscillator:
it overshoots, bounces back, and settles. Springs feel more organic than tweens
and respond naturally to interrupted input. Used for UI elements, camera follow,
juice effects.

**Time scale**: A global multiplier on delta time. At 0.5, everything runs at
half speed (slow-motion). At 0.0, the game is paused. Some systems (UI, screen
transitions) need to ignore time scale and run in real time.

**Tag**: A string identifier for a timer. Allows cancellation by name rather
than by handle. Critical for hot-reload: when Lua code is reloaded, new timers
registered with the same tag automatically replace old ones.

## Engine survey

| Feature | Anchor | hump.timer (Love2D) | flux (Love2D) | DOTween (Unity) | Godot Tween | GSAP (JS) |
|---|---|---|---|---|---|---|
| **Delayed callback** | timer_after(delay, fn, tag) | after(delay, fn) -> handle | N/A | DOVirtual.DelayedCall | create_timer(s).timeout | gsap.delayedCall |
| **Repeating timer** | timer_every(delay, fn, times, tag) | every(delay, fn, count) -> handle | N/A | SetLoops(-1) | set_loops(0) | repeat:-1 |
| **Duration callback** | timer_for(delay, fn, after, tag) | during(delay, fn, after) -> handle | N/A | OnUpdate(fn) | tween_method(fn, 0, 1, dur) | onUpdate:fn |
| **Tween** | timer_tween(dur, target, source, ease, after, tag) | tween(dur, subject, target, method, after) | flux.to(obj, time, {x=v}) | DOTween.To(get,set,end,dur) | tween_property(obj, prop, val, dur) | gsap.to(obj, {x:v}) |
| **Easing** | method string arg | 30+ (Penner set) | 27 variants | 33 Ease enum values | 12 TRANS * 4 EASE = 48 | Named strings |
| **Cooldown** | timer_cooldown(delay, cond, fn, tag) | N/A | N/A | N/A | N/A | N/A |
| **Spring** | spring_pull(f) / spring_animate(x) | N/A | N/A | DOPunch/DOShake | TRANS_SPRING | N/A |
| **Cancellation** | timer_cancel(tag) | cancel(handle) | tween:stop() | Kill(id) / Kill(target) | kill() / stop() | kill() |
| **Tag / ID system** | String tags (auto-replace) | Opaque handle table | Per-object conflict resolution | Object or string ID | Bound to node | Per-target |
| **Chaining** | N/A | N/A | tween:after() -> chained | Sequence.Append/Join | Sequential by default, chain()/parallel() | Timeline position params |
| **Coroutine scripting** | N/A | script(fn(wait)) | N/A | N/A | N/A | N/A |
| **Time scale** | N/A (uses global dt) | N/A | N/A | SetUpdate(independent:true) | set_pause_mode | N/A |
| **Memory** | Lua tables, GC'd | Lua tables, handle set | Array + per-object hash | Pre-allocated pools | Engine-managed | N/A |
| **GC pressure** | Medium (tables per timer) | Low (handle tables) | Low (flat struct) | None (pooled C#) | None (C++ engine) | N/A (JS GC) |

### Key takeaways

1. **Anchor** is the most complete Lua-native timer library. The `cooldown`
   type (time + condition gating) is unique and extremely useful for gameplay.
   String tags with auto-replacement are perfect for hot-reload. The `every_step`
   variant (delay changes over iterations) is creative but rarely needed.
   Springs are a separate mixin with simple Euler integration.

2. **hump.timer** is the cleanest minimal implementation. The `during(delay,
   fn, after)` pattern (call fn every frame for N seconds, then call after) is
   elegant for screen fades and damage flashes. The `script(fn(wait))`
   coroutine interface is the most readable way to write linear sequences.

3. **flux** focuses purely on tweening. Its key insight is **conflict
   resolution**: when a new tween starts on the same object property, it
   automatically cancels the old one. This prevents "tween fighting" which is
   a common bug. Deferred init (start values captured at tween start, not
   creation) is important for chained tweens.

4. **DOTween** demonstrates industrial-strength design. The Sequence API
   (Append/Join/Insert for sequential/parallel/absolute-time composition) is
   the most powerful chaining system. Per-tween `timeScale` and
   `isIndependentUpdate` for pause-immune tweens are essential features.
   Pre-allocated pools eliminate GC pressure entirely.

5. **Godot Tween** defaults to sequential execution (each step after the
   previous), with `parallel()` opt-in. The `tween_method` approach (call a
   function with an interpolated value) is more flexible than property-only
   tweening.

6. **GSAP** has the most expressive timeline positioning (`"<"`, `">-0.5"`,
   labels). Overkill for game timers but influential for sequence design.

## Current engine state

From `clock.h`:
```cpp
double NowInSeconds();
inline constexpr double TimeStepInSeconds() { return 1.0 / 60.0; }
```

The main loop (`game.cc`) runs a fixed timestep of 1/60s. Lua receives `t`
(total game time) and `dt` (fixed step) in `update(t, dt)`. There is no time
scale -- `dt` is always `1/60`.

Currently, timers and tweens require manual Lua code:

```lua
-- Manual delayed callback
local delay = 2.0
local delay_timer = 0
function g.update(t, dt)
  delay_timer = delay_timer + dt
  if delay_timer >= delay then
    do_something()
    delay_timer = -999  -- prevent re-trigger
  end
end

-- Manual tween
local tween_start = 100
local tween_end = 300
local tween_dur = 0.5
local tween_t = 0
function g.update(t, dt)
  tween_t = math.min(tween_t + dt / tween_dur, 1.0)
  player.x = tween_start + (tween_end - tween_start) * tween_t
end
```

This is the biggest productivity gap identified in [[Engine comparison]]:
every game needs timers and tweens, and hand-rolling them is tedious and
error-prone.

## Proposed design

### Where to implement

**Hybrid: C++ core with Lua-friendly API.** The timer/tween update loop and
easing functions live in C++. Lua callbacks are stored as registry refs. This
gives us:

- No per-timer Lua table allocation (reduces GC pressure)
- Easing functions computed in C++ (fast, no Lua call overhead per frame)
- Tag-based management in C++ (survives partial reloads, enables batch cancel)
- Lua only touches the system at creation and callback time

The system would be exposed as `G.timer`.

### Data model

```cpp
enum TimerType : uint8_t {
  kAfter,        // One-shot delayed callback
  kEvery,        // Repeating callback
  kDuring,       // Per-frame callback for a duration
  kTween,        // Property interpolation
  kCooldown,     // Time + condition gating
  kSpring,       // Damped harmonic oscillator
};

struct Timer {
  TimerType type;
  bool active;
  bool ignore_time_scale;  // UI/pause-immune timers

  float elapsed;           // Time accumulated
  float duration;          // Total duration (or interval for kEvery)
  float speed;             // Multiplier (for variable-rate repeating)

  // Tag for cancellation/replacement
  uint32_t tag_hash;       // Hashed tag for fast lookup

  // Lua callback refs (LUA_NOREF when unset)
  int action_ref;          // Main callback
  int after_ref;           // Completion callback (for kDuring, kTween)
  int condition_ref;       // Condition function (for kCooldown)

  // Type-specific data
  union {
    struct {               // kEvery
      int32_t remaining;   // Repetitions left (-1 = infinite)
    } every;

    struct {               // kTween
      int target_ref;      // Lua table being tweened
      int keys_ref;        // Lua table of {key, start, end} triples
      uint8_t easing;      // EasingType enum
    } tween;

    struct {               // kCooldown
      int32_t remaining;   // Repetitions left (-1 = infinite)
      bool ready;          // Timer has expired, waiting for condition
    } cooldown;

    struct {               // kSpring
      float value;         // Current position
      float velocity;      // Current velocity
      float target;        // Equilibrium position
      float stiffness;     // Spring constant k
      float damping;       // Damping coefficient d
      int target_ref;      // Lua table to write value into
      int key_ref;         // Lua string key to write
    } spring;
  };
};
```

Timer storage: `FixedArray<Timer>` pre-allocated at init (e.g., 256 slots).
Active timers are compacted (swap-with-last on removal) for cache-friendly
iteration. A `Dictionary<uint16_t>` maps tag hashes to slot indices for O(1)
cancel-by-tag.

### Easing functions

All 10 base curves from Robert Penner's canonical set, with `in`, `out`,
`in-out` variants generated mechanically. 31 functions total.

```cpp
enum EasingType : uint8_t {
  kLinear,
  kInQuad,    kOutQuad,    kInOutQuad,
  kInCubic,   kOutCubic,   kInOutCubic,
  kInQuart,   kOutQuart,   kInOutQuart,
  kInQuint,   kOutQuint,   kInOutQuint,
  kInSine,    kOutSine,    kInOutSine,
  kInExpo,    kOutExpo,    kInOutExpo,
  kInCirc,    kOutCirc,    kInOutCirc,
  kInBack,    kOutBack,    kInOutBack,
  kInElastic, kOutElastic, kInOutElastic,
  kInBounce,  kOutBounce,  kInOutBounce,
  kEasingCount,
};
```

Implementation: a single `float Ease(EasingType type, float t)` function with
a switch. Only the `in` variants need explicit formulas; `out` and `in-out` are
derived:

```cpp
// Composition rules (only need to implement In variants):
float EaseIn(EasingType base, float t);

float EaseOut(EasingType base, float t) {
  return 1.0f - EaseIn(base, 1.0f - t);
}

float EaseInOut(EasingType base, float t) {
  if (t < 0.5f) return EaseIn(base, t * 2.0f) * 0.5f;
  return 0.5f + EaseOut(base, (t - 0.5f) * 2.0f) * 0.5f;
}
```

Base `in` formulas (t in [0, 1]):

| Curve | Formula |
|-------|---------|
| Linear | `t` |
| Quad | `t*t` |
| Cubic | `t*t*t` |
| Quart | `t*t*t*t` |
| Quint | `t*t*t*t*t` |
| Sine | `1 - cos(t * PI/2)` |
| Expo | `t == 0 ? 0 : pow(2, 10*(t-1))` |
| Circ | `1 - sqrt(1 - t*t)` |
| Back | `t*t*((1.70158+1)*t - 1.70158)` |
| Elastic | `-pow(2, 10*(t-1)) * sin((t-1-0.075) * 2*PI / 0.3)` |
| Bounce | `1 - BounceOut(1-t)` where BounceOut is piecewise quadratic |

The easing function is resolved from a string name at timer creation time (not
per-frame), so there is no string lookup overhead during updates.

### Lua API

#### Timers

```lua
-- Delayed callback (fires once after delay seconds)
G.timer.after(delay, action)                --> tag
G.timer.after(delay, action, tag)           --> tag

-- Repeating callback
G.timer.every(delay, action)                --> tag  (infinite)
G.timer.every(delay, action, times)         --> tag  (limited repetitions)
G.timer.every(delay, action, times, tag)    --> tag

-- Duration callback (action called every frame with dt and elapsed)
G.timer.during(delay, action)               --> tag
G.timer.during(delay, action, after)        --> tag
G.timer.during(delay, action, after, tag)   --> tag
-- action receives: fn(dt, elapsed, fraction)
-- fraction is elapsed/duration, 0 to 1

-- Cancellation
G.timer.cancel(tag)          -- cancel by tag
G.timer.cancel_all()         -- cancel everything (use sparingly)

-- Query
G.timer.exists(tag)          --> bool
```

#### Tweens

```lua
-- Tween table fields toward target values
G.timer.tween(duration, subject, target, easing)          --> tag
G.timer.tween(duration, subject, target, easing, after)   --> tag
G.timer.tween(duration, subject, target, easing, after, tag) --> tag

-- subject: the Lua table whose fields will be modified
-- target: a table of {key = end_value} pairs
-- easing: string name ("linear", "out-quad", "in-out-cubic", etc.)
-- after: optional completion callback
```

Example:
```lua
local obj = {x = 100, y = 50, alpha = 1.0}

-- Move obj.x to 300 and obj.y to 200 over 0.5 seconds with ease-out
G.timer.tween(0.5, obj, {x = 300, y = 200}, "out-quad")

-- Fade out over 1 second, then destroy
G.timer.tween(1.0, obj, {alpha = 0}, "in-quad", function()
  obj.alive = false
end)
```

Tween implementation captures start values at creation time (not deferred like
flux, because our fixed timestep means there is no ambiguity about when the
tween starts).

#### Cooldowns

```lua
-- Fires action when BOTH delay has elapsed AND condition() returns true
G.timer.cooldown(delay, condition, action)           --> tag
G.timer.cooldown(delay, condition, action, times)    --> tag
G.timer.cooldown(delay, condition, action, times, tag) --> tag
```

Example:
```lua
-- Attack cooldown: can attack every 0.3s, but only while button is pressed
G.timer.cooldown(0.3, function()
  return G.input.is_down("z")
end, function()
  spawn_projectile(player.x, player.y)
end, 0, "player_attack")  -- 0 = infinite repetitions
```

#### Springs

```lua
-- Create a spring (returns spring userdata)
G.timer.spring(initial_value)                       --> spring
G.timer.spring(initial_value, stiffness, damping)   --> spring

-- Spring methods
spring:pull(force)              -- impulse: displace from current position
spring:animate(target)          -- change equilibrium (smooth transition)
spring:set(value)               -- hard set, kills velocity
spring:value()                  --> number (current position)
spring:velocity()               --> number

-- Attach spring to a table field (auto-updates every frame)
spring:bind(table, key)
-- Now table[key] is updated to spring:value() every frame automatically
```

Example:
```lua
function g.init()
  -- Scale spring for "squash and stretch"
  player.scale_spring = G.timer.spring(1.0, 200, 15)
  player.scale_spring:bind(player, "scale")
end

function g.update(t, dt)
  if just_landed then
    player.scale_spring:pull(-0.3)  -- squash on landing
  end
  -- player.scale is automatically updated by the spring
end

function g.draw()
  player.anim:draw(player.x, player.y, 0, player.scale, player.scale)
end
```

### Easing name resolution

Easing strings map to `EasingType` at timer creation. The format is
`"[in|out|in-out]-curve"`:

```
"linear"         -> kLinear
"in-quad"        -> kInQuad
"out-quad"       -> kOutQuad
"in-out-quad"    -> kInOutQuad
"in-cubic"       -> kInCubic
"out-cubic"      -> kOutCubic
"in-out-cubic"   -> kInOutCubic
"in-back"        -> kInBack
"out-back"       -> kOutBack
"in-out-back"    -> kInOutBack
"out-bounce"     -> kOutBounce
"out-elastic"    -> kOutElastic
...
```

A `Dictionary<EasingType>` built at init time maps these strings to enum values.
Invalid easing names produce a Lua error at creation time (fail fast, not at
runtime).

Shorthand: if no direction prefix is given, assume `out` (the most common in
games -- things start fast and decelerate). So `"quad"` means `"out-quad"`.

### Tag system

Tags are strings provided by the user (or auto-generated UUIDs if omitted).
They serve three purposes:

1. **Cancellation**: `G.timer.cancel("player_attack")` removes a specific timer.
2. **Auto-replacement**: Creating a timer with an existing tag cancels the old
   one first. This prevents duplicate timers when code runs repeatedly
   (e.g., re-entering a state).
3. **Hot-reload safety**: After code reload, `init()` re-registers timers with
   the same tags, automatically replacing stale ones from the previous load.

Tags are hashed to `uint32_t` for fast lookup. The hash-to-slot mapping is a
`Dictionary<uint16_t>`.

Auto-generated tags use an incrementing counter (no need for true UUIDs since
tags only need to be unique within a session).

### Time scale

Add to the engine clock:

```cpp
float time_scale = 1.0f;   // Multiplier for game dt
```

During the main loop update:

```cpp
float scaled_dt = dt * time_scale;
lua.Update(t, scaled_dt);              // Lua sees scaled dt
camera.Update(scaled_dt, viewport);    // Camera sees scaled dt
// Timer system internally uses scaled_dt for normal timers,
// raw dt for ignore_time_scale timers
```

Lua API for time scale:

```lua
G.clock.set_time_scale(scale)   -- 0.0 = paused, 0.5 = half speed, 1.0 = normal
G.clock.time_scale()            --> number
```

Timers with `ignore_time_scale` always use raw dt:

```lua
-- Screen fade during pause (ignores time scale)
G.timer.tween(0.5, overlay, {alpha = 1}, "in-quad", nil, "pause_fade")
G.timer.set_ignore_time_scale("pause_fade", true)
```

Or as a creation-time flag:

```lua
G.timer.after(0.5, unpause_fn, {tag = "unpause", real_time = true})
```

The simpler approach (flag per timer) is preferable to Godot's process mode or
DOTween's update type. Most games only need a handful of real-time timers (UI
transitions, input handling during pause).

### Update logic

Called once per frame by the engine, after `Lua.Update()`:

```
for each active timer:
  dt_eff = timer.ignore_time_scale ? raw_dt : scaled_dt
  dt_eff *= timer.speed

  switch timer.type:
    case kAfter:
      elapsed += dt_eff
      if elapsed >= duration:
        call action_ref
        remove timer

    case kEvery:
      elapsed += dt_eff
      while elapsed >= duration:  // while, not if (handles dt spikes)
        elapsed -= duration
        call action_ref
        if remaining > 0: remaining--
        if remaining == 0:
          call after_ref
          remove timer

    case kDuring:
      elapsed += dt_eff
      fraction = min(elapsed / duration, 1.0)
      call action_ref(dt_eff, elapsed, fraction)
      if elapsed >= duration:
        call after_ref
        remove timer

    case kTween:
      elapsed += dt_eff
      t = min(elapsed / duration, 1.0)
      eased_t = Ease(easing, t)
      for each (key, start, end) in tween keys:
        subject[key] = start + (end - start) * eased_t
      if elapsed >= duration:
        // Ensure exact final values
        for each (key, _, end) in tween keys:
          subject[key] = end
        call after_ref
        remove timer

    case kCooldown:
      elapsed += dt_eff
      if elapsed >= duration:
        if call condition_ref returns true:
          call action_ref
          elapsed = 0
          if remaining > 0: remaining--
          if remaining == 0:
            call after_ref
            remove timer

    case kSpring:
      // Semi-implicit Euler (stable for reasonable stiffness)
      a = -stiffness * (value - target) - damping * velocity
      velocity += a * dt_eff
      value += velocity * dt_eff
      if target_ref != LUA_NOREF:
        write value to target_ref[key_ref]
```

### Tween property access

Tweens need to read/write Lua table fields from C++. At creation time:

1. For each key in `target`, read the current value from `subject[key]` as the
   start value.
2. Store triples of `(key_string, start_float, end_float)` in a compact array.

During update:

1. For each triple, compute `lerp(start, end, eased_t)`.
2. Write back: `lua_rawgeti(L, LUA_REGISTRYINDEX, target_ref)` then
   `lua_setfield(L, -1, key)`.

This means tweens only work on numeric fields of Lua tables, which covers the
vast majority of use cases (position, scale, rotation, alpha, color components).

### Interaction with hot-reload

On hot-reload:
1. All Lua state is destroyed, including callback refs
2. The C++ timer array is cleared (all Lua refs are now invalid)
3. `init()` re-runs, re-registering timers

This is the same model as animation instances and physics bodies. Timers are
transient game state, not persistent data.

**Why clearing is safe**: Timers are created for gameplay effects (delays,
tweens, cooldowns). On hot-reload, the game restarts from `init()`. Any timers
from the previous run are irrelevant. Unlike a scene-switching system where
timers might need to persist, hot-reload is a full reset.

### Spring physics details

The spring uses a **semi-implicit Euler** integrator (velocity updated first,
then position):

```cpp
float a = -stiffness * (value - target) - damping * velocity;
velocity += a * dt;
value += velocity * dt;
```

This is more stable than explicit Euler (update position first) and simpler than
Verlet or RK4. For game-typical parameters (stiffness 40-300, damping 5-30) and
our fixed 1/60s timestep, it is unconditionally stable and visually correct.

**Settling detection**: When `|velocity| < epsilon` and `|value - target| <
epsilon`, snap to target and zero velocity. This prevents the spring from
vibrating indefinitely at sub-pixel amplitudes.

Default parameters: `stiffness = 180, damping = 12`. This gives a slightly
underdamped response (small overshoot, quick settle) which feels good for UI
elements. Based on Anchor's defaults tuned for game feel.

| Use case | Stiffness | Damping | Character |
|----------|-----------|---------|-----------|
| Bouncy UI | 100 | 8 | Very springy, multiple bounces |
| Snappy response | 300 | 25 | Quick, minimal overshoot |
| Camera follow | 200 | 28 | Nearly critically damped |
| Squash-stretch | 180 | 12 | One overshoot, fast settle |
| Gentle float | 40 | 6 | Slow, wobbly |

### Memory model

- **Timer slots**: `FixedArray<Timer>` pre-allocated (256 initially). Each slot
  is ~80 bytes. Total: ~20 KB. No heap allocation during gameplay.
- **Tag lookup**: `Dictionary<uint16_t>` for tag hash to slot index.
- **Tween key storage**: Small `FixedArray` within the timer module for
  `(key, start, end)` triples. Pre-allocated alongside the timer array.
- **Spring userdata**: Lua userdata (~48 bytes each). Small enough to be
  inline.
- **Lua callback refs**: Stored as `luaL_ref` integers. No Lua table allocation
  per timer. Freed with `luaL_unref` on timer removal.

### Usage examples

#### Screen flash (damage indicator)

```lua
function flash_screen()
  overlay.alpha = 1.0
  G.timer.tween(0.3, overlay, {alpha = 0}, "out-quad", nil, "flash")
end
```

#### Spawn wave (enemies appear in sequence)

```lua
function start_wave(enemies)
  for i, pos in ipairs(enemies) do
    G.timer.after(0.15 * i, function()
      spawn_enemy(pos.x, pos.y)
    end)
  end
end
```

#### Screen transition (fade out, switch, fade in)

```lua
function transition_to(scene_fn)
  G.timer.tween(0.4, overlay, {alpha = 1}, "in-quad", function()
    scene_fn()  -- switch scene
    G.timer.tween(0.4, overlay, {alpha = 0}, "out-quad", nil, "trans_in")
  end, "trans_out")
end
```

#### Invincibility frames with flashing

```lua
function take_damage()
  player.invincible = true
  player.visible = true
  G.timer.every(0.08, function()
    player.visible = not player.visible
  end, 15, "iframes_flash")  -- flash 15 times (1.2 seconds)
  G.timer.after(1.2, function()
    player.invincible = false
    player.visible = true
  end, "iframes_end")
end
```

#### Smooth UI movement with springs

```lua
function g.init()
  menu.y_spring = G.timer.spring(0, 200, 18)
  menu.y_spring:bind(menu, "y")
  menu.y_spring:animate(screen_h / 2)  -- slide menu to center
end

function dismiss_menu()
  menu.y_spring:animate(-100)  -- slide off screen with spring physics
  G.timer.after(0.5, function()
    menu.active = false
  end, "menu_dismiss")
end
```

#### Weapon cooldown

```lua
G.timer.cooldown(0.15, function()
  return G.input.is_down("x")
end, function()
  fire_bullet(player.x, player.y, player.facing)
end, 0, "player_shoot")  -- 0 = infinite repetitions
```

### What this does NOT include

- **Coroutine-based scripting**: hump.timer's `script(fn(wait))` is elegant but
  requires Lua coroutines, which complicate hot-reload (coroutine state is not
  serializable) and error handling (errors inside coroutines need special
  handling). The `after` + `tween` chaining covers the same use cases less
  elegantly but more robustly. Could be added as a Lua-side wrapper later.

- **Sequence/timeline API**: DOTween's Sequence and GSAP's Timeline are
  powerful but complex. For our engine, chaining via completion callbacks
  (`after` parameter on tweens) is sufficient. A timeline API could be built
  in Lua on top of the primitives.

- **Tween conflict resolution**: flux automatically cancels tweens on the same
  object property. We rely on tags for explicit cancellation instead. If the
  user creates two tweens on the same object field without cancelling, they will
  fight. This is simpler and more predictable.

- **Per-property easing**: All fields in a single tween use the same easing
  curve. For different easing per field, create separate tweens. This matches
  hump.timer and Anchor.

- **Nested table tweening**: Only top-level numeric fields of the subject table
  are tweened. No recursive descent into nested tables (unlike hump.timer which
  supports this). `{x = 100}` works, `{pos = {x = 100}}` does not. This
  avoids complexity and covers the common case.

## Implementation plan

### Phase 1: Timers

1. Create `timer.h` / `timer.cc` with the Timer struct and FixedArray storage.
2. `after`, `every`, `during` with tag-based management.
3. `Update(float dt)` called from the engine main loop.
4. Lua bindings in `lua_timer.cc`: `G.timer.after`, `G.timer.every`,
   `G.timer.during`, `G.timer.cancel`, `G.timer.cancel_all`.

### Phase 2: Easing and tweens

1. `easing.h` with all 31 easing functions.
2. Tween type: capture start values, interpolate per frame, write back to Lua.
3. Easing name dictionary for string-to-enum resolution.
4. Lua bindings: `G.timer.tween`.

### Phase 3: Cooldowns

1. Cooldown type: elapsed check + condition function check.
2. Lua bindings: `G.timer.cooldown`.

### Phase 4: Springs

1. Spring struct with semi-implicit Euler.
2. Spring as Lua userdata with metatable.
3. `bind()` for auto-updating Lua table fields.
4. Lua bindings: `G.timer.spring`, `spring:pull`, `spring:animate`.

### Phase 5: Time scale

1. Add `time_scale` to the engine clock.
2. Pass `scaled_dt` and `raw_dt` to the timer system.
3. `ignore_time_scale` flag on timers.
4. Lua: `G.clock.set_time_scale`, `G.clock.time_scale`.

### Phase 6: Expose easing as utility

1. `G.math.ease(easing, t)` for standalone easing evaluation.
2. Useful for manual interpolation in game code without creating a timer.
