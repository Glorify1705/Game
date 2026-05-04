---
status: in-design
tags: [tilemap, rendering, collision, lua-api, gameplay]
---

# Tilemap System

A 2D tilemap system for building platformers, RPGs, and top-down action games.
Tiles are the fundamental building block of level geometry in these genres.

## Motivation

Tilemaps are the most requested missing feature for non-physics games. The
engine already has the building blocks (batch renderer, sprite atlases, camera,
collision spatial hash) but no way to compose them into a tile-based level.

Without tilemaps, building a platformer requires manually placing collision
boxes and drawing sprites at hardcoded positions. A tilemap system provides:

- **Level design workflow**: Edit levels in Tiled (the standard 2D map editor),
  export as JSON, and load them directly.
- **Efficient rendering**: Only draw the tiles visible in the camera viewport.
- **Integrated collision**: Solid tiles become collision geometry automatically.
- **Layer compositing**: Background, playfield, and foreground layers with
  optional parallax.

## Engine survey

### high_impact

The most relevant comparison: C, minimal, 2D action games, same design ethos.

**Tilemap features**: First-class `map_t` with collision map, 4 background
layers, 54 slope tile types, parallax factors per layer, animated tiles.

**Collision**: AABB sweep trace against collision map tiles. Slope tiles have
precomputed height functions. Resolution slides remaining velocity along the
collision tangent. This is the gold standard for platformer tile collision.

**Rendering**: Iterates visible tiles (camera viewport / tile size), draws
from a tileset atlas. Simple and efficient.

**Key design**: The collision map is a separate grid of tile types (0 = empty,
1-54 = solid/slope variants), not a visual layer. Visual tiles and collision
tiles are independent. This separation is important: visual tiles can be
decorative without affecting gameplay.

### libGDX

**Tilemap features**: First-class Tiled (TMX) import with orthogonal,
isometric, and hex renderers. Supports tile layers, object layers (spawn
points, trigger zones), animated tiles, and tile properties.

**Collision**: Object layers define Box2D bodies (rectangles, polygons,
polylines). Tiles themselves don't have collision; collision shapes are
drawn explicitly in the Tiled editor on a separate object layer.

**Key design**: Full Tiled format support means level designers use Tiled's
visual editor and the engine reads the output directly. No custom tools
needed.

### Love2D

No built-in tilemap. The community uses STI (Simple Tiled Implementation),
a pure-Lua library that loads TMX/TMJ files and renders them. STI handles
frustum culling, animated tiles, and collision body generation. The fact that
this works well as a pure-Lua library suggests that a Lua-side implementation
is viable for our engine too.

### Carimbo

JSON tilemap + atlas PNG. Loads tile data from JSON, generates Box2D bodies
for collision tiles at load time. Layer offset factors for parallax.

### Summary

| Feature | high_impact | libGDX | STI (Love2D) | Carimbo |
|---------|-------------|--------|--------------|---------|
| **Format** | Custom binary | TMX (XML) | TMX/TMJ | JSON |
| **Collision** | Grid sweep + slopes | Object layer bodies | AABB bodies | Box2D bodies |
| **Layers** | 4 bg + collision | Unlimited | Unlimited | Layered |
| **Slopes** | 54 types | No | No | No |
| **Animated tiles** | Yes | Yes | Yes | No |
| **Parallax** | Per-layer factor | No | No | Offset factors |
| **Isometric/hex** | No | Yes | Yes | No |

## Existing engine support

**Already have (directly usable)**:
- Batch renderer with spritesheet atlas and automatic state deduplication
- Camera with follow, bounds, zoom, shake
- XML parser (custom, `xml.h`) and JSON parser (yyjson)
- Spatial hash collision world with `move_and_slide`, `move_and_collide`,
  raycasting, overlap queries
- Transform stack for parallax layer offsets
- Canvas system for layer compositing
- Stencil buffer for masking (fog-of-war)

**Missing**:
- Tilemap data structure (grid of tile IDs)
- Tile-to-UV mapping (tile ID -> spritesheet region)
- Viewport culling (which tiles are visible)
- Tile collision integration (grid -> collision bodies)
- Tiled format loader (TMJ/TMX)

## Design

### Data model

A tilemap has layers, and each layer has a grid of tile IDs. Tile ID 0 means
empty. Positive IDs reference tiles in a tileset (a spritesheet with a fixed
grid of tiles).

```
Tilemap
  tileset: spritesheet name + tile dimensions
  layers: [Layer, ...]

Layer
  name: string
  grid: int[] (flat array, row-major)
  width, height: tile counts
  parallax_x, parallax_y: float (1.0 = normal, 0.5 = half speed)
  visible: bool
  collision: bool (if true, non-zero tiles are solid)
```

Tile IDs map to tileset positions:
```
tile_id -> tileset column = (tile_id - 1) % tiles_per_row
tile_id -> tileset row    = (tile_id - 1) / tiles_per_row
```

### Lua API

#### Loading

