#pragma once
#ifndef _GAME_RENDERER_H
#define _GAME_RENDERER_H

#include <vector>

#include "array.h"
#include "assets.h"
#include "color.h"
#include "dictionary.h"
#include "libraries/stb_truetype.h"
#include "mat.h"
#include "shaders.h"
#include "transformations.h"
#include "vec.h"

namespace G {

class BatchRenderer {
 public:
  BatchRenderer(IVec2 viewport, Shaders* shaders, Allocator* allocator);

  ~BatchRenderer();

  size_t LoadTexture(const DbAssets::Image& image);

  size_t LoadTexture(const void* data, size_t width, size_t height);

  void SetActiveTexture(size_t texture_unit) {
    AddCommand(kSetTexture, SetTexture{texture_unit});
  }

  void ClearTexture() { SetActiveTexture(noop_texture_); }

  void SetActiveColor(const Color& color) {
    AddCommand(kSetColor, SetColor{color});
  }

  void SetActiveTransform(const FMat4x4& transform) {
    AddCommand(kSetTransform, SetTransform{transform});
  }

  void PushQuad(FVec2 p0, FVec2 p1, FVec2 q0, FVec2 q1, FVec2 origin,
                float angle) {
    AddCommand(kRenderQuad, RenderQuad{p0, p1, q0, q1, origin, angle});
  }
  void PushTriangle(FVec2 p0, FVec2 p1, FVec2 p2, FVec2 q0, FVec2 q1,
                    FVec2 q2) {
    AddCommand(kRenderTrig, RenderTriangle{p0, p1, p2, q0, q1, q2});
  }
  void BeginLine() { AddCommand(kStartLine, StartLine{}); }

  void FinishLine() { AddCommand(kEndLine, EndLine{}); }

  void PushLinePoints(const FVec2* ps, size_t n) {
    AddCommand(kAddLinePoint, /*count=*/n, ps, n * sizeof(FVec2));
  }

  void SetShaderProgram(std::string_view program_name) {
    AddCommand(kSetShader, SetShader{StringIntern(program_name)});
  }

  void SetActiveLineWidth(float width) {
    AddCommand(kSetLineWidth, SetLineWidth{width});
  }

  void Clear() {
    commands_.Clear();
    pos_ = 0;
  }

  void Finish() { AddCommand(kDone, DoneCommand{}); }

  void SetViewport(IVec2 viewport);

  IVec2 GetViewport() const { return viewport_; }

  void Render(Allocator* scratch);

 private:
  enum CommandType : uint32_t {
    kRenderQuad = 1,
    kRenderTrig,
    kStartLine,
    kAddLinePoint,
    kEndLine,
    kSetTexture,
    kSetColor,
    kSetTransform,
    kSetShader,
    kSetLineWidth,
    kDone
  };

  struct DoneCommand {};

  struct RenderQuad {
    FVec2 p0, p1, q0, q1, origin;
    float angle;
  };

  struct RenderTriangle {
    FVec2 p0, p1, p2, q0, q1, q2;
  };

  struct SetTexture {
    size_t texture_unit;
  };

  struct SetColor {
    Color color;
  };

  struct SetTransform {
    FMat4x4 transform;
  };

  struct SetShader {
    uint32_t shader_handle;
  };

  struct StartLine {};

  struct AddLinePoint {
    FVec2 p0;
  };

  struct EndLine {};

  struct SetLineWidth {
    float width;
  };

  inline static constexpr uint32_t kMaxCount = 1 << 20;

  struct QueueEntry {
    uint32_t type : 12;
    uint32_t count : 20;
  };

  union Command {
    RenderQuad quad;
    RenderTriangle triangle;
    StartLine start_line;
    AddLinePoint add_line_point;
    EndLine end_line;
    SetTexture set_texture;
    SetColor set_color;
    SetTransform set_transform;
    SetLineWidth set_line_width;
    SetShader set_shader;
  };

  static_assert(std::is_trivially_copyable_v<Command>);

  struct VertexData {
    FVec2 position;
    FVec2 tex_coords;
    // We duplicate the origin angle and color for every vertex in the quad
    // to avoid having to reset a uniform on drawing every colored rotated quad,
    // which would require an OpenGL context switch. This way we trade a bit
    // more computation and GPU RAM for less driver + OpenGL flushes.
    FVec2 origin;
    float angle;
    Color color;
  };

