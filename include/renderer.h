#pragma once
#ifndef _GAME_RENDERER_H
#define _GAME_RENDERER_H

#include <vector>

#include "array.h"
#include "assets.h"
#include "lookup_table.h"
#include "mat.h"
#include "shaders.h"
#include "stb_truetype.h"
#include "transformations.h"
#include "vec.h"

namespace G {

class BatchRenderer {
 public:
  BatchRenderer(IVec2 viewport);
  ~BatchRenderer();

  GLuint LoadTexture(const ImageAsset& image);

  GLuint LoadTexture(const void* data, size_t width, size_t height);

  void SetActiveTexture(GLuint texture) {
    if (batches_.empty() || (batches_.back().indices_count &&
                             texture != batches_.back().texture_unit)) {
      FlushBatch();
    }
    batches_.back().texture_unit = texture;
  }

  void SetActiveColor(FVec4 rgba_color) {
    if (batches_.empty() || (batches_.back().indices_count &&
                             rgba_color != batches_.back().rgba_color)) {
      FlushBatch();
    }
    batches_.back().rgba_color = rgba_color;
  }
  void SetActiveTransform(const FMat4x4& transform) {
    if (batches_.empty() || (batches_.back().indices_count &&
                             transform != batches_.back().transform)) {
      FlushBatch();
    }
    batches_.back().transform = transform;
  }

  void PushQuad(FVec2 p0, FVec2 p1, FVec2 q0, FVec2 q1, FVec2 origin,
                float angle);
  void PushTriangle(FVec2 p0, FVec2 p1, FVec2 p2, FVec2 q0, FVec2 q1, FVec2 q2,
                    FVec2 origin, float angle);

  void Clear() {
    batches_.Clear();
    indices_.Clear();
    vertices_.Clear();
  }

  void SetViewport(IVec2 viewport) { viewport_ = viewport; }

  IVec2 GetViewport() const { return viewport_; }

  void Render();

  void ToggleDebugRender() { debug_render_ = !debug_render_; }

 private:
  struct VertexData {
    FVec2 position;
    FVec2 tex_coords;
    // We duplicate the origin and angle for every vertex in the quad
    // to avoid having to reset a uniform on drawing every rotated quad,
    // which would require an OpenGL context switch. This way we trade a bit
    // more computation and GPU RAM for less driver + OpenGL flushes.
    //
    // TODO: Create a separate array for the rotated quads (e.g. for text).
    FVec2 origin;
    float angle;
  };
  struct Batch {
    GLuint texture_unit;
    FVec4 rgba_color;
    FMat4x4 transform;
    size_t indices_start;
    size_t indices_count;
  };

  void FlushBatch() {
    Batch batch;
    if (!batches_.empty()) {
      const auto& prev = batches_.back();
      batch.texture_unit = prev.texture_unit;
      batch.rgba_color = prev.rgba_color;
      batch.transform = prev.transform;
    } else {
      batch.texture_unit = 0;
      batch.transform = FMat4x4::Identity();
      batch.rgba_color = FVec(1, 1, 1, 1);
    }
    batch.indices_start = indices_.size();
    batch.indices_count = 0;
    batches_.Push(std::move(batch));
  }

  FixedArray<VertexData, 1 << 24> vertices_;
  FixedArray<GLuint, 1 << 24> indices_;
  ShaderCompiler compiler_;
  ShaderId vertex_shader_;
  ShaderId fragment_shader_;
  ShaderProgram shader_program_;
  FixedArray<Batch, 1 << 24> batches_;
  std::array<GLuint, 64> tex_;
  GLuint ebo_, vao_, vbo_;
  GLuint unit_ = 0;
  IVec2 viewport_;
  bool debug_render_ = false;
};

class Renderer {
 public:
  Renderer(const Assets& assets, BatchRenderer* renderer);

  void BeginFrame();
  void FlushFrame() {}

  void Draw(FVec2 position, float angle, const SpriteAsset& texture);

  void SetColor(FVec4 color);
  void DrawRect(FVec2 top_left, FVec2 bottom_right, float angle);
  void DrawCircle(FVec2 center, float radius);
  void DrawText(std::string_view font, float size, std::string_view str,
                FVec2 position);

  const SpriteAsset* sprite(std::string_view name) const;
  IVec2 viewport() const { return renderer_->GetViewport(); }

  void Push();

  void Pop();

  void Rotate(float angle) { ApplyTransform(RotationZ(angle)); }
  void Translate(float x, float y) { ApplyTransform(TranslationXY(x, y)); }
  void Scale(float x, float y) { ApplyTransform(ScaleXY(x, y)); }

 private:
  inline static constexpr size_t kAtlasWidth = 4096;
  inline static constexpr size_t kAtlasHeight = 4096;
  inline static constexpr size_t kFontSize = 32;

  struct SheetTexture {
    GLuint texture;
    uint32_t width, height;
  };

  struct FontInfo {
    GLuint texture;
    float scale = 0;
    int ascent, descent, line_gap;
    stbtt_fontinfo font_info;
    stbtt_pack_context context;
    std::array<stbtt_packedchar, 256> chars;
    std::array<uint8_t, kAtlasWidth * kAtlasHeight> atlas;
  };

  void LoadSpreadsheets(const Assets& assets);
  void LoadFonts(const Assets& assets, float pixel_height);

  void ApplyTransform(const FMat4x4& mat) {
    transform_stack_.back() = mat * transform_stack_.back();
    renderer_->SetActiveTransform(transform_stack_.back());
  }

  FixedArray<FMat4x4, 128> transform_stack_;

  LookupTable<SheetTexture> spritesheet_info_;
  LookupTable<const SpriteAsset*> sprites_;
  std::string_view current_spritesheet_;
  SheetTexture current_;
  BatchRenderer* renderer_;

  LookupTable<FontInfo*> font_table_;
  std::array<FontInfo, 32> fonts_;
  size_t font_count_ = 0;
};

}  // namespace G

#endif  // _GAME_RENDERER_H