```lua
-- Load from a Tiled JSON export (.tmj)
local map = G.tilemap.load("level1.tmj")

-- Or build programmatically
local map = G.tilemap.new({
  tile_width = 16,
  tile_height = 16,
  tileset = "tiles.png",     -- spritesheet name
})
map:add_layer("background", width, height)
map:add_layer("collision", width, height, { collision = true })
map:add_layer("foreground", width, height)

map:set_tile("collision", x, y, tile_id)
map:get_tile("collision", x, y) -> tile_id
map:fill_rect("collision", x1, y1, x2, y2, tile_id)
```

#### Rendering

```lua
-- Draw all visible layers (camera-aware, only visible tiles)
map:draw()

-- Draw a single layer (for manual ordering with game entities)
map:draw_layer("background")
-- ... draw entities ...
map:draw_layer("foreground")
```

The renderer iterates only over tiles in the camera viewport:
```
start_col = floor(camera_x / tile_width)
end_col   = ceil((camera_x + viewport_width) / tile_width)
start_row = floor(camera_y / tile_height)
end_row   = ceil((camera_y + viewport_height) / tile_height)
```

Each visible tile emits one `PushQuad` call with the tile's UV coordinates
from the tileset spritesheet. The batch renderer handles texture state
deduplication automatically.

#### Collision

```lua
-- Query what tile is at a world position
map:tile_at(x, y) -> tile_id
map:is_solid(x, y) -> bool

-- Convert between world and tile coordinates
map:world_to_tile(x, y) -> tile_x, tile_y
map:tile_to_world(tile_x, tile_y) -> x, y

-- Move an entity with tile collision (high_impact style)
-- Returns the final position and collision info.
local nx, ny, hit = map:move(x, y, w, h, vx, vy)
-- hit: { normal_x, normal_y, tile_x, tile_y, tile_id } or nil
```

The `map:move` function is the core platformer primitive. It:
1. Takes a position, size, and velocity
2. Sweeps the AABB against the collision layer grid
3. Resolves collisions (slide along solid edges)
4. Returns the final position and collision info

This is separate from the `G.collision` world. Tile collision uses direct
grid lookups (O(1) per tile check) rather than spatial hash queries. For
games that need both tile collision and entity-entity collision, use both:
```lua
-- Tile collision for level geometry
local nx, ny = map:move(x, y, w, h, vx * dt, vy * dt)
-- Entity collision for other objects
world:set_position(handle, nx, ny)
local overlaps = world:get_overlaps(handle)
```

#### Layer parallax

```lua
map:set_parallax("background", 0.5, 0.5)  -- scrolls at half speed
map:set_parallax("clouds", 0.2, 0.0)      -- slow horizontal, no vertical
```

Parallax layers offset by `(camera_x * (1 - parallax_x), camera_y * (1 - parallax_y))`
before computing visible tiles. The camera itself doesn't change.

#### Tile properties

```lua
-- Set per-tile-ID properties (applied to all instances of that tile)
map:set_tile_property(tile_id, "one_way", true)
map:set_tile_property(tile_id, "friction", 0.1)  -- ice
map:set_tile_property(tile_id, "damage", 10)      -- lava

-- Query in collision handler
local props = map:get_tile_properties(tile_id)
if props.one_way and vy < 0 then
  -- falling through one-way platform from below
end
```

### Tiled format support

The Tiled editor exports `.tmj` (JSON) and `.tmx` (XML). We support `.tmj`
because we already vendor yyjson and JSON is simpler to parse.

A `.tmj` file contains:
```json
{
  "width": 40, "height": 30,
  "tilewidth": 16, "tileheight": 16,
  "tilesets": [{ "firstgid": 1, "source": "tiles.tsj" }],
  "layers": [
    {
      "name": "background",
      "type": "tilelayer",
      "data": [0, 0, 1, 2, 3, ...],
      "width": 40, "height": 30
    },
    {
      "name": "collision",
      "type": "tilelayer",
      "data": [0, 1, 1, 1, 0, ...],
      "width": 40, "height": 30
    },
    {
      "name": "objects",
      "type": "objectgroup",
      "objects": [
        { "name": "player_spawn", "x": 100, "y": 200 },
        { "name": "enemy_spawn", "x": 400, "y": 200, "type": "goblin" }
      ]
    }
  ]
}
```

The loader:
1. Parses the JSON with yyjson
2. Loads the referenced tileset spritesheet (must be in the asset database)
3. Creates tile layers as flat integer arrays
4. Extracts object layers as Lua tables (spawn points, trigger zones)
5. Returns a tilemap handle with methods

Object layers are returned as plain Lua tables, not engine objects. The game
decides what to do with spawn points and trigger zones.

### Tile collision algorithm

The `map:move` function implements an AABB sweep against the tile grid:

```
function move(x, y, w, h, vx, vy):
  -- Resolve X axis first
  new_x = x + vx
  for each tile overlapping the AABB at (new_x, y, w, h):
    if tile is solid:
      if vx > 0: new_x = tile_left - w    (snap to left edge)
      if vx < 0: new_x = tile_right       (snap to right edge)
      hit_x = true

  -- Then resolve Y axis
  new_y = y + vy
  for each tile overlapping the AABB at (new_x, new_y, w, h):
    if tile is solid:
      if vy > 0: new_y = tile_top - h     (snap to top edge, landed)
      if vy < 0: new_y = tile_bottom      (snap to bottom edge, bumped ceiling)
      hit_y = true

  return new_x, new_y, { hit_x, hit_y, ... }
```

