#pragma once
#ifndef _GAME_RENDERER_H
#define _GAME_RENDERER_H

#include "allocators.h"
#include "array.h"
#include "assets.h"
#include "color.h"
#include "dictionary.h"
#include "libraries/sqlite3.h"
#include "libraries/stb_rect_pack.h"
#include "libraries/stb_truetype.h"
#include "mat.h"
#include "particles.h"
#include "shaders.h"
#include "transformations.h"
#include "vec.h"

namespace G {

enum BlendMode : uint8_t {
  BLEND_ALPHA = 0,      // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA (default)
  BLEND_ADD,            // GL_SRC_ALPHA, GL_ONE
  BLEND_MULTIPLY,       // GL_DST_COLOR, GL_ZERO
  BLEND_REPLACE,        // GL_ONE, GL_ZERO
  BLEND_PREMULTIPLIED,  // GL_ONE, GL_ONE_MINUS_SRC_ALPHA (internal)
};

// Off-screen render target backed by an OpenGL framebuffer object.
struct Canvas {
  GLuint fbo;           // Framebuffer object for off-screen rendering.
  GLuint texture;       // Color texture attached to the FBO.
  size_t texture_unit;  // Texture unit index used when sampling this canvas.
  int width, height;    // Dimensions of the canvas in pixels.

  // Releases the GPU resources owned by this canvas.
  void Destroy();
};

// Per-frame rendering statistics populated by BatchRenderer::Render().
struct FrameStats {
  int draw_calls = 0;
  int vertices = 0;
  int commands = 0;
  // Per-reason flush breakdown.
  int flush_texture = 0;
  int flush_transform = 0;
  int flush_shader = 0;
  int flush_blend = 0;
  int flush_canvas = 0;
  int flush_line_end = 0;
  int flush_other = 0;
  int flush_overflow =
      0;  // Mid-frame flushes triggered by command buffer overflow.
  // Redundant flushes avoided by state filtering.
  int redundant_texture = 0;
  int redundant_transform = 0;
  int redundant_shader = 0;
  int redundant_blend = 0;
  int redundant_line_width = 0;
  int redundant_sdf_outline = 0;
  int flush_particles = 0;
};

class BatchRenderer {
 public:
  BatchRenderer(IVec2 viewport, Shaders* shaders, Allocator* allocator);

  ~BatchRenderer();

  size_t LoadTexture(const DbAssets::Image& image);

  size_t LoadTexture(const void* data, size_t width, size_t height);

  size_t LoadFontTexture(const void* data, size_t width, size_t height);

  size_t RegisterTexture(GLuint tex);

  Canvas CreateCanvas(int width, int height, bool nearest_filter);

  void SetActiveTexture(size_t texture_unit) {
    rec_texture_ = texture_unit;
    AddCommand(kSetTexture, SetTexture{texture_unit});
  }

  void ClearTexture() { SetActiveTexture(noop_texture_); }

  size_t noop_texture() const { return noop_texture_; }

  void SetActiveColor(const Color& color) {
    rec_color_ = color;
    AddCommand(kSetColor, SetColor{color});
  }

  void SetActiveTransform(const FMat4x4& transform) {
    rec_transform_ = transform;
    AddCommand(kSetTransform, SetTransform{transform});
  }

  void SetActiveCanvas(GLuint fbo, int width, int height) {
    rec_canvas_ = {fbo, width, height};
    AddCommand(kSetCanvas, SetCanvas{fbo, width, height});
  }

  void ResetCanvas() {
    rec_canvas_ = {render_target_, viewport_.x, viewport_.y};
    AddCommand(kSetCanvas, SetCanvas{render_target_, viewport_.x, viewport_.y});
  }

  void SetActiveBlendMode(BlendMode mode) {
    rec_blend_ = mode;
    AddCommand(kSetBlendMode, SetBlendMode{mode});
  }

  void ClearWithColor(float r, float g, float b, float a) {
    AddCommand(kClearColor, ClearColor{r, g, b, a});
  }

