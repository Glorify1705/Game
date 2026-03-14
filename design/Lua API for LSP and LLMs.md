# Lua API for LSP and LLMs

## Problem

The game's Lua API is defined entirely in C++ (`lua_*.cc` files). This means:

- **LuaLS** (lua-language-server) has no visibility into `G.*` functions -- no autocomplete, no hover docs, no type checking
- **LLMs** generating game scripts have no structured reference to work from
- **Game developers** must read C++ source or use the runtime `_Docs` table to discover the API

## Current State

### API Structure

All game APIs live under a global `G` table with sub-modules:

```lua
G.graphics.draw_rect(x1, y1, x2, y2)
G.input.is_key_pressed("q")
G.math.v2(x, y)
G.physics.add_box(...)
G.sound.play("sound.wav")
G.system.quit()
G.window.set_title("Title")
G.clock.gametime()
G.filesystem.*
G.data.*
G.assets.*
G.random.*
```

### Two Registration Tiers

**Rich (`LuaApiFunction`)** -- used by `graphics` and `sound`:
```cpp
static const LuaApiFunction kGraphicsLib[] = {
    {"draw_sprite",
     "Draws a sprite by name to the screen",
     {{"sprite", "the name of the sprite"}, {"x", "x position"}, {"y", "y position"}},
     {},
     [](lua_State* state) { ... }},
};
```

**Basic (`luaL_Reg`)** -- used by everything else (`input`, `math`, `physics`, `system`, `clock`, `filesystem`, `data`, `assets`, `random`):
```cpp
const struct luaL_Reg kInputLib[] = {
    {"mouse_position", [](lua_State* state) { ... }},
    {"is_key_down", [](lua_State* state) { ... }},
};
```

The rich format captures docstrings and argument names/descriptions, exposed at runtime via the `_Docs` global table. The basic format has no metadata at all.

### Userdata Types

All types below are C++ userdata with metatables, created via `luaL_newmetatable`. They are all known at compile time -- none are created dynamically at runtime.

#### Vectors (`lua_math.cc`)

| Metatable | Constructor | Metamethods | Methods | Fields |
|-----------|-------------|-------------|---------|--------|
| `fvec2` | `G.math.v2(x, y)` | `__add`, `__sub`, `__mul`, `__tostring` | `dot(other)`, `len2()`, `normalized()`, `send_as_uniform(name)` | x, y |
| `fvec3` | `G.math.v3(x, y, z)` | `__add`, `__sub`, `__mul`, `__tostring` | `dot(other)`, `len2()`, `normalized()`, `send_as_uniform(name)` | x, y, z |
| `fvec4` | `G.math.v4(x, y, z, w)` | `__add`, `__sub`, `__mul`, `__tostring` | `dot(other)`, `len2()`, `normalized()`, `send_as_uniform(name)` | x, y, z, w |

`__mul` supports both `v * scalar` and `scalar * v`.

#### Matrices (`lua_math.cc`)

| Metatable | Constructor | Methods |
|-----------|-------------|---------|
| `fmat2x2` | `G.math.m2x2(v1, v2, v3, v4)` | `send_as_uniform(name)` |
| `fmat3x3` | `G.math.m3x3(v1, ..., v9)` | `send_as_uniform(name)` |
| `fmat4x4` | `G.math.m4x4(v1, ..., v16)` | `send_as_uniform(name)` |

No metamethods.

#### ByteBuffer (`lua_bytebuffer.cc`)

| Metatable | Constructors | Metamethods |
|-----------|-------------|-------------|
| `byte_buffer` | returned by `G.filesystem.slurp(path)`, `G.graphics.take_screenshot()` | `__index` (1-based byte access), `__len`, `__tostring`, `__concat` |

Not created directly by user code -- returned from API functions.

#### Physics Handle (`lua_physics.cc`)

| Metatable | Constructors |
|-----------|-------------|
| `physics_handle` | `G.physics.add_box(tx, ty, bx, by, angle, callback)`, `G.physics.add_circle(tx, ty, radius, callback)` |

