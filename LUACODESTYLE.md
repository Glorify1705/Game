# Lua Code Style Guide

This guide covers Lua game code written for this engine. It draws from the
[Roblox Lua Style Guide](https://roblox.github.io/lua-style-guide/) and the
[LuaRocks Style Guide](https://github.com/luarocks/lua-style-guide), adapted
to our conventions and constraints.

Where this guide is silent, defer to the LuaRocks guide. Where this guide
contradicts it, this guide wins.

All code must be valid **Lua 5.1**. All code must be formatted with
**StyLua**. Non-negotiable.

## Table of Contents

- [Lua 5.1 Constraints](#lua-51-constraints)
- [StyLua Configuration](#stylua-configuration)
- [Naming](#naming)
- [Variables and Scope](#variables-and-scope)
- [Strings](#strings)
- [Tables](#tables)
- [Functions](#functions)
- [Modules](#modules)
- [OOP with Classic](#oop-with-classic)
- [Game Loop Callbacks](#game-loop-callbacks)
- [The G API](#the-g-api)
- [Control Flow](#control-flow)
- [Error Handling](#error-handling)
- [Comments](#comments)
- [File Organization](#file-organization)
- [Fennel](#fennel)
- [Things to Avoid](#things-to-avoid)

---

## Lua 5.1 Constraints

These features do not exist in Lua 5.1. Do not use them:

- `goto` and `::labels::` (added in 5.2)
- `continue` (never added to standard Lua)
- Bitwise operators `&`, `|`, `~`, `<<`, `>>` (added in 5.3)
- `table.unpack` (use the global `unpack` instead)
- `table.move`, `table.pack`
- Integer division `//` (added in 5.3)
- `utf8` library
- `load()` with a string argument (use `loadstring()` in 5.1)

Things that exist in 5.1 but should be avoided:

- `module()` and `setfenv()`/`getfenv()` — use the return-a-table pattern
- `arg` as a parameter name — conflicts with the implicit vararg table
- Relying on `#` for tables with holes — behavior is undefined

The ternary idiom `x and y or z` fails when `y` is `nil` or `false`. Use an
explicit `if` when `y` could be falsy.

## StyLua Configuration

Place a `.stylua.toml` at the project root:

```toml
syntax = "Lua51"
column_width = 100
line_endings = "Unix"
indent_type = "Tabs"
indent_width = 4
quote_style = "AutoPreferDouble"
call_parentheses = "Always"
collapse_simple_statement = "Never"
```

Key choices:

- **Tabs** for indentation (matches the C++ side of the project).
- **Double quotes** preferred. StyLua will auto-switch to single quotes when
  the string contains double quotes to reduce escaping.
- **Always use parentheses** in function calls. Never omit them for single
  string or table arguments.
- **100-column soft limit**. StyLua will wrap around this width. Comments
  should be wrapped to 80 columns.
- **No collapsed statements**. `if x then return end` must be written on
  multiple lines.

Use `-- stylua: ignore` before a statement to opt out of formatting for that
statement.

## Naming

| Kind                     | Convention     | Example                             |
| ------------------------ | -------------- | ----------------------------------- |
| Local variables          | `snake_case`   | `player_speed`, `hit_count`         |
| Local functions          | `snake_case`   | `spawn_meteor`, `clamp_angle`       |
| Module-level constants   | `UPPER_CASE`   | `MAX_SPEED`, `PLAYER_RADIUS`        |
| Module tables            | `PascalCase`   | `local Game = {}`                   |
| Classes (via Classic)    | `PascalCase`   | `Player`, `Entity`, `Vec2`          |
| Methods                  | `snake_case`   | `Player:get_health()`               |
| Boolean variables        | `is_`/`has_`   | `is_active`, `has_shield`           |
| Private/internal         | `_` prefix     | `_reset_state()`                    |
| Ignored variables        | `_`            | `for _, v in ipairs(t) do`          |
| Iterator index           | `i`            | `for i = 1, n do`                   |
| Coordinate pairs         | `x`, `y`       | `local x, y = obj:position()`       |
| Dimensions               | `w`, `h`       | `local w, h = G.window.dimensions()`|
| Timing                   | `t`, `dt`      | `function Game:update(t, dt)`       |

**Names should grow with scope.** A one-letter name is fine in a 3-line loop.
A 50-line function deserves descriptive names.

**Acronyms**: capitalize only the first letter. `HttpClient`, not
`HTTPClient`. Exception: 2-3 letter conventions like `id`, `dx`, `dy`.

## Variables and Scope

Always use `local`. Never create global variables. The only permitted globals
are `require` results that are classes used throughout the file (matching the
existing `Object = require("classic")` convention):

```lua
-- OK: class-level require at file scope
Object = require("classic")
Entity = require("entity")

-- OK: local for everything else
local SPEED = 200
local timer = Timer()
```

Declare variables as close to their first use as possible. Don't pre-declare
all locals at the top of a function.

```lua
-- Bad
local x, y, angle, info
x = 10
y = 20

-- Good
local x = 10
local y = 20
```

Don't shadow variables from outer scopes unless the inner scope is very short
and the intent is obvious.

## Strings

Prefer double quotes. StyLua enforces this.

```lua
local name = "player"
local msg = 'He said "hello"'  -- single quotes to avoid escaping
local block = [=[Multi-line
string content]=]
```

Use `string.format()` for interpolation, not repeated concatenation:

```lua
-- Bad
local msg = "Player " .. name .. " has " .. tostring(hp) .. " HP"

-- Good
local msg = string.format("Player %s has %d HP", name, hp)
```

## Tables

Trailing commas on multi-line tables. Always.

```lua
-- Single line for 1-2 simple entries
local color = { 255, 0, 0, 255 }

-- Multi-line for anything else
local config = {
	speed = 200,
	radius = 16,
	category = CAT_PLAYER,
}
```

Don't mix list-style and dictionary-style entries in the same table.

Use plain `key = value` syntax. Use `["key"]` only for non-identifier keys:

```lua
local t = {
	name = "player",
	["my-key"] = true,  -- hyphenated, needs brackets
}
```

Use `ipairs` for arrays, `pairs` for dictionaries. Be explicit about intent.

## Functions

Use `local function name()` syntax, not `local name = function()`:

```lua
-- Good
local function spawn_meteor(x, y)
	-- ...
end

-- Bad
local spawn_meteor = function(x, y)
	-- ...
end
```

The exception is when you need a forward reference (declare the local first,
assign the function later):

```lua
local on_complete
local function start()
	-- on_complete is used here before it's defined below
	timer:after(1, on_complete)
end
on_complete = function()
	start()
end
```

Keep parameter lists short. Prefer a config table over more than 4-5
parameters:

```lua
-- Bad
local function create_entity(x, y, angle, image, id, health, speed)

-- Good
local function create_entity(config)
	-- config.x, config.y, config.image, etc.
end
```

Always use parentheses when calling functions, even with a single string or
table argument:

```lua
-- Good
require("classic")
print("hello")
table.insert(t, { x = 1 })

-- Bad
require "classic"
print "hello"
```

## Modules

Every file returns a single table. No side effects from `require`.

**Simple module** (for game scripts, test files):

```lua
local Game = {}

local SPEED = 200
local score = 0

local function helper()
	-- file-local function
end

function Game:init()
	-- ...
end

function Game:update(t, dt)
	-- ...
end

function Game:draw()
	-- ...
end

return Game
```

**Utility module** (for reusable libraries):

```lua
local Vec2 = {}
Vec2.__index = Vec2

function Vec2.new(x, y)
	return setmetatable({ x = x or 0, y = y or 0 }, Vec2)
end

function Vec2:length()
	return math.sqrt(self.x * self.x + self.y * self.y)
end

return Vec2
```

File structure order:

1. Requires (class-level globals first, then locals)
2. Constants (`UPPER_CASE`)
3. Module-level state
4. Local helper functions
5. Module methods
6. `return` statement

## OOP with Classic

The engine uses [classic](https://github.com/rxi/classic) for OOP. Follow
these conventions:

```lua
Object = require("classic")

Monster = Object:extend()

function Monster:new(x, y, hp)
	self.x = x
	self.y = y
	self.hp = hp
end

function Monster:is_alive()
	return self.hp > 0
end

function Monster:take_damage(amount)
	self.hp = self.hp - amount
end

return Monster
```

**Inheritance**:

```lua
Entity = require("entity")

Player = Entity:extend()

function Player:new(x, y)
	Player.super.new(self, x, y, 0, "playerShip1_green", "player")
	self.health = 100
end

return Player
```

- Call `super.new(self, ...)` in the constructor when extending.
- Use `:` (colon) syntax for instance methods.
- Use `.` (dot) syntax for class-level/static functions.
- All instance state is initialized in `new()`. Don't add new fields
  elsewhere.

## Game Loop Callbacks

The engine calls these methods on the module table each frame:

```lua
function Game:init()
	-- Called once at startup. Set up state, load assets.
end

function Game:update(t, dt)
	-- Called every frame. t = total elapsed time, dt = delta time.
	-- Do logic, physics, input handling here.
end

function Game:draw()
	-- Called every frame after update. All rendering goes here.
end
```

Keep `draw()` free of game logic. Keep `update()` free of draw calls.

## The G API

The engine exposes its API through the `G` global table. The API uses
`snake_case` throughout.

```lua
-- Graphics
G.graphics.draw_sprite("ship", x, y, angle)
G.graphics.set_color(255, 0, 0, 255)
G.graphics.clear()

-- Input
G.input.is_key_down("w")
G.input.is_key_pressed("space")

-- Window
local w, h = G.window.dimensions()
G.window.set_title("My Game")

-- Physics
local handle = G.physics.add_circle(x, y, radius, userdata)
G.physics.apply_force(handle, fx, fy)

-- Sound
G.sound.play_effect("explosion")
G.sound.play("theme")

-- Collision
local world = G.collision.new_world(cell_size)
local shape = G.collision.circle(radius)

-- Assets
local info = G.assets.sprite_info("ship")

-- System
G.system.quit()
```

Don't wrap the `G` API in unnecessary abstractions. Call it directly.

**Prefer engine functions over reimplementing.** Before writing utility code
(math helpers, data structure operations, file I/O, random number generation,
etc.), check whether the engine already provides it through `G.*`. The engine
API is extensive and covers graphics, sound, input, physics, collision,
filesystem, math, random numbers, asset management, and more.

**Consult the type definitions.** The file `definitions/game.lua` contains
auto-generated LuaLS type stubs for every engine function, including parameter
names, types, and descriptions. Read it before implementing functionality that
the engine might already provide. You can regenerate it at any time with:

```sh
game stubs
```

If your editor supports LuaLS (Lua Language Server), the definitions file
gives you autocomplete and type checking for the entire `G.*` API. Use it.

## Control Flow

Prefer early returns to reduce nesting:

```lua
-- Bad
function Player:take_damage(amount)
	if self:is_alive() then
		self.hp = self.hp - amount
		if self.hp <= 0 then
			self:die()
		end
	end
end

-- Good
function Player:take_damage(amount)
	if not self:is_alive() then
		return
	end
	self.hp = self.hp - amount
	if self.hp <= 0 then
		self:die()
	end
end
```

Since Lua 5.1 has no `continue`, use a nested `if` or restructure the loop:

```lua
-- Skip pattern (no continue available)
for _, entity in ipairs(entities) do
	if entity:is_alive() then
		entity:update(dt)
	end
end
```

Don't use single-line if blocks:

```lua
-- Bad
if dead then return end

-- Good
if dead then
	return
end
```

## Error Handling

For game code, prefer **checking state** over catching errors. Most game code
should not use `pcall`:

```lua
-- Prefer
if entity and entity:is_alive() then
	entity:update(dt)
end

-- Over
local ok, err = pcall(function() entity:update(dt) end)
```

Use `assert()` for things that indicate a programming bug, not a runtime
condition:

```lua
function Player:equip(weapon)
	assert(weapon, "weapon must not be nil")
	self.weapon = weapon
end
```

For utility modules that may fail, return `nil, message`:

```lua
function Config.load(path)
	local f = io.open(path, "r")
	if not f then
		return nil, "could not open " .. path
	end
	-- ...
	return data
end
```

## Comments

Comments explain **why**, not what. The code already says what.

```lua
-- Good: explains a non-obvious decision
-- Use squared distance to avoid sqrt in hot loop
if dx * dx + dy * dy < r * r then

-- Bad: restates the code
-- Check if distance is less than radius
if dx * dx + dy * dy < r * r then
```

Use `--` single-line comments. Use `--[[ ]]` for multi-line blocks or
temporarily disabling code.

Wrap comments at 80 columns even though code wraps at 100.

**Header comments** at the top of a file are encouraged for game scripts that
aren't self-explanatory:

```lua
-- Interactive collision system test
-- Controls:
--   WASD: move player
--   Space: cast ray toward mouse
--   Q: quit
```

Do not include filenames, authors, or dates in comments.

Use `TODO` and `FIXME` markers:

```lua
-- TODO: add support for polygon shapes
-- FIXME: this breaks when the entity list is empty
```

## File Organization

- **Filenames**: `snake_case.lua` (e.g., `player.lua`, `vector2d.lua`)
- **One module per file**. The filename matches the module name.
- **Test files**: `test*.lua` (e.g., `testcollision.lua`, `testsound.lua`)
- **Entry point**: `main.lua`

Group requires by kind:

```lua
-- Classes (global, for inheritance chains)
Object = require("classic")
Entity = require("entity")

-- Libraries (local)
local Timer = require("timer")
local lume = require("lume")

-- Constants
local SPEED = 200
local MAX_HP = 100
```

## Fennel

Fennel files (`.fnl`) are supported alongside Lua. Fennel conventions differ:

- **Naming**: `kebab-case` for functions and variables (`add-object!`,
  `update-position`)
- **Predicates**: end with `?` (`finished?`, `empty?`)
- **Mutating functions**: end with `!` (`push!`, `scale!`)
- **Special variables**: `*wrapped-in-asterisks*` for module-level state

Fennel compiles to Lua 5.1. The same runtime constraints apply.

## Things to Avoid

- **Global variables** (except class requires at file scope)
- **Deep inheritance hierarchies** — one or two levels is fine, more is a
  smell
- **Metatables for everything** — use plain tables when you don't need
  methods
- **String concatenation in loops** — use `table.concat()`
- **`table.getn()`** — use `#` instead (deprecated in 5.1, removed in 5.2)
- **Magic numbers** — give them a name
- **Overusing `self`** — if a function doesn't use `self`, make it a dot
  function or a local function
- **`type()` checks in hot paths** — trust your data, validate at boundaries
- **Semicolons** — they're legal but there's no reason to use them