  void SetSDFOutline(float r, float g, float b, float a, float thickness) {
    rec_sdf_outline_ = {r, g, b, a, thickness};
    AddCommand(kSetSDFOutline, SDFOutline{r, g, b, a, thickness});
  }

  // Clips all subsequent draws to an axis-aligned screen rectangle.
  void SetScissor(int x, int y, int w, int h) {
    rec_scissor_enabled_ = true;
    rec_scissor_ = {x, y, w, h};
    AddCommand(kSetScissor, SetScissorRect{x, y, w, h});
  }

  // Removes the scissor clipping region.
  void ClearScissor() {
    rec_scissor_enabled_ = false;
    AddCommand(kClearScissor, ClearScissorRect{});
  }

  // Begins writing to the stencil buffer. Geometry drawn after this call
  // writes stencil values instead of visible pixels.
  void BeginStencilWrite(uint16_t action, uint8_t ref) {
    rec_stencil_write_active_ = true;
    rec_stencil_write_ = {action, ref};
    AddCommand(kBeginStencilWrite, BeginStencilWriteCmd{action, ref});
  }

  // Ends stencil writing and restores normal color output.
  void EndStencilWrite() {
    rec_stencil_write_active_ = false;
    AddCommand(kEndStencilWrite, EndStencilWriteCmd{});
  }

  // Enables stencil testing: subsequent draws only appear where the stencil
  // buffer passes the comparison.
  void SetStencilTest(uint16_t compare, uint8_t ref) {
    rec_stencil_test_active_ = true;
    rec_stencil_test_ = {compare, ref};
    AddCommand(kSetStencilTest, SetStencilTestCmd{compare, ref});
  }