No methods on the handle itself. Physics operations take the handle as a first argument:
- `G.physics.destroy_handle(h)`
- `G.physics.position(h)` -- returns x, y
- `G.physics.angle(h)`
- `G.physics.rotate(h, angle)`
- `G.physics.apply_linear_impulse(h, x, y)`
- `G.physics.apply_force(h, x, y)`
- `G.physics.apply_torque(h, torque)`

#### Random Number Generator (`lua_random.cc`)

| Metatable | Constructors |
|-----------|-------------|
| `random_number_generator` | `G.random.from_seed(seed)`, `G.random.non_deterministic()` |

No methods on the RNG itself. Random operations take the RNG as a first argument:
- `G.random.sample(rng)` -- returns 0.0-1.0
- `G.random.sample(rng, start, end_)` -- returns value in [start, end]
- `G.random.pick(rng, list)` -- returns random element

#### Sprite Asset (`lua_assets.cc`)

| Metatable | Constructor | Fields |
|-----------|-------------|--------|
| `asset_sprite_ptr` | `G.assets.sprite(name)` -- returns userdata or nil | width, height, x, y, spritesheet |

Read-only fields accessible via `__index`.

## Proposal: LuaLS Stub Files

Generate `.lua` annotation files that LuaLS can consume. These use the [LuaCATS annotation format](https://luals.github.io/wiki/annotations/):

```lua
---@meta

-- ============================================================
-- Userdata types
-- ============================================================

---@class vec2
---@field x number
---@field y number
---@operator add(vec2): vec2
---@operator sub(vec2): vec2
---@operator mul(number): vec2
local vec2 = {}
---@param other vec2
---@return number
function vec2:dot(other) end
---@return number
function vec2:len2() end
---@return vec2
function vec2:normalized() end
---@param name string uniform name in the active shader
function vec2:send_as_uniform(name) end

---@class vec3
---@field x number
---@field y number
---@field z number
---@operator add(vec3): vec3
---@operator sub(vec3): vec3
---@operator mul(number): vec3
local vec3 = {}
---@param other vec3
---@return number
function vec3:dot(other) end
---@return number
function vec3:len2() end
---@return vec3
function vec3:normalized() end
---@param name string
function vec3:send_as_uniform(name) end

---@class vec4
---@field x number
---@field y number
---@field z number
---@field w number
---@operator add(vec4): vec4
---@operator sub(vec4): vec4
---@operator mul(number): vec4
local vec4 = {}
---@param other vec4
---@return number
function vec4:dot(other) end
---@return number
function vec4:len2() end
---@return vec4
function vec4:normalized() end
---@param name string
function vec4:send_as_uniform(name) end

---@class mat2x2
local mat2x2 = {}
---@param name string
function mat2x2:send_as_uniform(name) end

---@class mat3x3
local mat3x3 = {}
---@param name string
function mat3x3:send_as_uniform(name) end

---@class mat4x4
local mat4x4 = {}
---@param name string
function mat4x4:send_as_uniform(name) end

---@class byte_buffer
local byte_buffer = {}

---@class physics_handle

---@class rng

---@class sprite_asset
---@field width number
---@field height number
---@field x number
---@field y number
---@field spritesheet string

-- ============================================================
-- G namespace
-- ============================================================

---@class G
---@field graphics G.graphics
---@field input G.input
---@field sound G.sound
---@field math G.math
---@field physics G.physics
---@field system G.system
---@field window G.window
---@field clock G.clock
---@field filesystem G.filesystem
---@field data G.data
---@field assets G.assets
---@field random G.random
G = {}

-- ============================================================
-- G.math (showing typed constructors)
-- ============================================================

---@class G.math
G.math = {}

---@param x number
---@param y number
---@return vec2
function G.math.v2(x, y) end

---@param x number
---@param y number
---@param z number
---@return vec3
function G.math.v3(x, y, z) end

-- ... etc for v4, m2x2, m3x3, m4x4

-- ============================================================
-- G.graphics (showing docs + params)
-- ============================================================

---@class G.graphics
G.graphics = {}

---Clear the screen to black
function G.graphics.clear() end

---Draws a sprite by name to the screen
---@param sprite string the name of the sprite in any sprite sheet
---@param x number the x position
---@param y number the y position
function G.graphics.draw_sprite(sprite, x, y) end

-- ============================================================
-- G.physics (showing handle-based API)
-- ============================================================

---@class G.physics
G.physics = {}

---@param tx number top-left x
---@param ty number top-left y
---@param bx number bottom-right x
---@param by number bottom-right y
---@param angle number rotation in radians
---@param callback? function collision callback
---@return physics_handle
function G.physics.add_box(tx, ty, bx, by, angle, callback) end

---@param handle physics_handle
---@return number x, number y
function G.physics.position(handle) end

-- ============================================================
-- G.random (showing RNG-based API)
-- ============================================================

---@class G.random
G.random = {}

---@param seed integer
---@return rng
function G.random.from_seed(seed) end

---@return rng
function G.random.non_deterministic() end

---@param generator rng
---@param start? number range start (inclusive)
---@param end_? number range end (inclusive)
---@return number
function G.random.sample(generator, start, end_) end
```

### What this enables

- **Autocomplete**: typing `G.graphics.` shows all available functions; typing `v:` on a vec2 shows `dot`, `len2`, `normalized`, `send_as_uniform`
- **Hover docs**: hovering over `G.graphics.draw_sprite` shows docstring + params
- **Type checking**: passing a `vec3` where a `vec2` is expected gets flagged; `G.physics.position(handle)` returns `number, number`
- **Return type flow**: `local v = G.math.v2(1, 2)` -- LuaLS knows `v` is a `vec2`, offers `v:dot()`, `v.x`, and `v + other_vec2`
- **LLM context**: an LLM reading the stubs knows the full API surface including all 10 userdata types and their methods

### Generation Strategy

Two options:

**Option A: Hand-written stubs**
- Maintain a `definitions/` directory with `.lua` stub files
- Most accurate (can express complex types like vec2 metamethods)
- Risk of drift from C++ implementation

**Option B: Auto-generated at build time**
- Add a build step that reads `LuaApiFunction` metadata and emits stubs
- Stays in sync automatically
- Requires all libraries to use `LuaApiFunction` (currently only graphics + sound do)
- Harder to express complex types (vec2 metamethods, return types)

**Recommended: Option A to start, Option B later.** Hand-write stubs now for immediate LSP support. Migrate all libraries to `LuaApiFunction` with type info over time, then automate.

## Steps

### Phase 1: LuaLS Setup + Hand-Written Stubs

1. Create `definitions/game.lua` with `---@meta` annotations for all `G.*` modules
2. Add `.luarc.json` to the assets directory so LuaLS finds the stubs:
   ```json
   {
     "workspace.library": ["../definitions"],
     "runtime.version": "Lua 5.1",
     "diagnostics.globals": ["G", "_Docs"]
   }
   ```
3. Annotate special types (vec2, vec3, matrices, physics handles)

### Phase 2: Migrate All Libraries to `LuaApiFunction`

- Add docstrings and arg metadata to: `input`, `math`, `physics`, `system`, `clock`, `filesystem`, `data`, `assets`, `random`
- Add a `type` field to `LuaApiFunctionArg` (e.g. `"number"`, `"string"`, `"vec2"`, `"boolean"`)
- Add typed return annotations to `LuaApiFunction`

### Phase 3: Auto-Generation

- Write a build step (C++ or script) that iterates `LuaApiFunction` arrays and emits `.lua` stubs
- Run as a CMake custom command so stubs stay in sync
- Keep hand-written overrides for complex types that auto-generation can't express

## Files

| File | Purpose |
|------|---------|
| `definitions/game.lua` | LuaLS stub annotations for the full `G.*` API |
| `assets/.luarc.json` | LuaLS workspace config pointing to stubs |
| `src/lua.h` | `LuaApiFunction` / `LuaApiFunctionArg` structs (add type field) |
| `src/lua_*.cc` | Migrate from `luaL_Reg` to `LuaApiFunction` |
