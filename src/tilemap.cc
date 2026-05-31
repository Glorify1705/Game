#include "tilemap.h"

#include <cmath>
#include <cstring>
#include <utility>

#include "camera.h"
#include "renderer.h"
#include "xml.h"

namespace G {
namespace {

// Clamps an integer to the range [lo, hi].
int Clamp(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

}  // namespace

Tilemap::Tilemap(int tile_width, int tile_height, Allocator* allocator)
    : tile_width_(tile_width),
      tile_height_(tile_height),
      layer_count_(0),
      object_group_count_(0),
      allocator_(allocator) {
  tileset_name_[0] = '\0';
  std::memset(layers_, 0, sizeof(layers_));
  std::memset(object_groups_, 0, sizeof(object_groups_));
}

ErrorOr<void> Tilemap::LoadTmx(std::string_view xml_data,
                               int tileset_gid_offset, Allocator* allocator,
                               Tilemap* tilemap) {
  ArenaAllocator scratch(allocator, Kilobytes(64));
  XmlElement* root = TRY(ParseXml(xml_data, &scratch));
  if (root->tag != "map") {
    return Error::Message("TMX: expected <map> root element");
  }

  int map_w = root->AttrInt("width");
  int map_h = root->AttrInt("height");
  int tw = root->AttrInt("tilewidth");
  int th = root->AttrInt("tileheight");
  if (map_w <= 0 || map_h <= 0 || tw <= 0 || th <= 0) {
    return Error::Message("TMX: invalid map dimensions");
  }

  // Re-initialize the tilemap with correct dimensions.
  tilemap->~Tilemap();
  new (tilemap) Tilemap(tw, th, allocator);

  root->ForEachChild("layer", [&](const XmlElement& layer_elem) {
    std::string_view name = layer_elem.Attr("name");
    int lw = layer_elem.AttrInt("width");
    int lh = layer_elem.AttrInt("height");
    if (lw <= 0) lw = map_w;
    if (lh <= 0) lh = map_h;

    int index = tilemap->AddLayer(name, lw, lh, /*collision=*/false);
    if (index < 0) return;

    // Parse CSV tile data from <data encoding="csv"> text content.
    layer_elem.ForEachChild("data", [&](const XmlElement& data_elem) {
      std::string_view csv = data_elem.text;
      int x = 0, y = 0;
      size_t pos = 0;
      while (pos < csv.size() && y < lh) {
        // Skip whitespace and commas.
        while (pos < csv.size() &&
               (csv[pos] == ',' || csv[pos] == ' ' || csv[pos] == '\n' ||
                csv[pos] == '\r' || csv[pos] == '\t')) {
          ++pos;
        }
        if (pos >= csv.size()) break;
        // Parse integer as unsigned to avoid overflow on flipped tile GIDs.
        uint32_t gid = 0;
        while (pos < csv.size() && csv[pos] >= '0' && csv[pos] <= '9') {
          gid = gid * 10 + static_cast<uint32_t>(csv[pos] - '0');
          ++pos;
        }
        // Extract Tiled flip flags (top 3 bits) before stripping.
        uint32_t flip = gid & kTileFlipMask;
        gid &= kTileIdMask;
        // Convert Tiled GID to our 1-based tile ID.
        // gid_offset = firstgid - 1, so gid - offset = 1 for the first tile.
        int tile_id = (gid > static_cast<uint32_t>(tileset_gid_offset))
                          ? static_cast<int>(gid) - tileset_gid_offset
                          : 0;
        tile_id |= static_cast<int>(flip);
        tilemap->SetTile(name, x, y, tile_id);
        ++x;
        if (x >= lw) {
          x = 0;
          ++y;
        }
      }
    });
  });

  // Parse object groups (<objectgroup> elements).
  root->ForEachChild("objectgroup", [&](const XmlElement& group_elem) {
    if (tilemap->object_group_count_ >= kMaxObjectGroups) return;
    TilemapObjectGroup& group =
        tilemap->object_groups_[tilemap->object_group_count_];

    std::string_view gname = group_elem.Attr("name");
    size_t name_len = gname.size() < 63 ? gname.size() : 63;
    std::memcpy(group.name, gname.data(), name_len);
    group.name[name_len] = '\0';

    // Count objects first, then allocate.
    int count = 0;
    group_elem.ForEachChild("object", [&](const XmlElement&) { ++count; });
    if (count == 0) {
      group.objects = nullptr;
      group.object_count = 0;
      group.allocated_count = 0;
      tilemap->object_group_count_++;
      return;
    }
    if (count > TilemapObjectGroup::kMaxObjects)
      count = TilemapObjectGroup::kMaxObjects;

    group.objects = static_cast<TilemapObject*>(allocator->Alloc(
        count * sizeof(TilemapObject), alignof(TilemapObject)));
    std::memset(group.objects, 0, count * sizeof(TilemapObject));
    group.object_count = 0;
    group.allocated_count = count;

    group_elem.ForEachChild("object", [&](const XmlElement& obj_elem) {
      if (group.object_count >= count) return;
      TilemapObject& obj = group.objects[group.object_count];

      obj.id = obj_elem.AttrInt("id");
      obj.x = obj_elem.AttrFloat("x");
      obj.y = obj_elem.AttrFloat("y");
      obj.width = obj_elem.AttrFloat("width");
      obj.height = obj_elem.AttrFloat("height");

      std::string_view oname = obj_elem.Attr("name");
      size_t on = oname.size() < 63 ? oname.size() : 63;
      std::memcpy(obj.name, oname.data(), on);
      obj.name[on] = '\0';

      std::string_view otype = obj_elem.Attr("type");
      if (otype.empty()) otype = obj_elem.Attr("class");
      size_t ot = otype.size() < 63 ? otype.size() : 63;
      std::memcpy(obj.type, otype.data(), ot);
      obj.type[ot] = '\0';

      // Parse custom properties.
      obj.property_count = 0;
      obj_elem.ForEachChild("properties", [&](const XmlElement& props_elem) {
        props_elem.ForEachChild("property", [&](const XmlElement& prop_elem) {
          if (obj.property_count >= TilemapObject::kMaxProperties) return;
          TilemapProperty& prop = obj.properties[obj.property_count];

          std::string_view pname = prop_elem.Attr("name");
          size_t pn = pname.size() < 63 ? pname.size() : 63;
          std::memcpy(prop.name, pname.data(), pn);
          prop.name[pn] = '\0';

          std::string_view ptype = prop_elem.Attr("type");
          std::string_view pval = prop_elem.Attr("value");

          if (ptype == "int") {
            prop.type = TilemapProperty::kInt;
            prop.int_value = 0;
            size_t k = 0;
            bool negative = k < pval.size() && pval[k] == '-';
            if (negative) ++k;
            for (; k < pval.size(); ++k) {
              if (pval[k] >= '0' && pval[k] <= '9')
                prop.int_value = prop.int_value * 10 + (pval[k] - '0');
            }
            if (negative) prop.int_value = -prop.int_value;
          } else if (ptype == "float") {
            prop.type = TilemapProperty::kFloat;
            prop.float_value = prop_elem.AttrFloat("value");
          } else if (ptype == "bool") {
            prop.type = TilemapProperty::kBool;
            prop.bool_value = (pval == "true");
          } else {
            prop.type = TilemapProperty::kString;
            size_t sv = pval.size() < 127 ? pval.size() : 127;
            std::memcpy(prop.string_value, pval.data(), sv);
            prop.string_value[sv] = '\0';
          }
          obj.property_count++;
        });
      });

      group.object_count++;
    });

    tilemap->object_group_count_++;
  });

  return {};
}

const TilemapObjectGroup* Tilemap::FindObjectGroup(
    std::string_view name) const {
  for (int i = 0; i < object_group_count_; ++i) {
    if (name == object_groups_[i].name) return &object_groups_[i];
  }
  return nullptr;
}

Tilemap::~Tilemap() {
  for (int i = 0; i < layer_count_; ++i) {
    if (layers_[i].tiles) {
      allocator_->Dealloc(layers_[i].tiles,
                          layers_[i].width * layers_[i].height * sizeof(int));
      layers_[i].tiles = nullptr;
    }
  }
  for (int i = 0; i < object_group_count_; ++i) {
    if (object_groups_[i].objects) {
      allocator_->Dealloc(
          object_groups_[i].objects,
          object_groups_[i].allocated_count * sizeof(TilemapObject));
      object_groups_[i].objects = nullptr;
    }
  }
}

int Tilemap::AddLayer(std::string_view name, int width, int height,
                      bool collision) {
  if (layer_count_ >= kMaxLayers) return -1;
  if (width <= 0 || height <= 0) return -1;

  TilemapLayer& layer = layers_[layer_count_];
  size_t copy_len = name.size() < 63 ? name.size() : 63;
  std::memcpy(layer.name, name.data(), copy_len);
  layer.name[copy_len] = '\0';

  size_t tile_count = static_cast<size_t>(width) * height;
  layer.tiles = static_cast<int*>(
      allocator_->Alloc(tile_count * sizeof(int), alignof(int)));
  std::memset(layer.tiles, 0, tile_count * sizeof(int));

  layer.width = width;
  layer.height = height;
  layer.parallax_x = 1.0f;
  layer.parallax_y = 1.0f;
  layer.visible = true;
  layer.collision = collision;

  return layer_count_++;
}

TilemapLayer* Tilemap::FindLayer(std::string_view name) {
  for (int i = 0; i < layer_count_; ++i) {
    if (name == layers_[i].name) return &layers_[i];
  }
  return nullptr;
}

const TilemapLayer* Tilemap::FindLayer(std::string_view name) const {
  for (int i = 0; i < layer_count_; ++i) {
    if (name == layers_[i].name) return &layers_[i];
  }
  return nullptr;
}

void Tilemap::SetTile(std::string_view layer_name, int x, int y, int tile_id) {
  TilemapLayer* layer = FindLayer(layer_name);
  if (!layer) return;
  if (x < 0 || x >= layer->width || y < 0 || y >= layer->height) return;
  layer->tiles[y * layer->width + x] = tile_id;
}

int Tilemap::GetTile(std::string_view layer_name, int x, int y) const {
  const TilemapLayer* layer = FindLayer(layer_name);
  if (!layer) return 0;
  if (x < 0 || x >= layer->width || y < 0 || y >= layer->height) return 0;
  return layer->tiles[y * layer->width + x] & kTileIdMask;
}

void Tilemap::WorldToTile(float wx, float wy, int* tx, int* ty) const {
  *tx = static_cast<int>(std::floor(wx / tile_width_));
  *ty = static_cast<int>(std::floor(wy / tile_height_));
}

void Tilemap::TileToWorld(int tx, int ty, float* wx, float* wy) const {
  *wx = static_cast<float>(tx * tile_width_);
  *wy = static_cast<float>(ty * tile_height_);
}

const TilemapLayer* Tilemap::FindCollisionLayer() const {
  for (int i = 0; i < layer_count_; ++i) {
    if (layers_[i].collision) return &layers_[i];
  }
  return nullptr;
}

bool Tilemap::IsSolid(float wx, float wy) const {
  const TilemapLayer* col = FindCollisionLayer();
  if (!col) return false;
  int tx = static_cast<int>(std::floor(wx / tile_width_));
  int ty = static_cast<int>(std::floor(wy / tile_height_));
  if (tx < 0 || tx >= col->width || ty < 0 || ty >= col->height) return false;
  return (col->tiles[ty * col->width + tx] & kTileIdMask) != 0;
}

int Tilemap::TileAt(float wx, float wy) const {
  const TilemapLayer* col = FindCollisionLayer();
  if (!col) return 0;
  int tx = static_cast<int>(std::floor(wx / tile_width_));
  int ty = static_cast<int>(std::floor(wy / tile_height_));
  if (tx < 0 || tx >= col->width || ty < 0 || ty >= col->height) return 0;
  return col->tiles[ty * col->width + tx] & kTileIdMask;
}

TilemapMoveResult Tilemap::Move(float x, float y, float w, float h, float vx,
                                float vy) const {
  TilemapMoveResult result = {};
  result.x = x;
  result.y = y;

  const TilemapLayer* col = FindCollisionLayer();
  if (!col) {
    result.x = x + vx;
    result.y = y + vy;
    return result;
  }

  const float tw = static_cast<float>(tile_width_);
  const float th = static_cast<float>(tile_height_);

  // Resolve X axis.
  float new_x = x + vx;
  {
    // Compute the tile range overlapping the AABB at (new_x, y, w, h).
    int start_col = static_cast<int>(std::floor(new_x / tw));
    int end_col = static_cast<int>(std::ceil((new_x + w) / tw)) - 1;
    int start_row = static_cast<int>(std::floor(y / th));
    int end_row = static_cast<int>(std::ceil((y + h) / th)) - 1;

    start_col = Clamp(start_col, 0, col->width - 1);
    end_col = Clamp(end_col, 0, col->width - 1);
    start_row = Clamp(start_row, 0, col->height - 1);
    end_row = Clamp(end_row, 0, col->height - 1);

    for (int ty = start_row; ty <= end_row; ++ty) {
      for (int tx = start_col; tx <= end_col; ++tx) {
        int raw = col->tiles[ty * col->width + tx];
        int tile_id = raw & kTileIdMask;
        if (tile_id == 0) continue;

        float tile_left = tx * tw;
        float tile_right = tile_left + tw;

        // Check AABB overlap.
        if (new_x < tile_right && new_x + w > tile_left && y < ty * th + th &&
            y + h > ty * th) {
          if (vx > 0) {
            new_x = tile_left - w;
            result.normal_x = -1.0f;
          } else if (vx < 0) {
            new_x = tile_right;
            result.normal_x = 1.0f;
          }
          result.hit_x = true;
          result.tile_x = tx;
          result.tile_y = ty;
          result.tile_id = tile_id;
        }
      }
    }
  }

  // Resolve Y axis using the corrected X position.
  float new_y = y + vy;
  {
    int start_col = static_cast<int>(std::floor(new_x / tw));
    int end_col = static_cast<int>(std::ceil((new_x + w) / tw)) - 1;
    int start_row = static_cast<int>(std::floor(new_y / th));
    int end_row = static_cast<int>(std::ceil((new_y + h) / th)) - 1;

    start_col = Clamp(start_col, 0, col->width - 1);
    end_col = Clamp(end_col, 0, col->width - 1);
    start_row = Clamp(start_row, 0, col->height - 1);
    end_row = Clamp(end_row, 0, col->height - 1);

    for (int ty = start_row; ty <= end_row; ++ty) {
      for (int tx = start_col; tx <= end_col; ++tx) {
        int raw = col->tiles[ty * col->width + tx];
        int tile_id = raw & kTileIdMask;
        if (tile_id == 0) continue;

        float tile_top = ty * th;
        float tile_bottom = tile_top + th;

        if (new_x < tx * tw + tw && new_x + w > tx * tw &&
            new_y < tile_bottom && new_y + h > tile_top) {
          if (vy > 0) {
            new_y = tile_top - h;
            result.normal_y = -1.0f;
          } else if (vy < 0) {
            new_y = tile_bottom;
            result.normal_y = 1.0f;
          }
          result.hit_y = true;
          result.tile_x = tx;
          result.tile_y = ty;
          result.tile_id = tile_id;
        }
      }
    }
  }

  result.x = new_x;
  result.y = new_y;
  return result;
}

void Tilemap::SetTileset(std::string_view name) {
  size_t copy_len = name.size() < 255 ? name.size() : 255;
  std::memcpy(tileset_name_, name.data(), copy_len);
  tileset_name_[copy_len] = '\0';
}

void Tilemap::DrawTile(int tile_id, float x, float y, Renderer* renderer,
                       BatchRenderer* batch) const {
  if (tileset_name_[0] == '\0' || tile_id <= 0) return;

  float sheet_w, sheet_h;
  DbAssets::Spritesheet* sheet = renderer->GetSpritesheet(tileset_name_);
  if (sheet && renderer->SetSpritesheetTexture(tileset_name_)) {
    sheet_w = static_cast<float>(sheet->width);
    sheet_h = static_cast<float>(sheet->height);
  } else {
    DbAssets::Image* img = renderer->GetImage(tileset_name_);
    if (!img || !renderer->SetImageTexture(tileset_name_)) return;
    sheet_w = static_cast<float>(img->width);
    sheet_h = static_cast<float>(img->height);
  }
  const float tw = static_cast<float>(tile_width_);
  const float th = static_cast<float>(tile_height_);
  const int tiles_per_row = static_cast<int>(sheet_w) / tile_width_;
  if (tiles_per_row == 0) return;

  int tile_col = (tile_id - 1) % tiles_per_row;
  int tile_row = (tile_id - 1) / tiles_per_row;

  const float half_texel_u = 0.5f / sheet_w;
  const float half_texel_v = 0.5f / sheet_h;
  float u0 = (tile_col * tw) / sheet_w + half_texel_u;
  float v0 = (tile_row * th) / sheet_h + half_texel_v;
  float u1 = ((tile_col + 1) * tw) / sheet_w - half_texel_u;
  float v1 = ((tile_row + 1) * th) / sheet_h - half_texel_v;

  FVec2 p0(x, y);
  FVec2 p1(x + tw, y + th);
  FVec2 q0(u0, v0);
  FVec2 q1(u1, v1);
  FVec2 origin(x + tw * 0.5f, y + th * 0.5f);
  batch->PushQuad(p0, p1, q0, q1, origin, /*angle=*/0.0f);
}

void Tilemap::Draw(Renderer* renderer, BatchRenderer* batch,
                   Camera* camera) const {
  for (int i = 0; i < layer_count_; ++i) {
    if (layers_[i].visible) {
      DrawLayerImpl(layers_[i], renderer, batch, camera);
    }
  }
}

void Tilemap::DrawLayer(std::string_view name, Renderer* renderer,
                        BatchRenderer* batch, Camera* camera) const {
  const TilemapLayer* layer = FindLayer(name);
  if (layer && layer->visible) {
    DrawLayerImpl(*layer, renderer, batch, camera);
  }
}

void Tilemap::DrawLayerImpl(const TilemapLayer& layer, Renderer* renderer,
                            BatchRenderer* batch, Camera* camera) const {
  if (tileset_name_[0] == '\0') return;

  // Try spritesheet first, then fall back to plain image for the tileset.
  float sheet_w, sheet_h;
  DbAssets::Spritesheet* sheet = renderer->GetSpritesheet(tileset_name_);
  if (sheet && renderer->SetSpritesheetTexture(tileset_name_)) {
    sheet_w = static_cast<float>(sheet->width);
    sheet_h = static_cast<float>(sheet->height);
  } else {
    DbAssets::Image* img = renderer->GetImage(tileset_name_);
    if (!img || !renderer->SetImageTexture(tileset_name_)) return;
    sheet_w = static_cast<float>(img->width);
    sheet_h = static_cast<float>(img->height);
  }
  const float tw = static_cast<float>(tile_width_);
  const float th = static_cast<float>(tile_height_);
  const int tiles_per_row = static_cast<int>(sheet_w) / tile_width_;
  if (tiles_per_row == 0) return;

  // Compute the camera's visible world-space area for culling.
  IVec2 viewport = batch->GetViewport();
  FVec2 cam_pos = camera->GetPosition();
  float zoom = camera->GetZoom();

  // Apply parallax offset: the layer scrolls at a fraction of camera movement.
  float parallax_offset_x = cam_pos.x * (1.0f - layer.parallax_x);
  float parallax_offset_y = cam_pos.y * (1.0f - layer.parallax_y);

  // The effective camera position for this layer after parallax.
  float eff_cam_x = cam_pos.x - parallax_offset_x;
  float eff_cam_y = cam_pos.y - parallax_offset_y;

  // Visible world-space rectangle (camera center +/- half viewport/zoom).
  float half_vw = (viewport.x / zoom) * 0.5f;
  float half_vh = (viewport.y / zoom) * 0.5f;
  float view_left = eff_cam_x - half_vw;
  float view_top = eff_cam_y - half_vh;
  float view_right = eff_cam_x + half_vw;
  float view_bottom = eff_cam_y + half_vh;

  // Convert to tile range with one tile of padding.
  int start_col = static_cast<int>(std::floor(view_left / tw)) - 1;
  int end_col = static_cast<int>(std::ceil(view_right / tw)) + 1;
  int start_row = static_cast<int>(std::floor(view_top / th)) - 1;
  int end_row = static_cast<int>(std::ceil(view_bottom / th)) + 1;

  start_col = Clamp(start_col, 0, layer.width - 1);
  end_col = Clamp(end_col, 0, layer.width - 1);
  start_row = Clamp(start_row, 0, layer.height - 1);
  end_row = Clamp(end_row, 0, layer.height - 1);

  for (int row = start_row; row <= end_row; ++row) {
    for (int col = start_col; col <= end_col; ++col) {
      int raw = layer.tiles[row * layer.width + col];
      int tile_id = raw & kTileIdMask;
      if (tile_id <= 0) continue;

      // Tile position in world space, offset by parallax.
      float px = col * tw + parallax_offset_x;
      float py = row * th + parallax_offset_y;

      // UV coordinates from tile_id. Tile IDs are 1-based.
      int tile_col = (tile_id - 1) % tiles_per_row;
      int tile_row = (tile_id - 1) / tiles_per_row;

      // Inset UVs by half a texel to prevent sampling adjacent tile edges.
      const float half_texel_u = 0.5f / sheet_w;
      const float half_texel_v = 0.5f / sheet_h;
      float u0 = (tile_col * tw) / sheet_w + half_texel_u;
      float v0 = (tile_row * th) / sheet_h + half_texel_v;
      float u1 = ((tile_col + 1) * tw) / sheet_w - half_texel_u;
      float v1 = ((tile_row + 1) * th) / sheet_h - half_texel_v;

      // Apply flip flags by swapping UV coordinates.
      if (raw & kTileFlipHorizontal) std::swap(u0, u1);
      if (raw & kTileFlipVertical) std::swap(v0, v1);
      // Diagonal flip (anti-diagonal transpose) is equivalent to a 90° CW
      // rotation + horizontal flip. We approximate it by swapping both axes.
      if (raw & kTileFlipDiagonal) {
        std::swap(u0, u1);
        std::swap(v0, v1);
      }

      FVec2 p0(px, py);
      FVec2 p1(px + tw, py + th);
      FVec2 q0(u0, v0);
      FVec2 q1(u1, v1);
      FVec2 origin(px + tw * 0.5f, py + th * 0.5f);
      batch->PushQuad(p0, p1, q0, q1, origin, /*angle=*/0.0f);
    }
  }
}

}  // namespace G