Resolving axes independently (X then Y) is the standard approach used by
high_impact, STI, and most 2D platformer engines. It prevents corner-case
tunneling that occurs with simultaneous resolution.

### Implementation options

**Option A: Pure Lua** (like STI for Love2D)
- Tilemap data and rendering logic entirely in Lua
- Uses `G.graphics.draw_sprite` or raw `draw_rect` with UV coords per tile
- Collision via grid lookups in a Lua table
- Pro: No C++ changes, hot-reloadable, easy to prototype
- Con: Slower for large maps (many Lua->C++ calls per frame), no engine-level
  optimization

**Option B: C++ tilemap with Lua API**
- Tilemap stored as a C++ object (`DynArray<int>` grids)
- Rendering: single C++ function iterates visible tiles, emits `PushQuad` calls
- Collision: C++ sweep against the grid
- Pro: Fast, engine-level frustum culling, minimal per-frame Lua overhead
- Con: More C++ code, less flexible for experimentation

**Option C: Hybrid** (recommended)
- Tile data in C++ (fast grid storage and queries)
- Rendering in C++ (viewport culling + batch quad submission)
- Collision in C++ (AABB sweep against grid)
- Tiled format loading in C++ (yyjson is already C)
- Tile properties and game logic in Lua (flexible, hot-reloadable)

Option C gives the best performance for the hot path (rendering and collision)
while keeping game-specific logic in Lua.

## Implementation plan

### Phase 1: Core tilemap (minimum viable)

**C++ changes**:
- `tilemap.h/cc`: `Tilemap` class with layers, tile grids, tileset reference
- `tilemap.h/cc`: `TilemapDraw` function that iterates visible tiles and emits
  `PushQuad` calls via the batch renderer
- `tilemap.h/cc`: `TilemapMove` function for AABB sweep collision
- `lua_tilemap.cc`: Lua bindings for `G.tilemap.new`, `G.tilemap.load`,
  `map:draw`, `map:move`, `map:tile_at`, `map:is_solid`, tile get/set

**Lua API surface** (Phase 1):
```lua
G.tilemap.new(config) -> tilemap
G.tilemap.load(filename) -> tilemap

map:add_layer(name, width, height [, options])
map:set_tile(layer, x, y, tile_id)
map:get_tile(layer, x, y) -> tile_id
map:draw()
map:draw_layer(name)
map:tile_at(x, y) -> tile_id
map:is_solid(x, y) -> bool
map:world_to_tile(x, y) -> tile_x, tile_y
map:tile_to_world(tile_x, tile_y) -> x, y
map:move(x, y, w, h, vx, vy) -> nx, ny, hit?
```

### Phase 2: Tiled import + object layers

- TMJ (Tiled JSON) loader using yyjson
- Object layer extraction to Lua tables
- Tileset `.tsj` loading
- `map:get_objects(layer_name) -> [{name, type, x, y, width, height, properties}]`

### Phase 3: Polish

- Parallax factors per layer
- Animated tiles (frame list + duration per tile ID)
- Tile properties (one-way platforms, per-tile friction/damage)
- `map:fill_rect`, `map:clear_layer` convenience functions

### Phase 4: Advanced (if needed)

- Slope tiles (high_impact style, precomputed height functions)
- Isometric tile layout
- Tile flip/rotate flags (Tiled uses bit flags in tile IDs)

## Test game

A simple platformer that exercises the tilemap system:

- **Terrain**: Multi-layer tilemap (background decor + collision + foreground)
- **Player**: AABB character using `map:move` for collision
- **Gravity**: Applied each frame, `map:move` handles landing
- **Camera**: Follows player with bounds set to map dimensions
- **Collectibles**: Object layer spawn points for coins/items
- **One-way platforms**: Tile property that allows jumping through from below

## Decisions

1. **JSON over XML for Tiled import.** Both are supported by Tiled. JSON is
   simpler to parse and we already vendor yyjson. TMX (XML) can be added
   later if needed.

2. **Separate collision grid, not per-visual-tile.** Following high_impact:
   the collision layer is logically separate from visual layers. A decorative
   flower tile and a solid ground tile can use the same visual tile ID but
   different collision behavior.

3. **AABB sweep, not spatial hash, for tile collision.** Grid lookups are O(1)
   per tile. The spatial hash is better for entity-entity collision where
   positions are arbitrary. Both systems coexist.

4. **C++ core, Lua game logic.** Rendering and collision are hot-path and
   belong in C++. Tile properties, spawn logic, and game rules stay in Lua
   for flexibility.

5. **No isometric/hex in Phase 1.** Orthogonal tilemaps cover platformers,
   RPGs, and top-down games. Isometric and hex layouts are niche and can be
   added later without changing the core data model.