  class CommandIterator;

  template <typename T>
  void AddCommand(CommandType command, const T& data) {
    AddCommand(command, /*count=*/1, &data, sizeof(data));
  }

  void AddCommand(CommandType type, uint32_t count, const void* data,
                  size_t size);

  static constexpr size_t SizeOfCommand(CommandType t);

  static constexpr std::string_view CommandName(CommandType t);

  Allocator* allocator_;
  uint8_t* command_buffer_ = nullptr;
  size_t pos_ = 0;
  FixedArray<QueueEntry> commands_;
  FixedArray<GLuint> tex_;
  Shaders* shaders_;
  GLuint ebo_, vao_, vbo_;
  size_t noop_texture_;
  GLuint screen_quad_vao_, screen_quad_vbo_;
  GLuint render_target_, downsampled_target_, render_texture_,
      downsampled_texture_, depth_buffer_;
  GLint antialiasing_samples_;
  IVec2 viewport_, original_viewport_;
};

class Renderer {
 public:
  Renderer(const DbAssets& assets, BatchRenderer* renderer,
           Allocator* allocator);

  void ClearForFrame();
  void FlushFrame() { renderer_->Finish(); }

  void DrawSprite(std::string_view sprite_name, FVec2 position, float angle);
  void DrawSprite(const DbAssets::Sprite& asset, FVec2 position, float angle);

  void DrawImage(std::string_view imagename, FVec2 position, float angle);
  void DrawImage(const DbAssets::Image& asset, FVec2 position, float angle);

  // Returns the previous color.
  Color SetColor(Color color);

  // Returns the previous width.
  float SetLineWidth(float width);

  void DrawRect(FVec2 top_left, FVec2 bottom_right, float angle);
  void DrawCircle(FVec2 center, float radius);
  void DrawText(std::string_view font_name, uint32_t size, std::string_view str,
                FVec2 position);
  void DrawLine(FVec2 p0, FVec2 p1);
  void DrawLines(const FVec2* ps, size_t n);

  IVec2 TextDimensions(std::string_view font_name, uint32_t size,
                       std::string_view str);
  void DrawTriangle(FVec2 p1, FVec2 p2, FVec2 p3);

  IVec2 viewport() const { return renderer_->GetViewport(); }

  void Push();

  void Pop();

  void Rotate(float angle) { ApplyTransform(RotationZ(angle)); }
  void Translate(float x, float y) { ApplyTransform(TranslationXY(x, y)); }
  void Scale(float x, float y) { ApplyTransform(ScaleXY(x, y)); }

 private:
  inline static constexpr size_t kAtlasWidth = 1024;
  inline static constexpr size_t kAtlasHeight = 1024;
  inline static constexpr size_t kAtlasSize = kAtlasWidth * kAtlasHeight;

  struct FontInfo {
    GLuint texture;
    float scale = 0;
    int ascent, descent, line_gap;
    stbtt_fontinfo font_info;
    stbtt_pack_context context;
    stbtt_packedchar chars[256];
  };

  const FontInfo* LoadFont(std::string_view font_name, uint32_t font_size);

  void ApplyTransform(const FMat4x4& mat) {
    transform_stack_.back() = mat * transform_stack_.back();
    renderer_->SetActiveTransform(transform_stack_.back());
  }

  Color color_;
  float line_width_;

  Allocator* allocator_;
  const DbAssets* assets_;
  BatchRenderer* renderer_;

  FixedArray<FMat4x4> transform_stack_;

  Dictionary<uint32_t> textures_table_;
  FixedArray<GLuint> textures_;

  Dictionary<uint32_t> loaded_sprites_table_;
  FixedArray<const DbAssets::Sprite*> loaded_sprites_;

  Dictionary<uint32_t> loaded_images_table_;
  FixedArray<const DbAssets::Image*> loaded_images_;

  Dictionary<uint32_t> font_table_;
  FixedArray<FontInfo> fonts_;
};

}  // namespace G

#endif  // _GAME_RENDERER_H
