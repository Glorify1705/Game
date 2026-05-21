#pragma once
#ifndef _GAME_TILEMAP_H
#define _GAME_TILEMAP_H

#include <cstdint>
#include <string_view>

#include "allocators.h"
#include "error.h"
#include "vec.h"

namespace G {

class BatchRenderer;
class Camera;
class Renderer;

// A single layer in a tilemap. Stores a grid of tile IDs (0 = empty).
struct TilemapLayer {
  char name[64];     // Layer name for lookup.
  int* tiles;        // Flat array of tile IDs (row-major, width * height).
  int width;         // Width in tiles.
  int height;        // Height in tiles.
  float parallax_x;  // Horizontal parallax factor (1.0 = normal scroll).
  float parallax_y;  // Vertical parallax factor (1.0 = normal scroll).
  bool visible;      // Whether this layer is drawn.
  bool collision;    // Whether non-zero tiles are solid.
};

// A property attached to a TMX object. Supports string, int, float, bool.
struct TilemapProperty {
  enum Type : uint8_t { kString, kInt, kFloat, kBool };
  char name[64];
  Type type;
  union {
    char string_value[128];
    int int_value;
    float float_value;
    bool bool_value;
  };
};

// An object from a TMX object layer (point, rectangle, etc.).
struct TilemapObject {
  static constexpr int kMaxProperties = 16;
  int id;         // TMX object id.
  char name[64];  // Object name (may be empty).
  char type[64];  // Object type/class (may be empty).
  float x;        // X position in pixels.
  float y;        // Y position in pixels.
  float width;    // Width in pixels (0 for points).
  float height;   // Height in pixels (0 for points).
  TilemapProperty properties[kMaxProperties];
  int property_count;
};

// A group of objects from a TMX objectgroup layer.
struct TilemapObjectGroup {
  static constexpr int kMaxObjects = 256;
  char name[64];
  TilemapObject* objects;  // Array of objects (allocated).
  int object_count;
};

// Result of a tilemap move (AABB sweep against solid tiles).
struct TilemapMoveResult {
  float x;         // Final x position after collision resolution.
  float y;         // Final y position after collision resolution.
  bool hit_x;      // True if horizontal collision occurred.
  bool hit_y;      // True if vertical collision occurred.
  float normal_x;  // Collision normal x component.
  float normal_y;  // Collision normal y component.
  int tile_x;      // Tile coordinate of last collision (x).
  int tile_y;      // Tile coordinate of last collision (y).
  int tile_id;     // Tile ID of last collision.
};

// A 2D tilemap with multiple layers, tile collision, and camera-aware
// rendering.
class Tilemap {
 public:
  static constexpr int kMaxLayers = 16;
  static constexpr int kMaxObjectGroups = 16;

  // Creates an empty tilemap with the given tile dimensions.
  Tilemap(int tile_width, int tile_height, Allocator* allocator);

  // Loads a tilemap from TMX (Tiled XML) data into an existing Tilemap.
  // The tileset_gid_offset is subtracted from each tile GID to convert Tiled
  // GIDs to 1-based tile IDs. The output tilemap is destructed and
  // reconstructed in-place.
  static ErrorOr<void> LoadTmx(std::string_view xml_data,
                               int tileset_gid_offset, Allocator* allocator,
                               Tilemap* out);

  // Destroys the tilemap and frees all layer tile arrays.
  ~Tilemap();

  // Adds a new layer with the given name and dimensions. Returns the layer
  // index, or -1 if the maximum layer count is reached.
  int AddLayer(std::string_view name, int width, int height, bool collision);

  // Finds a layer by name. Returns nullptr if not found.
  TilemapLayer* FindLayer(std::string_view name);

  // Finds a layer by name (const). Returns nullptr if not found.
  const TilemapLayer* FindLayer(std::string_view name) const;

  // Sets a tile ID at the given tile coordinates in the named layer.
  void SetTile(std::string_view layer_name, int x, int y, int tile_id);

  // Gets the tile ID at the given tile coordinates in the named layer.
  int GetTile(std::string_view layer_name, int x, int y) const;

  // Converts world coordinates to tile coordinates.
  void WorldToTile(float wx, float wy, int* tx, int* ty) const;

  // Converts tile coordinates to world coordinates (top-left of tile).
  void TileToWorld(int tx, int ty, float* wx, float* wy) const;

  // Returns true if the world position overlaps a solid tile.
  bool IsSolid(float wx, float wy) const;

  // Returns the tile ID at a world position from the first collision layer.
  int TileAt(float wx, float wy) const;

  // Moves an AABB through the tilemap with collision resolution.
  TilemapMoveResult Move(float x, float y, float w, float h, float vx,
                         float vy) const;

  // Draws all visible layers using the camera for viewport culling.
  void Draw(Renderer* renderer, BatchRenderer* batch, Camera* camera) const;

  // Draws a single layer by name.
  void DrawLayer(std::string_view name, Renderer* renderer,
                 BatchRenderer* batch, Camera* camera) const;

  // Sets the tileset spritesheet name.
  void SetTileset(std::string_view name);

  // Returns the tileset spritesheet name.
  std::string_view tileset() const { return std::string_view(tileset_name_); }

  // Returns the tile width in pixels.
  int tile_width() const { return tile_width_; }

  // Returns the tile height in pixels.
  int tile_height() const { return tile_height_; }

  // Returns the number of layers.
  int layer_count() const { return layer_count_; }

  // Returns the layer at the given index.
  TilemapLayer* layer(int index) { return &layers_[index]; }

  // Returns the layer at the given index (const).
  const TilemapLayer* layer(int index) const { return &layers_[index]; }

  // Finds an object group by name. Returns nullptr if not found.
  const TilemapObjectGroup* FindObjectGroup(std::string_view name) const;

  // Returns the number of object groups.
  int object_group_count() const { return object_group_count_; }

  // Returns the object group at the given index.
  const TilemapObjectGroup* object_group(int index) const {
    return &object_groups_[index];
  }

  // Active tilemap for debug UI inspection (set by Lua bindings).
  static inline Tilemap* debug_active_tilemap = nullptr;

 private:
  // Draws a single layer with viewport culling.
  void DrawLayerImpl(const TilemapLayer& layer, Renderer* renderer,
                     BatchRenderer* batch, Camera* camera) const;

  // Finds the first collision layer. Returns nullptr if none.
  const TilemapLayer* FindCollisionLayer() const;

  int tile_width_;                   // Tile width in pixels.
  int tile_height_;                  // Tile height in pixels.
  char tileset_name_[256];           // Spritesheet name.
  TilemapLayer layers_[kMaxLayers];  // Layer storage.
  int layer_count_;                  // Number of active layers.
  TilemapObjectGroup object_groups_[kMaxObjectGroups];  // Object groups.
  int object_group_count_;  // Number of object groups.
  Allocator* allocator_;    // Allocator for arrays.
};

}  // namespace G

#endif  // _GAME_TILEMAP_H
