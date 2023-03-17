#pragma once
#ifndef _GAME_RENDERER_H
#define _GAME_RENDERER_H

#include <vector>

#include "array.h"
#include "assets.h"
#include "map.h"
#include "mat.h"
#include "shaders.h"
#include "transformations.h"
#include "vec.h"

namespace G {

class QuadRenderer {
 public:
  QuadRenderer(IVec2 viewport);
  ~QuadRenderer();

  GLuint LoadTexture(const ImageFile& image) {
    return LoadTexture(image.contents()->Data(), image.width(), image.height());
  }

  GLuint LoadTexture(const void* data, size_t width, size_t height);

  void SetActiveTexture(GLuint texture) {
    if (batches_.empty() || (batches_.back().indices_count &&
                             texture != batches_.back().texture_unit)) {
      FlushBatch();
    }
    batches_.back().texture_unit = texture;
  }

  void SetActiveColor(FVec4 rgba_color) {
    if (batches_.empty() && (batches_.back().indices_count &&
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

class SpriteSheetRenderer {
 public:
  SpriteSheetRenderer(Assets* assets, QuadRenderer* renderer);

  void BeginFrame();
  void FlushFrame() {}

  void Draw(FVec2 position, float angle, const Subtexture& texture);

  void SetColor(FVec4 color);
  void DrawRect(FVec2 top_left, FVec2 bottom_right, float angle);

  const Subtexture* sub_texture(std::string_view name) const;
  IVec2 viewport() const { return renderer_->GetViewport(); }

  void Push();

  void Pop();

  void Rotate(float angle) { ApplyTransform(RotationZ(angle)); }
  void Translate(float x, float y) { ApplyTransform(TranslationXY(x, y)); }
  void Scale(float x, float y) { ApplyTransform(ScaleXY(x, y)); }

 private:
  void ApplyTransform(const FMat4x4& mat) {
    transform_stack_.back() = mat * transform_stack_.back();
    renderer_->SetActiveTransform(transform_stack_.back());
  }

  FixedArray<FMat4x4, 128> transform_stack_;

  struct Sheet {
    GLuint texture;
    uint32_t width, height;
  };

  LookupTable<Sheet> sheet_info_;
  LookupTable<const Subtexture*> subtexts_;
  const Subtexture* texture_current_ = nullptr;
  Sheet current_;
  QuadRenderer* renderer_;
};

}  // namespace G

#endif  // _GAME_RENDERER_H