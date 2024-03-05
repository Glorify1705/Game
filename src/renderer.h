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
    AddCommand(kSetTexture, SetTexture{texture_unit});
  }

  void ClearTexture() { SetActiveTexture(noop_texture_); }

  Color SetActiveColor(const Color& color) {
    AddCommand(kSetColor, SetColor{color});
    auto result = current_color_;
    current_color_ = color;
    return result;
  }

  void SetActiveTransform(const FMat4x4& transform) {
    AddCommand(kSetTransform, SetTransform{transform});
  }

  void PushQuad(FVec2 p0, FVec2 p1, FVec2 q0, FVec2 q1, FVec2 origin,
                float angle) {
    AddCommand(kRenderQuad, RenderQuad{p0, p1, q0, q1, origin, angle});
  }
  void PushTriangle(FVec2 p0, FVec2 p1, FVec2 p2, FVec2 q0, FVec2 q1, FVec2 q2,
                    FVec2 origin, float angle) {
    AddCommand(kRenderTrig,
               RenderTriangle{p0, p1, p2, q0, q1, q2, origin, angle});
  }

  void SwitchShaderProgram(std::string_view fragment_shader_name) {
    program_handle_ = StringIntern(fragment_shader_name);
  }

  void Clear() {
    commands_.Clear();
    pos_ = 0;
    current_color_ = Color::White();
  }

  void Finish() { AddCommand(kDone, DoneCommand{}); }

  void SetViewport(IVec2 viewport);

  IVec2 GetViewport() const { return viewport_; }

  void Render(Allocator* scratch);

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

 private:
  enum CommandType : uint32_t {
    kRenderQuad = 1,
    kRenderTrig,
    kSetTexture,
    kSetColor,
    kSetTransform,
    kDone
  };

  struct DoneCommand {};

  struct RenderQuad {
    FVec2 p0, p1, q0, q1, origin;
    float angle;
  };

  struct RenderTriangle {
    FVec2 p0, p1, p2, q0, q1, q2, origin;
    float angle;
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

  inline static constexpr uint32_t kMaxCount = 1 << 20;

  struct QueueEntry {
    uint32_t type : 12;
    uint32_t count : 20;
  };

  union Command {
    RenderQuad quad;
    RenderTriangle triangle;
    SetTexture set_texture;
    SetColor set_color;
    SetTransform set_transform;
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

  struct ScreenshotRequest {
    uint8_t* out_buffer;
    size_t width, height;
    void (*callback)(uint8_t*, size_t, size_t, void*);
    void* userdata = nullptr;
  };

  class CommandIterator;

  template <typename T>
  void AddCommand(CommandType command, const T& data) {
    AddCommand(command, &data, sizeof(data));
  }

  void AddCommand(CommandType type, const void* data, size_t size);

  static constexpr size_t SizeOfCommand(CommandType t) {
    switch (t) {
      case kRenderQuad:
        return sizeof(RenderQuad);
      case kRenderTrig:
        return sizeof(RenderTriangle);
      case kSetTexture:
        return sizeof(SetTexture);
      case kSetColor:
        return sizeof(SetColor);
      case kSetTransform:
        return sizeof(SetTransform);
      case kDone:
        return 0;
    }
    return 0;
  }

  static std::string_view CommandName(CommandType t) {
    switch (t) {
      case kRenderQuad:
        return "RENDER_QUAD";
      case kRenderTrig:
        return "RENDER_TRIANGLE";
      case kSetTexture:
        return "SET_TEXTURE";
      case kSetColor:
        return "SET_COLOR";
      case kSetTransform:
        return "SET_TRANSFORM";
      case kDone:
        return "DONE";
    }
    return "";
  }

  void TakeScreenshots();

  Allocator* allocator_;
  uint8_t* command_buffer_ = nullptr;
  size_t pos_ = 0;
  FixedArray<QueueEntry> commands_;
  FixedArray<GLuint> tex_;
  FixedArray<ScreenshotRequest> screenshots_;
  Color current_color_;
  Shaders* shaders_;
  GLuint ebo_, vao_, vbo_;
  size_t noop_texture_;
  GLuint screen_quad_vao_, screen_quad_vbo_;
  GLuint intermediate_target_;
  GLuint render_target_, downsampled_target_, render_texture_,
      downsampled_texture_, depth_buffer_;
  GLint antialiasing_samples_;
  IVec2 viewport_;
  bool debug_render_ = false;
  uint32_t program_handle_;
};

class Renderer {
 public:
  Renderer(const Assets& assets, BatchRenderer* renderer, Allocator* allocator);

  void BeginFrame();
  void FlushFrame() { renderer_->Finish(); }

  void Draw(std::string_view spritename, FVec2 position, float angle);
  void Draw(const SpriteAsset& asset, FVec2 position, float angle);

  // Returns the previous color.
  Color SetColor(Color color);

  void DrawRect(FVec2 top_left, FVec2 bottom_right, float angle);
  void DrawCircle(FVec2 center, float radius);
  void DrawText(std::string_view font, uint32_t size, std::string_view str,
                FVec2 position);

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

  const SpriteAsset* LoadSprite(std::string_view name);
  const FontInfo* LoadFont(const FontAsset& asset, uint32_t font_size,
                           Allocator* scratch);

  void ApplyTransform(const FMat4x4& mat) {
    transform_stack_.back() = mat * transform_stack_.back();
    renderer_->SetActiveTransform(transform_stack_.back());
  }

  Allocator* allocator_;
  const Assets* assets_;
  BatchRenderer* renderer_;

  FixedArray<FMat4x4> transform_stack_;

  Dictionary<uint32_t> textures_table_;
  FixedArray<GLuint> textures_;

  Dictionary<uint32_t> sprites_table_;
  FixedArray<const SpriteAsset*> sprites_;

  Dictionary<uint32_t> font_table_;
  FixedArray<FontInfo> fonts_;
};

}  // namespace G

#endif  // _GAME_RENDERER_H