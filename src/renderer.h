#pragma once
#ifndef _GAME_RENDERER_H
#define _GAME_RENDERER_H

#include <vector>

#include "array.h"
#include "assets.h"
#include "color.h"
#include "dictionary.h"
#include "libraries/stb_truetype.h"
#include "lookup_table.h"
#include "mat.h"
#include "shaders.h"
#include "transformations.h"
#include "vec.h"

namespace G {

class BatchRenderer {
 public:
  BatchRenderer(IVec2 viewport, Shaders* shaders, Allocator* allocator);

  ~BatchRenderer();

  size_t LoadTexture(const ImageAsset& image);

  size_t LoadTexture(const void* data, size_t width, size_t height);

  void SetActiveTexture(size_t texture_unit) {
    if (batches_.empty() || (batches_.back().indices_count &&
                             texture_unit != batches_.back().texture_unit)) {
      FlushBatch();
    }
    batches_.back().texture_unit = texture_unit;
  }

  void ClearTexture() { SetActiveTexture(noop_texture_); }

  Color SetActiveColor(Color rgba_color) {
    // We do not need to flush on color changes because they are
    // put on vertex data.
    Color previous;
    if (!batches_.empty()) {
      previous = batches_.back().rgba_color;
    } else {
      previous = Color::White();
      FlushBatch();
    }
    batches_.back().rgba_color = rgba_color;
    return previous;
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

  void SetViewport(IVec2 viewport);

  IVec2 GetViewport() const { return viewport_; }

  void Render();

  void ToggleDebugRender() { debug_render_ = !debug_render_; }

  void RequestScreenshot(uint8_t* pixels, size_t width, size_t height,
                         void (*callback)(uint8_t*, size_t, size_t, void*),
                         void* userdata);

  template <typename T>
  void RequestScreenshot(uint8_t* pixels, size_t width, size_t height, T* ptr) {
    RequestScreenshot(
        pixels, width, height,
        [](uint8_t* pixels, size_t width, size_t height, void* userdata) {
          reinterpret_cast<T*>(userdata)->HandleScreenshot(pixels, width,
                                                           height);
        },
        ptr);
  }

  void SwitchShaderProgram(std::string_view fragment_shader_name);

 private:
  struct VertexData {
    // The position contains a Z to do ordering.
    FVec3 position;
    FVec2 tex_coords;
    // We duplicate the origin angle and color for every vertex in the quad
    // to avoid having to reset a uniform on drawing every colored rotated quad,
    // which would require an OpenGL context switch. This way we trade a bit
    // more computation and GPU RAM for less driver + OpenGL flushes.
    FVec2 origin;
    float angle;
    Color color;
  };
  struct Batch {
    GLuint texture_unit;
    Color rgba_color;
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
      batch.rgba_color = Color::White();
    }
    batch.indices_start = indices_.size();
    batch.indices_count = 0;
    batches_.Push(std::move(batch));
  }

  struct ScreenshotRequest {
    uint8_t* out_buffer;
    size_t width, height;
    void (*callback)(uint8_t*, size_t, size_t, void*);
    void* userdata = nullptr;
  };

  void TakeScreenshots();

  Allocator* allocator_;
  FixedArray<ScreenshotRequest, 32> screenshots_;
  FixedArray<VertexData, 1 << 24> vertices_;
  FixedArray<GLuint, 1 << 24> indices_;
  FixedArray<Batch, 1 << 12> batches_;
  FixedArray<GLuint, 64> tex_;
  Shaders* shaders_;
  GLuint ebo_, vao_, vbo_;
  size_t noop_texture_;
  GLuint screen_quad_vao_, screen_quad_vbo_;
  GLuint render_target_, render_texture_, depth_buffer_;
  IVec2 viewport_;
  bool debug_render_ = false;
  FixedStringBuffer<128> program_name_;
};

class Renderer {
 public:
  Renderer(const Assets& assets, BatchRenderer* renderer, Allocator* allocator);

  void BeginFrame();
  void FlushFrame() {}

  void Draw(FVec2 position, float angle, const SpriteAsset& texture);

  // Returns the previous color.
  Color SetColor(Color color);

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
  inline static constexpr size_t kAtlasSize = kAtlasWidth * kAtlasHeight;
  inline static constexpr size_t kFontSize = 32;

  struct FontInfo {
    GLuint texture;
    float scale = 0;
    int ascent, descent, line_gap;
    stbtt_fontinfo font_info;
    stbtt_pack_context context;
    std::array<stbtt_packedchar, 256> chars;
  };

  void LoadSpreadsheets(const Assets& assets);
  void LoadFonts(const Assets& assets, float pixel_height);

  void ApplyTransform(const FMat4x4& mat) {
    transform_stack_.back() = mat * transform_stack_.back();
    renderer_->SetActiveTransform(transform_stack_.back());
  }

  Allocator* allocator_;
  const Assets* assets_;
  BatchRenderer* renderer_;

  FixedArray<FMat4x4, 128> transform_stack_;

  FixedArray<GLuint, 256> textures_;

  Dictionary<uint32_t> sprites_table_;
  FixedArray<const SpriteAsset*, 1 << 20> sprites_;

  Dictionary<uint32_t> font_table_;
  FixedArray<FontInfo, 64> fonts_;
};

}  // namespace G

#endif  // _GAME_RENDERER_H