  // Disables stencil testing.
  void ClearStencilTest() {
    rec_stencil_test_active_ = false;
    AddCommand(kClearStencilTest, ClearStencilTestCmd{});
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

  void PushLinePoints(Slice<FVec2> ps) {
    AddCommand(kAddLinePoint, /*count=*/ps.size(), ps.data(),
               ps.size() * sizeof(FVec2));
  }

  // Pushes a single instanced draw command for particles. The instance_data
  // pointer must remain valid until RenderBatch completes (use frame
  // allocator).
  void DrawParticles(const ParticleInstanceData* instance_data, uint32_t count,
                     size_t texture_unit, BlendMode blend);

  void SetShaderProgram(std::string_view program_name) {
    current_shader_ = StringIntern(program_name);
    AddCommand(kSetShader, SetShader{current_shader_});
  }

  void SetShaderByHandle(uint32_t handle) {
    current_shader_ = handle;
    AddCommand(kSetShader, SetShader{handle});
  }

  uint32_t GetCurrentShaderHandle() const { return current_shader_; }

  void SetActiveLineWidth(float width) {
    rec_line_width_ = width;
    AddCommand(kSetLineWidth, SetLineWidth{width});
  }

  void Clear() {
    commands_.Clear();
    pos_ = 0;
    needs_clear_ = true;
    flush_overflow_ = 0;
  }

  void Finish() { AddCommand(kDone, DoneCommand{}); }

  void SetViewport(IVec2 viewport);

  void InitializeFramebuffers();

  void SetFrameTime(float t) { frame_time_ = t; }

  IVec2 GetViewport() const { return viewport_; }

  // Sets the actual window size for the post-pass. When different from the
  // viewport, the game is letterboxed to preserve aspect ratio.
  void SetWindowSize(IVec2 size) { window_size_ = size; }
  IVec2 GetWindowSize() const { return window_size_; }

  GLuint GetRenderTarget() const { return render_target_; }

  void Render();

  const FrameStats& GetFrameStats() const { return frame_stats_; }

  // Returns the number of bytes currently used in the command buffer.
  size_t GetCommandBufferUsed() const { return pos_; }

  // Returns the total command buffer capacity in bytes.
  size_t GetCommandBufferCapacity() const;

  // Returns the number of loaded texture units.
  // Returns the number of loaded texture units.
  size_t GetTextureCount() const { return tex_.size(); }

  // Returns the GL texture ID for a given texture index.
  GLuint GetTextureId(size_t index) const {
    return index < tex_.size() ? tex_[index] : 0;
  }

  // Returns the current viewport size.
  IVec2 viewport() const { return viewport_; }

  // Returns the current blend mode.
  BlendMode GetCurrentBlendMode() const { return rec_blend_; }

  // Returns the current shader handle.
  uint32_t GetCurrentShader() const { return current_shader_; }

  struct Screenshot {
    size_t width, height;
    const uint8_t* buffer;
  };

  Screenshot TakeScreenshot(Allocator* allocator) const;

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
    kSetCanvas,
    kSetBlendMode,
    kClearColor,
    kSetSDFOutline,
    kSetScissor,
    kClearScissor,
    kBeginStencilWrite,
    kEndStencilWrite,
    kSetStencilTest,
    kClearStencilTest,
    kRenderParticles,
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

  struct SetCanvas {
    GLuint fbo;
    int width, height;
  };

  struct SetBlendMode {
    BlendMode mode;
  };

  struct ClearColor {
    float r, g, b, a;
  };

  // Outline parameters for SDF text rendering. Color is premultiplied alpha.
  struct SDFOutline {
    float r, g, b, a;
    float thickness;
  };

  // Axis-aligned scissor rectangle in screen pixels.
  struct SetScissorRect {
    int x, y, w, h;
  };

  struct ClearScissorRect {};

  // Stencil write mode: draw geometry into the stencil buffer.
  struct BeginStencilWriteCmd {
    uint16_t action;  // GL stencil op (GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT)
    uint8_t ref;      // Reference value to write.
  };

  struct EndStencilWriteCmd {};

  // Stencil test: subsequent draws pass/fail based on stencil comparison.
  struct SetStencilTestCmd {
    uint16_t compare;  // GL comparison func (GL_EQUAL, GL_NOTEQUAL, etc.)
    uint8_t ref;       // Reference value to compare against.
  };

  struct ClearStencilTestCmd {};

  // Instanced particle draw. Data pointer is frame-allocator-owned.
  struct RenderParticlesCmd {
    const ParticleInstanceData* data;
    uint32_t count;
    size_t texture_unit;
    BlendMode blend;
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
    SetCanvas set_canvas;
    SetBlendMode set_blend_mode;
    ClearColor clear_color;
    SDFOutline sdf_outline;
    SetScissorRect set_scissor;
    ClearScissorRect clear_scissor;
    BeginStencilWriteCmd begin_stencil_write;
    EndStencilWriteCmd end_stencil_write;
    SetStencilTestCmd set_stencil_test;
    ClearStencilTestCmd clear_stencil_test;
    RenderParticlesCmd render_particles;
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

  // Submits the current command buffer to the GPU. Builds vertex/index
  // arrays, uploads them, and issues draw calls. Does not clear or post-pass.
  void RenderBatch();

  // Called when AddCommand would overflow the command buffer. Submits the
  // current batch to the GPU, resets the buffer, and re-emits current state.
  void FlushAndContinue();

  // Re-emits current recording state into a freshly cleared command buffer.
  void ReEmitState();

  // Accumulates local batch stats into the per-frame totals.
  void AccumulateStats(const FrameStats& batch_stats, int vertices_count);

  // Sets up common GL state for rendering (blend, multisample, etc.).
  void SetupGLState();

  // Initializes the particle VAO, quad VBO/EBO, and instance VBO.
  void InitializeParticleResources();

  // Renders particles via instanced draw. Called from within RenderBatch.
  void RenderParticlesBatch(const RenderParticlesCmd& cmd, int viewport_w,
                            int viewport_h, const FMat4x4& transform,
                            FrameStats& stats);

  Allocator* allocator_;
  uint8_t* command_buffer_ = nullptr;
  size_t pos_ = 0;
  float frame_time_ = 0;
  uint32_t current_shader_ = 0;
  FrameStats frame_stats_;
  FixedArray<QueueEntry> commands_;
  FixedArray<GLuint> tex_;
  Shaders* shaders_;
  GLuint ebo_, vao_, vbo_;
  size_t noop_texture_;
  GLuint screen_quad_vao_, screen_quad_vbo_;
  GLuint particle_vao_, particle_quad_vbo_, particle_quad_ebo_,
      particle_instance_vbo_;
  GLuint render_target_, downsampled_target_, render_texture_,
      downsampled_texture_, depth_buffer_;
  GLint antialiasing_samples_;
  IVec2 viewport_;
  IVec2 window_size_;

  // Scratch arena for vertex/index arrays during RenderBatch. Allocated once,
  // reset before each batch submission.
  ArenaAllocator render_scratch_;

  // Whether the framebuffer needs clearing before the next batch submission.
  bool needs_clear_ = true;

  // Number of mid-frame overflow flushes this frame.
  int flush_overflow_ = 0;

  // Recording state: tracks the last value of each state command so that
  // FlushAndContinue can re-emit them after resetting the command buffer.
  size_t rec_texture_ = 0;
  Color rec_color_ = Color::White();
  FMat4x4 rec_transform_ = FMat4x4::Identity();
  BlendMode rec_blend_ = BLEND_ALPHA;
  float rec_line_width_ = 2.5f;
  SetCanvas rec_canvas_ = {};
  SDFOutline rec_sdf_outline_ = {};
  bool rec_scissor_enabled_ = false;
  SetScissorRect rec_scissor_ = {};
  bool rec_stencil_write_active_ = false;
  BeginStencilWriteCmd rec_stencil_write_ = {};
  bool rec_stencil_test_active_ = false;
  SetStencilTestCmd rec_stencil_test_ = {};
};

class Renderer {
 public:
  Renderer(const DbAssets& assets, BatchRenderer* renderer, sqlite3* db,
           Allocator* allocator);

  void ClearForFrame();
  void FlushFrame() { renderer_->Finish(); }

  ErrorOr<void> LoadSprite(const DbAssets::Sprite& sprite);
  void LoadTexture(const DbAssets::Image& image);
  ErrorOr<void> LoadSpritesheet(const DbAssets::Spritesheet& sprite);
  void LoadFont(const DbAssets::Font& font);

  ErrorOr<void> DrawSprite(std::string_view sprite_name, FVec2 position,
                           float angle);
  ErrorOr<void> DrawSprite(const DbAssets::Sprite& asset, FVec2 position,
                           float angle);

  ErrorOr<void> DrawImage(std::string_view imagename, FVec2 position,
                          float angle);
  ErrorOr<void> DrawImage(const DbAssets::Image& asset, FVec2 position,
                          float angle);

  DbAssets::Sprite* GetSprite(std::string_view name) const {
    DbAssets::Sprite* result = nullptr;
    if (!loaded_sprites_table_.Lookup(name, &result)) {
      return nullptr;
    }
    return result;
  }

  DbAssets::Spritesheet* GetSpritesheet(std::string_view name) const {
    DbAssets::Spritesheet* result = nullptr;
    if (!loaded_spritesheets_table_.Lookup(name, &result)) {
      return nullptr;
    }
    return result;
  }

  Slice<DbAssets::Sprite> GetSprites() const {
    return MakeSlice(loaded_sprites_);
  }

  Slice<DbAssets::Image> GetImages() const { return MakeSlice(loaded_images_); }

  // Returns the GL texture ID for a spritesheet/image by name, or 0.
  GLuint GetTextureByName(std::string_view name) const {
    uint32_t idx;
    if (!textures_table_.Lookup(name, &idx)) return 0;
    // textures_[idx] is a BatchRenderer texture index, not a GLuint.
    return renderer_->GetTextureId(textures_[idx]);
  }

  Slice<DbAssets::Spritesheet> GetSpritesheets() const {
    return MakeSlice(loaded_spritesheets_);
  }

  // Sets the active texture to the spritesheet with the given name.
  bool SetSpritesheetTexture(std::string_view name) {
    uint32_t idx;
    if (!textures_table_.Lookup(name, &idx)) return false;
    SetTextureDedup(textures_[idx]);
    return true;
  }

  // Returns the batch renderer texture index for a spritesheet, or -1.
  int GetSpritesheetTextureIndex(std::string_view name) const {
    uint32_t idx;
    if (!textures_table_.Lookup(name, &idx)) return -1;
    return static_cast<int>(textures_[idx]);
  }

  // Returns the previous color.
  Color SetColor(Color color);

  // Returns the previous width.
  float SetLineWidth(float width);

  void DrawRect(FVec2 top_left, FVec2 bottom_right, float angle);
  // Draws an outlined rectangle using line segments.
  void DrawRectOutline(FVec2 top_left, FVec2 bottom_right, float angle);
  void DrawCircle(FVec2 center, float radius);
  // Draws an outlined circle using line segments.
  void DrawCircleOutline(FVec2 center, float radius);
  void DrawString(std::string_view font_name, uint32_t size,
                  std::string_view str, FVec2 position);
  void DrawLine(FVec2 p0, FVec2 p1);
  void DrawLines(Slice<FVec2> ps);

  IVec2 TextDimensions(std::string_view font_name, uint32_t size,
                       std::string_view str);

  // Text alignment for wrapped text drawing.
  enum class TextAlign : uint8_t { kLeft, kCenter, kRight };

  // Draws word-wrapped text within max_width pixels.
  void DrawStringWrapped(std::string_view font_name, uint32_t size,
                         std::string_view str, FVec2 position, float max_width,
                         TextAlign align);

  // Returns the total height that word-wrapped text would occupy.
  int TextWrappedHeight(std::string_view font_name, uint32_t size,
                        std::string_view str, float max_width);

  // A colored text segment for multi-color text rendering.
  struct ColoredSegment {
    Color color;
    std::string_view text;
  };

  // Draws multi-color text with optional word wrapping and alignment.
  void DrawStringColored(std::string_view font_name, uint32_t size,
                         const ColoredSegment* segments, size_t num_segments,
                         FVec2 position, float max_width, TextAlign align);

  // Sets the outline color and thickness for subsequent SDF text draws.
  void SetTextOutline(Color color, float thickness);

  // Removes the text outline effect.
  void ClearTextOutline();

  void DrawTriangle(FVec2 p1, FVec2 p2, FVec2 p3);
  // Draws an outlined triangle using line segments.
  void DrawTriangleOutline(FVec2 p1, FVec2 p2, FVec2 p3);
  // Draws a filled ellipse with separate x and y radii.
  void DrawEllipse(FVec2 center, float rx, float ry);
  // Draws an outlined ellipse using line segments.
  void DrawEllipseOutline(FVec2 center, float rx, float ry);
  // Draws a filled rounded rectangle with corner radius.
  void DrawRoundedRect(FVec2 top_left, FVec2 bottom_right, float radius);
  // Draws an outlined rounded rectangle using line segments.
  void DrawRoundedRectOutline(FVec2 top_left, FVec2 bottom_right, float radius);

  IVec2 viewport() const { return renderer_->GetViewport(); }

  void Push();

  void Pop();

  void Rotate(float angle) { ApplyTransform(RotationZ(angle)); }
  void Translate(float x, float y) { ApplyTransform(TranslationXY(x, y)); }
  void Scale(float x, float y) { ApplyTransform(ScaleXY(x, y)); }

  void ApplyTransform(const FMat4x4& mat) {
    transform_stack_.back() = transform_stack_.back() * mat;
    renderer_->SetActiveTransform(transform_stack_.back());
  }

 private:
  // Per-glyph SDF atlas data used by the SDF text renderer. Stores the
  // glyph's location in the atlas texture and its positioning metrics.
  // All spatial values are in SDF pixel units and scaled at render time.
  struct SDFGlyph {
    float s0, t0, s1, t1;      // Atlas UV coordinates (normalized 0-1)
    float x_offset, y_offset;  // Top-left offset from pen position (SDF pixels)
    float width, height;       // Glyph quad size (SDF pixels)
    float advance;             // Horizontal advance (SDF pixels)
  };

  struct FontInfo {
    GLuint texture;
    float scale = 0;         // stbtt scale factor for the SDF reference height
    float pixel_height = 0;  // SDF reference rasterization height in pixels
    int ascent;              // Distance from baseline to top of tallest glyph
                             // (unscaled font units; multiply by `scale`)
    int descent;             // Distance from baseline downward (negative,
                             // unscaled font units)
    int line_gap;  // Extra spacing between lines (unscaled font units)
    stbtt_fontinfo font_info;
    // TODO: For Unicode support, replace this fixed array with a hash map
    // (codepoint -> SDFGlyph) to handle arbitrary codepoint ranges.
    SDFGlyph glyphs[128];  // Indexed by codepoint (only 32-126 used)
    int atlas_width, atlas_height;
  };

  // Intermediate storage for a single glyph's SDF bitmap before it's blitted
  // into the atlas.
  struct GlyphBitmap {
    unsigned char* data;
    int w, h, xoff, yoff;
  };

  static void GenerateSDFBitmaps(FontInfo& font, GlyphBitmap* bitmaps,
                                 stbrp_rect* rects, std::string_view name);
  static int PackGlyphRects(stbrp_rect* rects, std::string_view name);
  static uint8_t* BlitGlyphsIntoAtlas(FontInfo& font,
                                      const GlyphBitmap* bitmaps,
                                      const stbrp_rect* rects, int atlas_dim,
                                      ArenaAllocator* scratch);

  ErrorOr<FontInfo> LoadSDFFromCache(sqlite3* db, std::string_view font_name,
                                     uint64_t font_hash);

  static void SaveSDFToCache(sqlite3* db, std::string_view font_name,
                             uint64_t font_hash, const FontInfo& font,
                             const uint8_t* atlas_bitmap);

  // A single line produced by word wrapping.
  struct WrappedLine {
    std::string_view text;  // Substring view into the original text.
    float width;            // Pixel width of this line.
  };

  // Breaks text into lines that fit within max_width pixels.
  void WordWrapLines(const FontInfo& info, float pixel_scale,
                     std::string_view str, float max_width,
                     FixedArray<WrappedLine>* out);

  // Measures the pixel width of a text span using the given font metrics.
  float MeasureSpan(const FontInfo& info, float pixel_scale,
                    std::string_view str);

  Color color_;
  float line_width_;
  Color outline_color_;
  float outline_thickness_ = 0.0f;
  // Tier 2 dedup: last texture unit emitted to the batch renderer.
  // Prevents redundant kSetTexture commands at the source.
  size_t last_texture_ = 0;

  void SetTextureDedup(size_t texture_unit) {
    if (texture_unit == last_texture_) return;
    renderer_->SetActiveTexture(texture_unit);
    last_texture_ = texture_unit;
  }

  void ClearTextureDedup() { SetTextureDedup(renderer_->noop_texture()); }

  Allocator* allocator_;
  BatchRenderer* renderer_;
  sqlite3* db_;

  FixedArray<FMat4x4> transform_stack_;

  Dictionary<uint32_t> textures_table_;
  FixedArray<GLuint> textures_;

  Dictionary<DbAssets::Spritesheet*> loaded_spritesheets_table_;
  FixedArray<DbAssets::Spritesheet> loaded_spritesheets_;

  Dictionary<DbAssets::Sprite*> loaded_sprites_table_;
  FixedArray<DbAssets::Sprite> loaded_sprites_;

  Dictionary<DbAssets::Image*> loaded_images_table_;
  FixedArray<DbAssets::Image> loaded_images_;

  Dictionary<FontInfo*> font_table_;
  FixedArray<FontInfo> fonts_;
};

}  // namespace G

#endif  // _GAME_RENDERER_H
