#include "lua_tilemap.h"

#include "camera.h"
#include "renderer.h"
#include "tilemap.h"

namespace G {
namespace {

Tilemap* CheckTilemap(lua_State* state, int index) {
  return static_cast<Tilemap*>(luaL_checkudata(state, index, "tilemap"));
}

int TilemapNew(lua_State* state) {
  luaL_checktype(state, 1, LUA_TTABLE);

  lua_getfield(state, 1, "tile_width");
  int tile_width = luaL_checkinteger(state, -1);
  lua_pop(state, 1);

  lua_getfield(state, 1, "tile_height");
  int tile_height = luaL_checkinteger(state, -1);
  lua_pop(state, 1);

  auto* allocator = Registry<Lua>::Retrieve(state)->allocator();

  auto* tilemap =
      static_cast<Tilemap*>(lua_newuserdata(state, sizeof(Tilemap)));
  new (tilemap) Tilemap(tile_width, tile_height, allocator);

  lua_getfield(state, 1, "tileset");
  if (lua_isstring(state, -1)) {
    size_t len;
    const char* name = lua_tolstring(state, -1, &len);
    tilemap->SetTileset(std::string_view(name, len));
  }
  lua_pop(state, 1);

  luaL_getmetatable(state, "tilemap");
  lua_setmetatable(state, -2);
  return 1;
}

int TilemapAddLayer(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  std::string_view name = GetLuaString(state, 2);
  int width = luaL_checkinteger(state, 3);
  int height = luaL_checkinteger(state, 4);

  bool collision = false;
  float parallax_x = 1.0f, parallax_y = 1.0f;
  if (lua_istable(state, 5)) {
    lua_getfield(state, 5, "collision");
    if (!lua_isnil(state, -1)) collision = lua_toboolean(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, 5, "parallax_x");
    if (lua_isnumber(state, -1)) parallax_x = lua_tonumber(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, 5, "parallax_y");
    if (lua_isnumber(state, -1)) parallax_y = lua_tonumber(state, -1);
    lua_pop(state, 1);
  }

  int index = tilemap->AddLayer(name, width, height, collision);
  if (index < 0) {
    LUA_ERROR(state, "tilemap: max layers reached");
  }
  TilemapLayer* layer = tilemap->layer(index);
  layer->parallax_x = parallax_x;
  layer->parallax_y = parallax_y;
  lua_pushinteger(state, index + 1);
  return 1;
}

int TilemapSetTile(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  std::string_view layer = GetLuaString(state, 2);
  int x = luaL_checkinteger(state, 3);
  int y = luaL_checkinteger(state, 4);
  int tile_id = luaL_checkinteger(state, 5);
  tilemap->SetTile(layer, x, y, tile_id);
  return 0;
}

int TilemapGetTile(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  std::string_view layer = GetLuaString(state, 2);
  int x = luaL_checkinteger(state, 3);
  int y = luaL_checkinteger(state, 4);
  lua_pushinteger(state, tilemap->GetTile(layer, x, y));
  return 1;
}

int TilemapDraw(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  auto* renderer = Registry<Renderer>::Retrieve(state);
  auto* batch = Registry<BatchRenderer>::Retrieve(state);
  auto* camera = Registry<Camera>::Retrieve(state);
  tilemap->Draw(renderer, batch, camera);
  return 0;
}

int TilemapDrawLayer(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  std::string_view name = GetLuaString(state, 2);
  auto* renderer = Registry<Renderer>::Retrieve(state);
  auto* batch = Registry<BatchRenderer>::Retrieve(state);
  auto* camera = Registry<Camera>::Retrieve(state);
  tilemap->DrawLayer(name, renderer, batch, camera);
  return 0;
}

int TilemapTileAt(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  float x = luaL_checknumber(state, 2);
  float y = luaL_checknumber(state, 3);
  lua_pushinteger(state, tilemap->TileAt(x, y));
  return 1;
}

int TilemapIsSolid(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  float x = luaL_checknumber(state, 2);
  float y = luaL_checknumber(state, 3);
  lua_pushboolean(state, tilemap->IsSolid(x, y));
  return 1;
}

int TilemapWorldToTile(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  float wx = luaL_checknumber(state, 2);
  float wy = luaL_checknumber(state, 3);
  int tx, ty;
  tilemap->WorldToTile(wx, wy, &tx, &ty);
  lua_pushinteger(state, tx);
  lua_pushinteger(state, ty);
  return 2;
}

int TilemapTileToWorld(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  int tx = luaL_checkinteger(state, 2);
  int ty = luaL_checkinteger(state, 3);
  float wx, wy;
  tilemap->TileToWorld(tx, ty, &wx, &wy);
  lua_pushnumber(state, wx);
  lua_pushnumber(state, wy);
  return 2;
}

int TilemapMove(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  float x = luaL_checknumber(state, 2);
  float y = luaL_checknumber(state, 3);
  float w = luaL_checknumber(state, 4);
  float h = luaL_checknumber(state, 5);
  float vx = luaL_checknumber(state, 6);
  float vy = luaL_checknumber(state, 7);

  TilemapMoveResult result = tilemap->Move(x, y, w, h, vx, vy);

  lua_pushnumber(state, result.x);
  lua_pushnumber(state, result.y);

  if (result.hit_x || result.hit_y) {
    lua_createtable(state, 0, 5);
    lua_pushnumber(state, result.normal_x);
    lua_setfield(state, -2, "normal_x");
    lua_pushnumber(state, result.normal_y);
    lua_setfield(state, -2, "normal_y");
    lua_pushinteger(state, result.tile_x);
    lua_setfield(state, -2, "tile_x");
    lua_pushinteger(state, result.tile_y);
    lua_setfield(state, -2, "tile_y");
    lua_pushinteger(state, result.tile_id);
    lua_setfield(state, -2, "tile_id");
  } else {
    lua_pushnil(state);
  }

  return 3;
}

int TilemapSetParallax(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  std::string_view name = GetLuaString(state, 2);
  float px = luaL_checknumber(state, 3);
  float py = luaL_checknumber(state, 4);

  TilemapLayer* layer = tilemap->FindLayer(name);
  if (!layer) {
    LUA_ERROR(state, "tilemap: layer not found");
  }
  layer->parallax_x = px;
  layer->parallax_y = py;
  return 0;
}

int TilemapSetVisible(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  std::string_view name = GetLuaString(state, 2);
  bool visible = lua_toboolean(state, 3);

  TilemapLayer* layer = tilemap->FindLayer(name);
  if (!layer) {
    LUA_ERROR(state, "tilemap: layer not found");
  }
  layer->visible = visible;
  return 0;
}

int TilemapDimensions(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  lua_pushinteger(state, tilemap->tile_width());
  lua_pushinteger(state, tilemap->tile_height());
  lua_pushinteger(state, tilemap->layer_count());
  return 3;
}

int TilemapLayerCount(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  lua_pushinteger(state, tilemap->layer_count());
  return 1;
}

int TilemapGc(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  tilemap->~Tilemap();
  return 0;
}

int TilemapToString(lua_State* state) {
  auto* tilemap = CheckTilemap(state, 1);
  lua_pushfstring(state, "tilemap(%dx%d, %d layers)", tilemap->tile_width(),
                  tilemap->tile_height(), tilemap->layer_count());
  return 1;
}

constexpr luaL_Reg kTilemapMethods[] = {
    {"add_layer", TilemapAddLayer},
    {"set_tile", TilemapSetTile},
    {"get_tile", TilemapGetTile},
    {"draw", TilemapDraw},
    {"draw_layer", TilemapDrawLayer},
    {"tile_at", TilemapTileAt},
    {"is_solid", TilemapIsSolid},
    {"world_to_tile", TilemapWorldToTile},
    {"tile_to_world", TilemapTileToWorld},
    {"move", TilemapMove},
    {"set_parallax", TilemapSetParallax},
    {"set_visible", TilemapSetVisible},
    {"dimensions", TilemapDimensions},
    {"layer_count", TilemapLayerCount},
    {"__gc", TilemapGc},
    {"__tostring", TilemapToString},
};

const LuaApiFunction kTilemapLib[] = {
    {"new",
     "Creates a new tilemap",
     {{"config",
       "Config table with tile_width, tile_height, and optional tileset fields",
       "table"}},
     {{"tilemap", "The new tilemap", "tilemap"}},
     TilemapNew},
};

const LuaUserdataMethod kTilemapMethodDefs[] = {
    {"add_layer",
     "Adds a new layer to the tilemap",
     {{"name", "Layer name", "string"},
      {"width", "Width in tiles", "integer"},
      {"height", "Height in tiles", "integer"},
      {"opts", "Options table with optional collision=true", "table?"}},
     {{"index", "Layer index (1-based)", "integer"}}},
    {"set_tile",
     "Sets a tile ID at the given coordinates in a layer",
     {{"layer", "Layer name", "string"},
      {"x", "Tile x coordinate", "integer"},
      {"y", "Tile y coordinate", "integer"},
      {"tile_id", "Tile ID (0 = empty)", "integer"}},
     {}},
    {"get_tile",
     "Gets the tile ID at the given coordinates in a layer",
     {{"layer", "Layer name", "string"},
      {"x", "Tile x coordinate", "integer"},
      {"y", "Tile y coordinate", "integer"}},
     {{"tile_id", "Tile ID (0 = empty)", "integer"}}},
    {"draw",
     "Draws all visible layers with camera-based viewport culling",
     {},
     {}},
    {"draw_layer",
     "Draws a single layer by name",
     {{"name", "Layer name", "string"}},
     {}},
    {"tile_at",
     "Returns the tile ID at a world position from the collision layer",
     {{"x", "World x position", "number"},
      {"y", "World y position", "number"}},
     {{"tile_id", "Tile ID or 0", "integer"}}},
    {"is_solid",
     "Returns true if the world position overlaps a solid tile",
     {{"x", "World x position", "number"},
      {"y", "World y position", "number"}},
     {{"solid", "Whether the tile is solid", "boolean"}}},
    {"world_to_tile",
     "Converts world coordinates to tile coordinates",
     {{"x", "World x position", "number"},
      {"y", "World y position", "number"}},
     {{"tx", "Tile x coordinate", "integer"},
      {"ty", "Tile y coordinate", "integer"}}},
    {"tile_to_world",
     "Converts tile coordinates to world coordinates (top-left corner)",
     {{"tx", "Tile x coordinate", "integer"},
      {"ty", "Tile y coordinate", "integer"}},
     {{"x", "World x position", "number"},
      {"y", "World y position", "number"}}},
    {"move",
     "Moves an AABB through the tilemap with collision resolution",
     {{"x", "Current x position", "number"},
      {"y", "Current y position", "number"},
      {"w", "AABB width", "number"},
      {"h", "AABB height", "number"},
      {"vx", "X velocity", "number"},
      {"vy", "Y velocity", "number"}},
     {{"nx", "Final x position", "number"},
      {"ny", "Final y position", "number"},
      {"hit", "Collision info or nil", "table?"}}},
    {"set_parallax",
     "Sets the parallax scroll factor for a layer",
     {{"name", "Layer name", "string"},
      {"px", "Horizontal parallax (1.0 = normal)", "number"},
      {"py", "Vertical parallax (1.0 = normal)", "number"}},
     {}},
    {"set_visible",
     "Sets whether a layer is visible",
     {{"name", "Layer name", "string"},
      {"visible", "Whether to draw this layer", "boolean"}},
     {}},
    {"dimensions",
     "Returns tile dimensions and layer count",
     {},
     {{"tile_width", "Tile width in pixels", "integer"},
      {"tile_height", "Tile height in pixels", "integer"},
      {"layer_count", "Number of layers", "integer"}}},
    {"layer_count",
     "Returns the number of layers",
     {},
     {{"count", "Number of layers", "integer"}}},
};

}  // namespace

void AddTilemapLibrary(Lua* lua) {
  LOAD_METATABLE(lua, "tilemap", kTilemapMethods);
  lua->AddLibrary("tilemap", kTilemapLib);
  lua->RegisterUserdataType(
      {"tilemap", "tilemap", "A 2D tilemap with layers and tile collision",
       nullptr, 0, kTilemapMethodDefs, std::size(kTilemapMethodDefs), nullptr,
       0});
}

LuaLibraryDef GetTilemapLibraryDef() {
  static const LuaLibraryDef::Library kLibs[] = {
      {"tilemap", kTilemapLib, std::size(kTilemapLib)},
  };
  static const LuaUserdataType kTypes[] = {
      {"tilemap", "tilemap", "A 2D tilemap with layers and tile collision",
       nullptr, 0, kTilemapMethodDefs, std::size(kTilemapMethodDefs), nullptr,
       0},
  };
  return {kLibs, std::size(kLibs), kTypes, std::size(kTypes)};
}

}  // namespace G
