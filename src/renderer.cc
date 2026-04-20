#include "renderer.h"

#include <cmath>
#include <cstring>

#include "bits.h"
#include "clock.h"
#include "defer.h"
#include "sqlite_helpers.h"
#include "image.h"
#include "libraries/rapidhash.h"
#include "libraries/sqlite3.h"
#include "libraries/stb_rect_pack.h"
#include "profiler.h"
#include "transformations.h"
#include "units.h"

namespace G {
namespace {

// Reference height for SDF rasterization. Higher values capture more glyph
// detail (fine serifs, tight curves) but produce a larger atlas. 80px gives
// good quality from ~8px to 300px+ rendering sizes.
constexpr float kSDFHeight = 80.0f;
// Extra pixels around each glyph for the distance field to extend into.
// Enables outline/glow effects and prevents edge clipping. 6px is enough
// for moderate outlines; use 8 for heavy effects.
constexpr int kPadding = 6;
// Distance value at the glyph boundary. Set to the midpoint of the 0-255
// uint8 range so inside and outside distances have equal resolution.
constexpr unsigned char kOnEdge = 128;
// Maps SDF pixel distances to the 0-255 range within the padding radius.
// A pixel `kPadding` texels from the edge maps to 0 (outside) or 255 (inside).
constexpr float kPixelDistScale = kOnEdge / (float)kPadding;
// ASCII printable range (space through tilde).
constexpr int kFirstChar = 32;
constexpr int kLastChar = 126;
constexpr int kNumChars = kLastChar - kFirstChar + 1;
// Spacing between packed glyphs to prevent texture bleeding during
// bilinear sampling.
constexpr int kAtlasGutter = 2;
// Bump this when SDF generation parameters change to invalidate cached atlases.
constexpr uint64_t kSDFCacheVersion = 1;

}  // namespace

constexpr size_t kCommandMemory = Megabytes(64);

size_t BatchRenderer::GetCommandBufferCapacity() const {
  return kCommandMemory;
}

// Size of each command in command_buffer_: sizeof rounded up to
// alignof(Command) so every command starts on a Command-aligned offset.
// This lets CommandIterator::Read return a const Command* directly into
// the buffer without a memcpy. The hot iterator path is a single load
// from a constexpr table instead of a 22-case switch.
constexpr size_t BatchRenderer::SizeOfCommand(CommandType t) {
  constexpr auto kAlign = alignof(Command);
  constexpr size_t kSizes[] = {
      0,  // unused, CommandType is 1-indexed
      Align(sizeof(RenderQuad), kAlign),
      Align(sizeof(RenderTriangle), kAlign),
      Align(sizeof(StartLine), kAlign),
      Align(sizeof(AddLinePoint), kAlign),
      Align(sizeof(EndLine), kAlign),
      Align(sizeof(SetTexture), kAlign),
      Align(sizeof(SetColor), kAlign),
      Align(sizeof(SetTransform), kAlign),
      Align(sizeof(SetShader), kAlign),
      Align(sizeof(SetLineWidth), kAlign),
      Align(sizeof(SetCanvas), kAlign),
      Align(sizeof(SetBlendMode), kAlign),
      Align(sizeof(ClearColor), kAlign),
      Align(sizeof(SDFOutline), kAlign),
      Align(sizeof(SetScissorRect), kAlign),
      Align(sizeof(ClearScissorRect), kAlign),
      Align(sizeof(BeginStencilWriteCmd), kAlign),
      Align(sizeof(EndStencilWriteCmd), kAlign),
      Align(sizeof(SetStencilTestCmd), kAlign),
      Align(sizeof(ClearStencilTestCmd), kAlign),
      0,  // kDone
  };
  return kSizes[t];
}

constexpr std::string_view BatchRenderer::CommandName(CommandType t) {
  switch (t) {
    case kRenderQuad:
      return "RENDER_QUAD";
    case kRenderTrig:
      return "RENDER_TRIANGLE";
    case kStartLine:
      return "START_LINE";
    case kAddLinePoint:
      return "ADD_LINE_POINT";
    case kEndLine:
      return "END_LINE";
    case kSetTexture:
      return "SET_TEXTURE";
    case kSetColor:
      return "SET_COLOR";
    case kSetTransform:
      return "SET_TRANSFORM";
    case kSetShader:
      return "SET_SHADER";
    case kSetLineWidth:
      return "SET_LINE_WIDTH";
    case kSetCanvas:
      return "SET_CANVAS";
    case kSetBlendMode:
      return "SET_BLEND_MODE";
    case kClearColor:
      return "CLEAR_COLOR";
    case kSetSDFOutline:
      return "SET_SDF_OUTLINE";
    case kSetScissor:
      return "SET_SCISSOR";
    case kClearScissor:
      return "CLEAR_SCISSOR";
    case kBeginStencilWrite:
      return "BEGIN_STENCIL_WRITE";
    case kEndStencilWrite:
      return "END_STENCIL_WRITE";
    case kSetStencilTest:
      return "SET_STENCIL_TEST";
    case kClearStencilTest:
      return "CLEAR_STENCIL_TEST";
    case kDone:
      return "DONE";
  }
  return "";
}

class BatchRenderer::CommandIterator {
 public:
  CommandIterator(uint8_t* buffer, FixedArray<QueueEntry>* commands)
      : commands_(commands), buffer_(buffer), pos_(0) {
    remaining_ = commands_->empty() ? 0 : (*commands_)[0].count;
  }

  // Returns the next command's type and sets *out to a pointer directly
  // into the command buffer. The storage is valid until the iterator
  // advances past this command. Commands are laid out at offsets that
  // are multiples of alignof(Command) (see SizeOfCommand), so the cast
  // is alignment-safe.
  CommandType Read(const Command** out) {
    if (i_ == commands_->size()) return kDone;
    if (remaining_ == 0) {
      i_++;
      if (i_ == commands_->size()) return kDone;
      remaining_ = (*commands_)[i_].count;
    }
    const QueueEntry& e = (*commands_)[i_];
    remaining_--;
    const auto type = static_cast<CommandType>(e.type);
    *out = reinterpret_cast<const Command*>(&buffer_[pos_]);
    pos_ += SizeOfCommand(type);
    return type;
  }

  bool Done() const { return i_ == commands_->size(); }

 private:
  FixedArray<QueueEntry>* commands_;
  uint8_t* buffer_;
  size_t pos_ = 0, remaining_ = 0, i_ = 0;
};

void BatchRenderer::AddCommand(CommandType command, uint32_t count,
                               const void* data, size_t size) {
  if (command != kDone) {
    const size_t aligned = Align(size, alignof(Command));
    if (pos_ + aligned > kCommandMemory) {
      FlushAndContinue();
    }
    std::memcpy(&command_buffer_[pos_], data, size);
    // Advance by the actual byte count written, rounded up to
    // alignof(Command), so the next command starts on a Command-aligned
    // offset (CommandIterator::Read returns pointers directly into this
    // buffer and assumes alignment). `size` is per-call total bytes:
    // `count * sizeof(T)` for batched multi-element commands like
    // PushLinePoints, so we can't use SizeOfCommand here.
    pos_ += aligned;
  }
  // Check if we need a new queue entry or can merge with the previous one.
  bool needs_new_entry = commands_.empty() ||
                         commands_.back().type != command ||
                         commands_.back().count == kMaxCount;
  if (needs_new_entry) {
    if (commands_.size() == commands_.capacity()) {
      FlushAndContinue();
    }
    commands_.Push(QueueEntry{.type = command, .count = count});
  } else {
    commands_.back().count += count;
  }
}

BatchRenderer::BatchRenderer(IVec2 viewport, Shaders* shaders,
                             Allocator* allocator)
    : allocator_(allocator),
      command_buffer_(static_cast<uint8_t*>(
          allocator->Alloc(kCommandMemory, alignof(Command)))),
      commands_(1 << 20, allocator),
      tex_(256, allocator),
      shaders_(shaders),
      viewport_(viewport),
      window_size_(viewport),
      render_scratch_(allocator, Megabytes(64)) {
  TIMER();
  glGetIntegerv(GL_MAX_SAMPLES, &antialiasing_samples_);
  LOG("Using ", antialiasing_samples_, " MSAA samples");
  LOG("Using viewport = ", viewport.x, " ", viewport.y);
  OPENGL_CALL(glGenVertexArrays(1, &vao_));
  OPENGL_CALL(glGenBuffers(1, &vbo_));
  OPENGL_CALL(glGenBuffers(1, &ebo_));
  // Generate the quad for the post pass step.
  OPENGL_CALL(glGenVertexArrays(1, &screen_quad_vao_));
  OPENGL_CALL(glGenBuffers(1, &screen_quad_vbo_));
  OPENGL_CALL(glBindVertexArray(screen_quad_vao_));
  std::array<float, 24> screen_quad_vertices = {
      // Vertex position and Tex coord in Normalized Device Coordinates.
      -1.0f, 1.0f,  0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f,
      1.0f,  -1.0f, 1.0f, 0.0f, -1.0f, 1.0f,  0.0f, 1.0f,
      1.0f,  -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  1.0f, 1.0f};
  OPENGL_CALL(glBindBuffer(GL_ARRAY_BUFFER, screen_quad_vbo_));
  OPENGL_CALL(glBufferData(GL_ARRAY_BUFFER,
                           screen_quad_vertices.size() * sizeof(float),
                           screen_quad_vertices.data(), GL_STATIC_DRAW));
  shaders_->UseProgram("post_pass");
  const GLint pos_attribute = shaders_->AttributeLocation("input_position");
  OPENGL_CALL(glEnableVertexAttribArray(pos_attribute));
  OPENGL_CALL(glVertexAttribPointer(pos_attribute, 2, GL_FLOAT, GL_FALSE,
                                    4 * sizeof(float), (void*)0));
  const GLint tex_attribute = shaders_->AttributeLocation("input_tex_coord");
  OPENGL_CALL(glEnableVertexAttribArray(tex_attribute));
  OPENGL_CALL(glVertexAttribPointer(tex_attribute, 2, GL_FLOAT, GL_FALSE,
                                    4 * sizeof(float),
                                    (void*)(2 * sizeof(float))));
  InitializeFramebuffers();
  rec_canvas_ = {render_target_, viewport_.x, viewport_.y};
  // Load an empty texture, just white pixels, to be able to draw colors without
  // if statements in the shader.
  uint8_t white_pixels[32 * 32 * 4];
  std::memset(white_pixels, 255, sizeof(white_pixels));
  noop_texture_ = LoadTexture(&white_pixels, /*width=*/32, /*height=*/32);
  SetActiveTexture(noop_texture_);
}

void BatchRenderer::InitializeFramebuffers() {
  // Create a render target for the viewport.
  OPENGL_CALL(glGenFramebuffers(1, &render_target_));
  OPENGL_CALL(glGenFramebuffers(1, &downsampled_target_));
  OPENGL_CALL(glGenTextures(1, &render_texture_));
  OPENGL_CALL(glGenTextures(1, &downsampled_texture_));
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, render_target_));
  OPENGL_CALL(glActiveTexture(GL_TEXTURE0));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, render_texture_));
  OPENGL_CALL(glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
                                      antialiasing_samples_, GL_RGBA,
                                      viewport_.x, viewport_.y, GL_TRUE));
  OPENGL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_2D_MULTISAMPLE, render_texture_,
                                     /*level=*/0));
  // Attach a multisampled depth/stencil renderbuffer to the MSAA render target
  // so stencil operations work during the main rendering pass.
  OPENGL_CALL(glGenRenderbuffers(1, &depth_buffer_));
  OPENGL_CALL(glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer_));
  OPENGL_CALL(glRenderbufferStorageMultisample(
      GL_RENDERBUFFER, antialiasing_samples_, GL_DEPTH24_STENCIL8, viewport_.x,
      viewport_.y));
  OPENGL_CALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                        GL_DEPTH_STENCIL_ATTACHMENT,
                                        GL_RENDERBUFFER, depth_buffer_));
  CHECK(!glGetError(), "Could generate render texture: ", glGetError());
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "Render target framebuffer incomplete: ",
        glCheckFramebufferStatus(GL_FRAMEBUFFER));
  // Create downsampled texture data.
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, downsampled_target_));
  OPENGL_CALL(glActiveTexture(GL_TEXTURE1));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, downsampled_texture_));
  OPENGL_CALL(glTexImage2D(
      GL_TEXTURE_2D, /*level=*/0, GL_RGBA, viewport_.x, viewport_.y,
      /*border=*/0, GL_RGBA, GL_UNSIGNED_BYTE, /*pixels=*/nullptr));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
  OPENGL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_2D, downsampled_texture_,
                                     /*level=*/0));
  CHECK(!glGetError(), "Could generate downsampled texture: ", glGetError());
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "Downsampled target framebuffer incomplete: ",
        glCheckFramebufferStatus(GL_FRAMEBUFFER));
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
  glActiveTexture(GL_TEXTURE0);
}

void BatchRenderer::SetViewport(IVec2 viewport) {
  if (viewport_ == viewport) return;
  LOG("Resizing viewport from ", viewport_, " to ", viewport);
  viewport_ = viewport;
  // Delete the framebuffers, renderbuffer, and textures, then recreate them.
  std::array<GLuint, 2> frame_buffers = {render_target_, downsampled_target_};
  OPENGL_CALL(glDeleteFramebuffers(frame_buffers.size(), frame_buffers.data()));
  OPENGL_CALL(glDeleteRenderbuffers(1, &depth_buffer_));
  std::array<GLuint, 2> render_target_textures = {render_texture_,
                                                  downsampled_texture_};
  OPENGL_CALL(glDeleteTextures(render_target_textures.size(),
                               render_target_textures.data()));
  InitializeFramebuffers();
}

size_t BatchRenderer::LoadTexture(const DbAssets::Image& image) {
  TIMER("Decoding ", image.name);
  QoiDesc desc;
  constexpr int kChannels = 4;
  auto* image_bytes =
      QoiDecode(image.contents, image.size, &desc, kChannels, allocator_);
  size_t index = LoadTexture(image_bytes, image.width, image.height);
  allocator_->Dealloc(image_bytes, image.width * image.height * kChannels);
  return index;
}

BatchRenderer::~BatchRenderer() {
  std::array<GLuint, 3> object_buffers = {vbo_, ebo_, screen_quad_vbo_};
  OPENGL_CALL(glDeleteBuffers(object_buffers.size(), object_buffers.data()));
  std::array<GLuint, 2> frame_buffers = {render_target_, downsampled_target_};
  OPENGL_CALL(glDeleteFramebuffers(frame_buffers.size(), frame_buffers.data()));
  OPENGL_CALL(glDeleteRenderbuffers(1, &depth_buffer_));
  OPENGL_CALL(glDeleteVertexArrays(1, &vao_));
  OPENGL_CALL(glDeleteVertexArrays(1, &screen_quad_vao_));
  std::array<GLuint, 2> render_target_textures = {render_texture_,
                                                  downsampled_texture_};
  OPENGL_CALL(glDeleteTextures(render_target_textures.size(),
                               render_target_textures.data()));
  OPENGL_CALL(glDeleteTextures(tex_.size(), tex_.data()));
  allocator_->Dealloc(command_buffer_, kCommandMemory);
}

size_t BatchRenderer::LoadTexture(const void* data, size_t width,
                                  size_t height) {
  GLuint tex;
  const size_t index = tex_.size();
  OPENGL_CALL(glGenTextures(1, &tex));
  OPENGL_CALL(glActiveTexture(GL_TEXTURE0 + index));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                              GL_LINEAR_MIPMAP_LINEAR));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
  OPENGL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                           GL_UNSIGNED_BYTE, data));
  OPENGL_CALL(glGenerateMipmap(GL_TEXTURE_2D));
  CHECK(!glGetError(), "Could generate texture: ", glGetError());
  tex_.Push(tex);
  return index;
}

size_t BatchRenderer::LoadFontTexture(const void* data, size_t width,
                                      size_t height) {
  GLuint tex;
  const size_t index = tex_.size();
  OPENGL_CALL(glGenTextures(1, &tex));
  OPENGL_CALL(glActiveTexture(GL_TEXTURE0 + index));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
  OPENGL_CALL(
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
  OPENGL_CALL(
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED));
  OPENGL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED,
                           GL_UNSIGNED_BYTE, data));
  CHECK(!glGetError(), "Could generate texture: ", glGetError());
  tex_.Push(tex);
  return index;
}

size_t BatchRenderer::RegisterTexture(GLuint tex) {
  const size_t index = tex_.size();
  OPENGL_CALL(glActiveTexture(GL_TEXTURE0 + index));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
  tex_.Push(tex);
  return index;
}

Canvas BatchRenderer::CreateCanvas(int width, int height, bool nearest_filter) {
  Canvas c;
  c.width = width;
  c.height = height;

  // Create a framebuffer object to serve as the off-screen render target.
  OPENGL_CALL(glGenFramebuffers(1, &c.fbo));
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, c.fbo));

  // Allocate a color texture and configure filtering/wrapping.
  OPENGL_CALL(glGenTextures(1, &c.texture));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, c.texture));
  OPENGL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                           GL_UNSIGNED_BYTE, nullptr));
  const GLint filter = nearest_filter ? GL_NEAREST : GL_LINEAR;
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter));
  OPENGL_CALL(
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
  OPENGL_CALL(
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

  // Attach the texture to the FBO as its color output and verify completeness.
  OPENGL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_2D, c.texture, 0));
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "Canvas framebuffer incomplete: ",
        glCheckFramebufferStatus(GL_FRAMEBUFFER));

  // Clear the canvas to transparent.
  OPENGL_CALL(glClearColor(0.f, 0.f, 0.f, 0.f));
  OPENGL_CALL(glClear(GL_COLOR_BUFFER_BIT));

  // Restore the main render target.
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, render_target_));

  // Register the texture so it can be sampled during rendering.
  c.texture_unit = RegisterTexture(c.texture);
  return c;
}

void Canvas::Destroy() {
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
}

void BatchRenderer::SetupGLState() {
  OPENGL_CALL(glEnable(GL_MULTISAMPLE));
  OPENGL_CALL(glViewport(0, 0, viewport_.x, viewport_.y));
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, render_target_));
  OPENGL_CALL(glEnable(GL_BLEND));
  OPENGL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
  OPENGL_CALL(glBlendEquation(GL_FUNC_ADD));
  OPENGL_CALL(glDisable(GL_DEPTH_TEST));
  OPENGL_CALL(glDisable(GL_STENCIL_TEST));
  OPENGL_CALL(glStencilMask(0x00));
  OPENGL_CALL(glEnable(GL_LINE_SMOOTH));
  if (needs_clear_) {
    OPENGL_CALL(glClearColor(0.f, 0.f, 0.f, 0.f));
    OPENGL_CALL(glStencilMask(0xFF));
    OPENGL_CALL(glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
    OPENGL_CALL(glStencilMask(0x00));
    needs_clear_ = false;
  }
}

void BatchRenderer::FlushAndContinue() {
  Finish();
  SetupGLState();
  RenderBatch();
  commands_.Clear();
  pos_ = 0;
  ReEmitState();
  flush_overflow_++;
}

void BatchRenderer::ReEmitState() {
  AddCommand(kSetColor, SetColor{rec_color_});
  AddCommand(kSetTexture, SetTexture{rec_texture_});
  AddCommand(kSetTransform, SetTransform{rec_transform_});
  if (current_shader_ != 0) {
    AddCommand(kSetShader, SetShader{current_shader_});
  }
  AddCommand(kSetBlendMode, SetBlendMode{rec_blend_});
  AddCommand(kSetLineWidth, SetLineWidth{rec_line_width_});
  if (rec_canvas_.fbo != render_target_) {
    AddCommand(kSetCanvas, rec_canvas_);
  }
  if (rec_sdf_outline_.thickness > 0) {
    AddCommand(kSetSDFOutline, rec_sdf_outline_);
  }
  if (rec_scissor_enabled_) {
    AddCommand(kSetScissor, rec_scissor_);
  }
  if (rec_stencil_write_active_) {
    AddCommand(kBeginStencilWrite, rec_stencil_write_);
  }
  if (rec_stencil_test_active_) {
    AddCommand(kSetStencilTest, rec_stencil_test_);
  }
}

void BatchRenderer::RenderBatch() {
  render_scratch_.Reset();
  Allocator* scratch = &render_scratch_;
  // Compute size of data.
  size_t vertices_count = 0, indices_count = 0;
  for (CommandIterator it(command_buffer_, &commands_); !it.Done();) {
    const Command* c;
    switch (it.Read(&c)) {
      case kRenderQuad:
        vertices_count += 4;
        indices_count += 6;
        break;
      case kRenderTrig:
        vertices_count += 3;
        indices_count += 3;
        break;
      case kAddLinePoint:
        vertices_count += 1;
        indices_count += 1;
        break;
      default:
        // Other commands do not add vertices.
        break;
    }
  }
  FixedArray<VertexData> vertices(vertices_count, scratch);
  FixedArray<GLuint> indices(indices_count, scratch);
  // Add data.
  Color color = Color::White();
  for (CommandIterator it(command_buffer_, &commands_); !it.Done();) {
    size_t current = vertices.size();
    const Command* c;
    switch (it.Read(&c)) {
      case kRenderQuad: {
        const RenderQuad& q = c->quad;
        vertices.Push({.position = FVec(q.p0.x, q.p1.y),
                       .tex_coords = FVec(q.q0.x, q.q1.y),
                       .origin = q.origin,
                       .angle = q.angle,
                       .color = color});
        vertices.Push({.position = FVec(q.p1.x, q.p1.y),
                       .tex_coords = q.q1,
                       .origin = q.origin,
                       .angle = q.angle,
                       .color = color});
        vertices.Push({.position = FVec(q.p1.x, q.p0.y),
                       .tex_coords = FVec(q.q1.x, q.q0.y),
                       .origin = q.origin,
                       .angle = q.angle,
                       .color = color});
        vertices.Push({.position = FVec(q.p0.x, q.p0.y),
                       .tex_coords = q.q0,
                       .origin = q.origin,
                       .angle = q.angle,
                       .color = color});
        for (int i : {0, 1, 3, 1, 2, 3}) {
          indices.Push(current + i);
        }
      }; break;
      case kRenderTrig: {
        const RenderTriangle& t = c->triangle;
        vertices.Push({.position = FVec(t.p0.x, t.p0.y),
                       .tex_coords = t.q0,
                       .origin = FVec(0, 0),
                       .angle = 0,
                       .color = color});
        vertices.Push({.position = FVec(t.p1.x, t.p1.y),
                       .tex_coords = t.q1,
                       .origin = FVec(0, 0),
                       .angle = 0,
                       .color = color});
        vertices.Push({.position = FVec(t.p2.x, t.p2.y),
                       .tex_coords = t.q2,
                       .origin = FVec(0, 0),
                       .angle = 0,
                       .color = color});
        for (int i : {0, 1, 2}) {
          indices.Push(current + i);
        }
      }; break;
      case kAddLinePoint: {
        const AddLinePoint& l = c->add_line_point;
        vertices.Push({.position = l.p0,
                       .tex_coords = FVec(0, 0),
                       .origin = FVec(0, 0),
                       .angle = 0,
                       .color = color});
        indices.Push(current);
      }; break;
      case kSetColor:
        color = c->set_color.color;
        break;
      default:
        // Other commands do not add vertices.
        break;
    }
  }
  // Setup OpenGL context.
  OPENGL_CALL(glBindVertexArray(vao_));
  OPENGL_CALL(glBindBuffer(GL_ARRAY_BUFFER, vbo_));
  OPENGL_CALL(glBufferData(GL_ARRAY_BUFFER, vertices.bytes(), vertices.data(),
                           GL_STATIC_DRAW));
  OPENGL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_));
  OPENGL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.bytes(),
                           indices.data(), GL_STATIC_DRAW));
  auto set_program_state = [&](std::string_view program_name) {
    shaders_->UseProgram(program_name);
    const GLint pos_attribute = shaders_->AttributeLocation("input_position");
    if (pos_attribute != -1) {
      OPENGL_CALL(glVertexAttribPointer(
          pos_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
          sizeof(VertexData),
          reinterpret_cast<void*>(offsetof(VertexData, position))));
      OPENGL_CALL(glEnableVertexAttribArray(pos_attribute));
    }
    const GLint tex_coord_attribute =
        shaders_->AttributeLocation("input_tex_coord");
    if (tex_coord_attribute != -1) {
      OPENGL_CALL(glVertexAttribPointer(
          tex_coord_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
          sizeof(VertexData),
          reinterpret_cast<void*>(offsetof(VertexData, tex_coords))));
      OPENGL_CALL(glEnableVertexAttribArray(tex_coord_attribute));
    }
    const GLint origin_attribute = shaders_->AttributeLocation("origin");
    if (origin_attribute != -1) {
      OPENGL_CALL(glVertexAttribPointer(
          origin_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
          sizeof(VertexData),
          reinterpret_cast<void*>(offsetof(VertexData, origin))));
      OPENGL_CALL(glEnableVertexAttribArray(origin_attribute));
    }
    const GLint angle_attribute = shaders_->AttributeLocation("angle");
    if (angle_attribute != -1) {
      OPENGL_CALL(glVertexAttribPointer(
          angle_attribute, 1, GL_FLOAT, GL_FALSE, sizeof(VertexData),
          reinterpret_cast<void*>(offsetof(VertexData, angle))));
      OPENGL_CALL(glEnableVertexAttribArray(angle_attribute));
    }
    const GLint color_attribute = shaders_->AttributeLocation("color");
    if (color_attribute != -1) {
      OPENGL_CALL(glVertexAttribPointer(
          color_attribute, sizeof(Color), GL_UNSIGNED_BYTE, GL_FALSE,
          sizeof(VertexData),
          reinterpret_cast<void*>(offsetof(VertexData, color))));
      OPENGL_CALL(glEnableVertexAttribArray(color_attribute));
    }
    shaders_->SetUniformSilent("global_color", color.ToFloat());
  };
  uint32_t current_shader_handle = current_shader_;
  set_program_state("pre_pass");
  // Render batches by finding changes to the OpenGL context.
  FrameStats stats = {};
  size_t indices_start = 0;
  size_t indices_end = 0;
  GLuint texture_unit = 0;
  FMat4x4 transform = FMat4x4::Identity();
  GLint primitives = GL_TRIANGLES;
  float line_width = 2.5;
  BlendMode blend_mode = BLEND_ALPHA;
  float sdf_thickness = 0.0f;
  float sdf_r = 0, sdf_g = 0, sdf_b = 0, sdf_a = 0;
  GLuint current_fbo = render_target_;
  int current_viewport_w = viewport_.x;
  int current_viewport_h = viewport_.y;
  const Command* c;
  for (CommandIterator it(command_buffer_, &commands_); !it.Done();) {
    auto flush = [&] {
      if (indices_start == indices_end) return;
      glLineWidth(line_width);
      glActiveTexture(GL_TEXTURE0 + texture_unit);
      shaders_->SetUniformSilent("tex", texture_unit);
      shaders_->SetUniformSilent(
          "projection", Ortho(0, current_viewport_w, 0, current_viewport_h));
      shaders_->SetUniformSilent("transform", transform);
      shaders_->SetUniformSilent("g_ScreenSize",
                                 FVec(current_viewport_w, current_viewport_h));
      shaders_->SetUniformSilentF("g_Time", frame_time_);
      OPENGL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_));
      OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, tex_[texture_unit]));
      const auto indices_start_ptr =
          reinterpret_cast<uintptr_t>(&indices[indices_start]) -
          reinterpret_cast<uintptr_t>(&indices[0]);
      OPENGL_CALL(glDrawElementsInstanced(
          primitives, indices_end - indices_start, GL_UNSIGNED_INT,
          reinterpret_cast<void*>(indices_start_ptr), 1));
      stats.draw_calls++;
      indices_start = indices_end;
    };
    stats.commands++;
    switch (it.Read(&c)) {
      case kRenderQuad:
        if (primitives != GL_TRIANGLES) flush();
        primitives = GL_TRIANGLES;
        indices_end += 6;
        break;
      case kRenderTrig:
        if (primitives != GL_TRIANGLES) flush();
        primitives = GL_TRIANGLES;
        indices_end += 3;
        break;
      case kStartLine:
        if (primitives != GL_LINE_STRIP) flush();
        primitives = GL_LINE_STRIP;
        break;
      case kAddLinePoint:
        indices_end += 1;
        break;
      case kEndLine:
        flush();
        stats.flush_line_end++;
        break;
      case kSetTransform:
        if (c->set_transform.transform == transform) {
          stats.redundant_transform++;
          break;
        }
        flush();
        stats.flush_transform++;
        transform = c->set_transform.transform;
        break;
      case kSetTexture:
        if (c->set_texture.texture_unit == texture_unit) {
          stats.redundant_texture++;
          break;
        }
        flush();
        stats.flush_texture++;
        texture_unit = c->set_texture.texture_unit;
        break;
      case kSetShader:
        if (c->set_shader.shader_handle == current_shader_handle) {
          stats.redundant_shader++;
          break;
        }
        flush();
        stats.flush_shader++;
        current_shader_handle = c->set_shader.shader_handle;
        set_program_state(StringByHandle(c->set_shader.shader_handle));
        break;
      case kSetLineWidth:
        if (c->set_line_width.width == line_width) {
          stats.redundant_line_width++;
          break;
        }
        flush();
        stats.flush_other++;
        line_width = c->set_line_width.width;
        break;
      case kSetCanvas:
        flush();
        stats.flush_canvas++;
        OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, c->set_canvas.fbo));
        OPENGL_CALL(
            glViewport(0, 0, c->set_canvas.width, c->set_canvas.height));
        current_fbo = c->set_canvas.fbo;
        current_viewport_w = c->set_canvas.width;
        current_viewport_h = c->set_canvas.height;
        break;
      case kSetBlendMode:
        if (c->set_blend_mode.mode == blend_mode) {
          stats.redundant_blend++;
          break;
        }
        flush();
        stats.flush_blend++;
        blend_mode = c->set_blend_mode.mode;
        switch (c->set_blend_mode.mode) {
          case BLEND_ALPHA:
            OPENGL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
            break;
          case BLEND_ADD:
            OPENGL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE));
            break;
          case BLEND_MULTIPLY:
            OPENGL_CALL(glBlendFunc(GL_DST_COLOR, GL_ZERO));
            break;
          case BLEND_REPLACE:
            OPENGL_CALL(glBlendFunc(GL_ONE, GL_ZERO));
            break;
          case BLEND_PREMULTIPLIED:
            OPENGL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
            break;
        }
        break;
      case kClearColor:
        flush();
        stats.flush_other++;
        OPENGL_CALL(glClearColor(c->clear_color.r, c->clear_color.g,
                                 c->clear_color.b, c->clear_color.a));
        // Clear stencil alongside color so each clear() resets mask state.
        OPENGL_CALL(glStencilMask(0xFF));
        OPENGL_CALL(glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
        OPENGL_CALL(glStencilMask(0x00));
        break;
      case kSetColor:
        color = c->set_color.color;
        break;
      case kSetSDFOutline:
        if (c->sdf_outline.thickness == sdf_thickness &&
            c->sdf_outline.r == sdf_r && c->sdf_outline.g == sdf_g &&
            c->sdf_outline.b == sdf_b && c->sdf_outline.a == sdf_a) {
          stats.redundant_sdf_outline++;
          break;
        }
        flush();
        stats.flush_other++;
        sdf_thickness = c->sdf_outline.thickness;
        sdf_r = c->sdf_outline.r;
        sdf_g = c->sdf_outline.g;
        sdf_b = c->sdf_outline.b;
        sdf_a = c->sdf_outline.a;
        shaders_->SetUniformSilentF("u_outline_thickness",
                                    c->sdf_outline.thickness);
        shaders_->SetUniformSilent("u_outline_color",
                                   FVec(c->sdf_outline.r, c->sdf_outline.g,
                                        c->sdf_outline.b, c->sdf_outline.a));
        break;
      case kSetScissor:
        flush();
        stats.flush_other++;
        OPENGL_CALL(glEnable(GL_SCISSOR_TEST));
        // OpenGL scissor Y is bottom-up; convert from top-left origin.
        OPENGL_CALL(
            glScissor(c->set_scissor.x,
                      current_viewport_h - c->set_scissor.y - c->set_scissor.h,
                      c->set_scissor.w, c->set_scissor.h));
        break;
      case kClearScissor:
        flush();
        stats.flush_other++;
        OPENGL_CALL(glDisable(GL_SCISSOR_TEST));
        break;
      case kBeginStencilWrite:
        flush();
        stats.flush_other++;
        OPENGL_CALL(glEnable(GL_STENCIL_TEST));
        OPENGL_CALL(glStencilFunc(GL_ALWAYS, c->begin_stencil_write.ref, 0xFF));
        OPENGL_CALL(
            glStencilOp(GL_KEEP, GL_KEEP, c->begin_stencil_write.action));
        OPENGL_CALL(glStencilMask(0xFF));
        // Disable color writes so stencil geometry is invisible.
        OPENGL_CALL(glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));
        break;
      case kEndStencilWrite:
        flush();
        stats.flush_other++;
        OPENGL_CALL(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
        OPENGL_CALL(glStencilMask(0x00));
        OPENGL_CALL(glDisable(GL_STENCIL_TEST));
        break;
      case kSetStencilTest:
        flush();
        stats.flush_other++;
        OPENGL_CALL(glEnable(GL_STENCIL_TEST));
        OPENGL_CALL(glStencilFunc(c->set_stencil_test.compare,
                                  c->set_stencil_test.ref, 0xFF));
        OPENGL_CALL(glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP));
        OPENGL_CALL(glStencilMask(0x00));
        break;
      case kClearStencilTest:
        flush();
        stats.flush_other++;
        OPENGL_CALL(glDisable(GL_STENCIL_TEST));
        break;
      case kDone:
        color = Color::White();
        flush();
        break;
    }
  }
  // Ensure we're back on the main render target after canvas switches.
  if (current_fbo != render_target_) {
    OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, render_target_));
    OPENGL_CALL(glViewport(0, 0, viewport_.x, viewport_.y));
  }
  AccumulateStats(stats, static_cast<int>(vertices_count));
}

void BatchRenderer::AccumulateStats(const FrameStats& stats,
                                    int vertices_count) {
  frame_stats_.draw_calls += stats.draw_calls;
  frame_stats_.vertices += vertices_count;
  frame_stats_.commands += stats.commands;
  frame_stats_.flush_texture += stats.flush_texture;
  frame_stats_.flush_transform += stats.flush_transform;
  frame_stats_.flush_shader += stats.flush_shader;
  frame_stats_.flush_blend += stats.flush_blend;
  frame_stats_.flush_canvas += stats.flush_canvas;
  frame_stats_.flush_line_end += stats.flush_line_end;
  frame_stats_.flush_other += stats.flush_other;
  frame_stats_.redundant_texture += stats.redundant_texture;
  frame_stats_.redundant_transform += stats.redundant_transform;
  frame_stats_.redundant_shader += stats.redundant_shader;
  frame_stats_.redundant_blend += stats.redundant_blend;
  frame_stats_.redundant_line_width += stats.redundant_line_width;
  frame_stats_.redundant_sdf_outline += stats.redundant_sdf_outline;
}

void BatchRenderer::Render() {
  PROFILE_SCOPE;
  frame_stats_ = {};
  SetupGLState();
  RenderBatch();
  frame_stats_.flush_overflow = flush_overflow_;
  // MSAA resolve: downsample from multisampled to regular framebuffer.
  OPENGL_CALL(glActiveTexture(GL_TEXTURE0));
  OPENGL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, render_target_));
  OPENGL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, downsampled_target_));
  OPENGL_CALL(glBlitFramebuffer(0, 0, viewport_.x, viewport_.y, 0, 0,
                                viewport_.x, viewport_.y, GL_COLOR_BUFFER_BIT,
                                GL_NEAREST));
  // Post pass: draw to screen. Use window_size_ so the game stretches to
  // fill the window even when the viewport is smaller.
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
  OPENGL_CALL(glClearColor(0.f, 0.f, 0.f, 0.f));
  OPENGL_CALL(glClear(GL_COLOR_BUFFER_BIT));
  shaders_->UseProgram("post_pass");
  glActiveTexture(GL_TEXTURE1);
  shaders_->SetUniformSilent("screen_texture", 1);
  OPENGL_CALL(glBindVertexArray(screen_quad_vao_));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, downsampled_texture_));
  OPENGL_CALL(glViewport(0, 0, window_size_.x, window_size_.y));
  OPENGL_CALL(glDrawArrays(GL_TRIANGLES, 0, 6));
  frame_stats_.draw_calls++;
  PROFILE_COUNTER("Draw Calls", frame_stats_.draw_calls);
  PROFILE_COUNTER("Vertices", static_cast<double>(frame_stats_.vertices));
  PROFILE_COUNTER("Flush: Texture", frame_stats_.flush_texture);
  PROFILE_COUNTER("Flush: Transform", frame_stats_.flush_transform);
  PROFILE_COUNTER("Flush: Shader", frame_stats_.flush_shader);
  PROFILE_COUNTER("Flush: Blend Mode", frame_stats_.flush_blend);
  PROFILE_COUNTER("Flush: Canvas", frame_stats_.flush_canvas);
  PROFILE_COUNTER("Flush: Line End", frame_stats_.flush_line_end);
  PROFILE_COUNTER("Flush: Other", frame_stats_.flush_other);
  PROFILE_COUNTER("Flush: Overflow", frame_stats_.flush_overflow);
  PROFILE_COUNTER("Redundant: Texture", frame_stats_.redundant_texture);
  PROFILE_COUNTER("Redundant: Transform", frame_stats_.redundant_transform);
  PROFILE_COUNTER("Redundant: Shader", frame_stats_.redundant_shader);
}

BatchRenderer::Screenshot BatchRenderer::TakeScreenshot(
    Allocator* allocator) const {
  Screenshot result;
  const IVec2 viewport = GetViewport();
  size_t bytes = viewport.x;
  bytes *= viewport.y * sizeof(Color);
  auto* buffer = allocator->Alloc(bytes, /*align=*/4);
  // Read from the resolved (non-MSAA) framebuffer.
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, downsampled_target_));
  glReadnPixels(0, 0, viewport.x, viewport.y, GL_RGBA, GL_UNSIGNED_BYTE, bytes,
                buffer);
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
  // Flip the rows.
  ArenaAllocator scratch(allocator, Megabytes(1));
  const size_t row_size = viewport.x * sizeof(Color);
  auto* temp = reinterpret_cast<uint8_t*>(scratch.Alloc(row_size, /*align=*/4));
  auto* image = reinterpret_cast<uint8_t*>(buffer);
  for (int i = 0, j = viewport.y - 1; i < j; ++i, --j) {
    std::memcpy(temp, &image[j * row_size], row_size);
    std::memcpy(&image[j * row_size], &image[i * row_size], row_size);
    std::memcpy(&image[i * row_size], temp, row_size);
  }
  result.buffer = image;
  result.width = viewport.x;
  result.height = viewport.y;
  return result;
}

Renderer::Renderer(const DbAssets& assets, BatchRenderer* renderer, sqlite3* db,
                   Allocator* allocator)
    : allocator_(allocator),
      renderer_(renderer),
      db_(db),
      transform_stack_(128, allocator),
      textures_table_(allocator),
      textures_(256, allocator),
      loaded_spritesheets_table_(allocator),
      loaded_spritesheets_(1 << 16, allocator),
      loaded_sprites_table_(allocator),
      loaded_sprites_(1 << 20, allocator),
      loaded_images_table_(allocator),
      loaded_images_(1 << 10, allocator),
      font_table_(allocator),
      fonts_(512, allocator) {}

void Renderer::ClearForFrame() {
  renderer_->Clear();
  transform_stack_.Clear();
  transform_stack_.Push(FMat4x4::Identity());
  ApplyTransform(FMat4x4::Identity());
  SetColor(Color::White());
  last_texture_ = 0;
}

void Renderer::Push() { transform_stack_.Push(transform_stack_.back()); }

void Renderer::Pop() {
  transform_stack_.Pop();
  renderer_->SetActiveTransform(transform_stack_.back());
}

Color Renderer::SetColor(Color color) {
  auto result = color_;
  color_ = color;
  renderer_->SetActiveColor(color);
  return result;
}

float Renderer::SetLineWidth(float width) {
  auto result = line_width_;
  line_width_ = width;
  renderer_->SetActiveLineWidth(width);
  return result;
}

ErrorOr<void> Renderer::LoadSprite(const DbAssets::Sprite& sprite) {
  if (!textures_table_.Contains(sprite.spritesheet)) {
    return Error::Message("unknown spritesheet for sprite");
  }
  loaded_sprites_.Push(sprite);
  loaded_sprites_table_.Insert(sprite.name, &loaded_sprites_.back());
  return {};
}

void Renderer::LoadTexture(const DbAssets::Image& image) {
  if (!textures_table_.Contains(image.name)) {
    LOG("Loading texture for image ", image.name);
    textures_table_.Insert(image.name, textures_.size());
    textures_.Push(renderer_->LoadTexture(image));
  }
  loaded_images_.Push(image);
  loaded_images_table_.Insert(image.name, &loaded_images_.back());
}

ErrorOr<void> Renderer::LoadSpritesheet(
    const DbAssets::Spritesheet& spritesheet) {
  DbAssets::Image* image;
  if (!loaded_images_table_.Lookup(spritesheet.image, &image) ||
      image == nullptr) {
    return Error::Message("unknown image for spritesheet");
  }
  LOG("Loading texture ", spritesheet.name);
  textures_table_.Insert(spritesheet.name, textures_.size());
  textures_.Push(renderer_->LoadTexture(*image));
  loaded_spritesheets_.Push(spritesheet);
  loaded_spritesheets_table_.Insert(spritesheet.name,
                                    &loaded_spritesheets_.back());
  return {};
}

ErrorOr<void> Renderer::DrawSprite(std::string_view sprite_name, FVec2 position,
                                   float angle) {
  DbAssets::Sprite* sprite = nullptr;
  if (!loaded_sprites_table_.Lookup(sprite_name, &sprite)) {
    return Error::Message("sprite not found");
  }
  return DrawSprite(*sprite, position, angle);
}

ErrorOr<void> Renderer::DrawSprite(const DbAssets::Sprite& sprite,
                                   FVec2 position, float angle) {
  DbAssets::Spritesheet* spritesheet;
  if (!loaded_spritesheets_table_.Lookup(sprite.spritesheet, &spritesheet)) {
    return Error::Message("spritesheet not found");
  }
  uint32_t texture_index;
  CHECK(textures_table_.Lookup(spritesheet->name, &texture_index),
        "No spritesheet texture for ", sprite.name, "(spritesheet ",
        spritesheet->name, ")");
  SetTextureDedup(textures_[texture_index]);
  const float x = sprite.x, y = sprite.y, w = sprite.width, h = sprite.height;
  const FVec2 p0(position - FVec(w / 2.0, h / 2.0));
  const FVec2 p1(position + FVec(w / 2.0, h / 2.0));
  const FVec2 q0(
      FVec(1.0 * x / spritesheet->width, 1.0 * y / spritesheet->height));
  const FVec2 q1(1.0f * (x + w) / spritesheet->width,
                 1.0f * (y + h) / spritesheet->height);
  renderer_->PushQuad(p0, p1, q0, q1, position, angle);
  return {};
}

ErrorOr<void> Renderer::DrawImage(std::string_view image_name, FVec2 position,
                                  float angle) {
  DbAssets::Image* image = nullptr;
  if (!loaded_images_table_.Lookup(image_name, &image)) {
    return Error::Message("image not found");
  }
  return DrawImage(*image, position, angle);
}

ErrorOr<void> Renderer::DrawImage(const DbAssets::Image& image, FVec2 position,
                                  float angle) {
  uint32_t texture_index;
  CHECK(textures_table_.Lookup(image.name, &texture_index),
        "No spritesheet texture for image ", image.name);
  SetTextureDedup(textures_[texture_index]);
  const float w = image.width, h = image.height;
  const FVec2 p0(position - FVec(w / 2.0, h / 2.0));
  const FVec2 p1(position + FVec(w / 2.0, h / 2.0));
  const FVec2 q0(0, 0);
  const FVec2 q1(1.0, 1.0);
  renderer_->PushQuad(p0, p1, q0, q1, position, angle);
  return {};
}

void Renderer::DrawRect(FVec2 top_left, FVec2 bottom_right, float angle) {
  ClearTextureDedup();
  const FVec2 center = (top_left + bottom_right) / 2;
  renderer_->PushQuad(top_left, bottom_right, FVec(0, 0), FVec(1, 1),
                      /*origin=*/center, angle);
}

void Renderer::DrawLine(FVec2 p0, FVec2 p1) {
  ClearTextureDedup();
  renderer_->BeginLine();
  FVec2 ps[2] = {p0, p1};
  renderer_->PushLinePoints(Slice<FVec2>(ps, 2));
  renderer_->FinishLine();
}

void Renderer::DrawLines(Slice<FVec2> ps) {
  ClearTextureDedup();
  renderer_->BeginLine();
  renderer_->PushLinePoints(ps);
  renderer_->FinishLine();
}

void Renderer::DrawTriangle(FVec2 p1, FVec2 p2, FVec2 p3) {
  ClearTextureDedup();
  renderer_->PushTriangle(p1, p2, p3, FVec(0, 0), FVec(1, 0), FVec(1, 1));
}

// Pre-computed unit circle vertices. Indexed by segment, stores (cos, sin).
template <size_t N>
struct UnitCircle {
  struct Point {
    float x, y;
  };
  Point v[N + 1];  // v[N] == v[0] for wraparound.
  UnitCircle() {
    constexpr double kAngle = (2.0 * M_PI) / N;
    for (size_t i = 0; i < N; ++i) {
      v[i].x = static_cast<float>(std::cos(kAngle * i));
      v[i].y = static_cast<float>(std::sin(kAngle * i));
    }
    v[N] = v[0];
  }
};

static const UnitCircle<22> kCircle22;
static const UnitCircle<32> kCircle32;

void Renderer::DrawCircle(FVec2 center, float radius) {
  ClearTextureDedup();
  for (size_t i = 0; i < 22; ++i) {
    const FVec2 p0(center.x + radius * kCircle22.v[i].x,
                   center.y + radius * kCircle22.v[i].y);
    const FVec2 p1(center.x + radius * kCircle22.v[i + 1].x,
                   center.y + radius * kCircle22.v[i + 1].y);
    renderer_->PushTriangle(center, p0, p1, FVec(0, 0), FVec(1, 0), FVec(1, 1));
  }
}

void Renderer::DrawRectOutline(FVec2 top_left, FVec2 bottom_right,
                               float angle) {
  ClearTextureDedup();
  FVec2 corners[4] = {
      top_left,
      FVec(bottom_right.x, top_left.y),
      bottom_right,
      FVec(top_left.x, bottom_right.y),
  };
  if (angle != 0.0f) {
    const FVec2 center = (top_left + bottom_right) / 2;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    for (auto& corner : corners) {
      const float dx = corner.x - center.x;
      const float dy = corner.y - center.y;
      corner.x = center.x + dx * c - dy * s;
      corner.y = center.y + dx * s + dy * c;
    }
  }
  renderer_->BeginLine();
  FVec2 loop[5] = {corners[0], corners[1], corners[2], corners[3], corners[0]};
  renderer_->PushLinePoints(Slice<FVec2>(loop, 5));
  renderer_->FinishLine();
}

void Renderer::DrawCircleOutline(FVec2 center, float radius) {
  ClearTextureDedup();
  constexpr size_t kSegments = 32;
  FVec2 points[kSegments + 1];
  for (size_t i = 0; i <= kSegments; ++i) {
    points[i] = FVec(center.x + radius * kCircle32.v[i].x,
                     center.y + radius * kCircle32.v[i].y);
  }
  renderer_->BeginLine();
  renderer_->PushLinePoints(Slice<FVec2>(points, kSegments + 1));
  renderer_->FinishLine();
}

void Renderer::DrawTriangleOutline(FVec2 p1, FVec2 p2, FVec2 p3) {
  ClearTextureDedup();
  FVec2 loop[4] = {p1, p2, p3, p1};
  renderer_->BeginLine();
  renderer_->PushLinePoints(Slice<FVec2>(loop, 4));
  renderer_->FinishLine();
}

void Renderer::DrawEllipse(FVec2 center, float rx, float ry) {
  ClearTextureDedup();
  for (size_t i = 0; i < 32; ++i) {
    const FVec2 p0(center.x + rx * kCircle32.v[i].x,
                   center.y + ry * kCircle32.v[i].y);
    const FVec2 p1(center.x + rx * kCircle32.v[i + 1].x,
                   center.y + ry * kCircle32.v[i + 1].y);
    renderer_->PushTriangle(center, p0, p1, FVec(0, 0), FVec(1, 0), FVec(1, 1));
  }
}

void Renderer::DrawEllipseOutline(FVec2 center, float rx, float ry) {
  ClearTextureDedup();
  constexpr size_t kSegments = 32;
  FVec2 points[kSegments + 1];
  for (size_t i = 0; i <= kSegments; ++i) {
    points[i] = FVec(center.x + rx * kCircle32.v[i].x,
                     center.y + ry * kCircle32.v[i].y);
  }
  renderer_->BeginLine();
  renderer_->PushLinePoints(Slice<FVec2>(points, kSegments + 1));
  renderer_->FinishLine();
}

void Renderer::DrawRoundedRect(FVec2 top_left, FVec2 bottom_right,
                               float radius) {
  ClearTextureDedup();
  const float x1 = top_left.x;
  const float y1 = top_left.y;
  const float x2 = bottom_right.x;
  const float y2 = bottom_right.y;
  const float max_radius = std::min((x2 - x1) / 2.0f, (y2 - y1) / 2.0f);
  const float r = std::min(radius, max_radius);
  constexpr size_t kArcSegments = 8;
  constexpr double kHalfPi = M_PI / 2.0;
  // Center rectangle (between the rounded corners).
  renderer_->PushQuad(FVec(x1 + r, y1), FVec(x2 - r, y2), FVec(0, 0),
                      FVec(1, 1), (top_left + bottom_right) / 2, /*angle=*/0);
  // Left rectangle.
  renderer_->PushQuad(FVec(x1, y1 + r), FVec(x1 + r, y2 - r), FVec(0, 0),
                      FVec(1, 1), (top_left + bottom_right) / 2, /*angle=*/0);
  // Right rectangle.
  renderer_->PushQuad(FVec(x2 - r, y1 + r), FVec(x2, y2 - r), FVec(0, 0),
                      FVec(1, 1), (top_left + bottom_right) / 2, /*angle=*/0);
  // Four corner arcs as triangle fans.
  struct ArcCenter {
    FVec2 center;
    double start_angle;
  };
  const ArcCenter arcs[4] = {
      {FVec(x2 - r, y1 + r), -kHalfPi},  // top-right
      {FVec(x2 - r, y2 - r), 0},         // bottom-right
      {FVec(x1 + r, y2 - r), kHalfPi},   // bottom-left
      {FVec(x1 + r, y1 + r), M_PI},      // top-left
  };
  for (const auto& arc : arcs) {
    for (size_t i = 0; i < kArcSegments; ++i) {
      const double a0 = arc.start_angle + (kHalfPi * i) / kArcSegments;
      const double a1 = arc.start_angle + (kHalfPi * (i + 1)) / kArcSegments;
      const FVec2 p0 = FVec(arc.center.x + r * std::cos(a0),
                            arc.center.y + r * std::sin(a0));
      const FVec2 p1 = FVec(arc.center.x + r * std::cos(a1),
                            arc.center.y + r * std::sin(a1));
      renderer_->PushTriangle(arc.center, p0, p1, FVec(0, 0), FVec(1, 0),
                              FVec(1, 1));
    }
  }
}

void Renderer::DrawRoundedRectOutline(FVec2 top_left, FVec2 bottom_right,
                                      float radius) {
  ClearTextureDedup();
  const float x1 = top_left.x;
  const float y1 = top_left.y;
  const float x2 = bottom_right.x;
  const float y2 = bottom_right.y;
  const float max_radius = std::min((x2 - x1) / 2.0f, (y2 - y1) / 2.0f);
  const float r = std::min(radius, max_radius);
  constexpr size_t kArcSegments = 8;
  constexpr double kHalfPi = M_PI / 2.0;
  // Build the outline as a single connected line strip: four arcs connected
  // by four straight edges.
  constexpr size_t kTotalPoints = 4 * kArcSegments + 5;
  FVec2 points[kTotalPoints];
  size_t idx = 0;
  struct ArcCenter {
    FVec2 center;
    double start_angle;
  };
  const ArcCenter arcs[4] = {
      {FVec(x2 - r, y1 + r), -kHalfPi},  // top-right
      {FVec(x2 - r, y2 - r), 0},         // bottom-right
      {FVec(x1 + r, y2 - r), kHalfPi},   // bottom-left
      {FVec(x1 + r, y1 + r), M_PI},      // top-left
  };
  for (const auto& arc : arcs) {
    for (size_t i = 0; i <= kArcSegments; ++i) {
      const double a = arc.start_angle + (kHalfPi * i) / kArcSegments;
      points[idx++] =
          FVec(arc.center.x + r * std::cos(a), arc.center.y + r * std::sin(a));
    }
  }
  // Close the loop back to the first point.
  points[idx++] = points[0];
  renderer_->BeginLine();
  renderer_->PushLinePoints(Slice<FVec2>(points, idx));
  renderer_->FinishLine();
}

// Generate individual SDF bitmaps for each glyph and prepare packing rects.
// Populates `bitmaps` with per-glyph SDF data and `rects` with dimensions
// for atlas packing. Also stores advance widths into `font.glyphs`.
void Renderer::GenerateSDFBitmaps(FontInfo& font, GlyphBitmap* bitmaps,
                                  stbrp_rect* rects, std::string_view name) {
  TIMER("Generating SDF glyphs for ", name);
  for (int i = 0; i < kNumChars; ++i) {
    int cp = kFirstChar + i;
    bitmaps[i].data = stbtt_GetCodepointSDF(
        &font.font_info, font.scale, cp, kPadding, kOnEdge, kPixelDistScale,
        &bitmaps[i].w, &bitmaps[i].h, &bitmaps[i].xoff, &bitmaps[i].yoff);
    int advance, bearing;
    stbtt_GetCodepointHMetrics(&font.font_info, cp, &advance, &bearing);
    font.glyphs[cp].advance = advance * font.scale;
    rects[i].id = i;
    rects[i].w = bitmaps[i].data ? bitmaps[i].w + kAtlasGutter : 0;
    rects[i].h = bitmaps[i].data ? bitmaps[i].h + kAtlasGutter : 0;
  }
}

// Pack glyph rectangles into the smallest square atlas that fits.
// Starts at 256x256 and doubles until all glyphs fit (max 2048x2048).
// Returns the atlas dimension.
int Renderer::PackGlyphRects(stbrp_rect* rects, std::string_view name) {
  int atlas_dim = 256;
  bool packed = false;
  while (atlas_dim <= 2048 && !packed) {
    stbrp_context pack_ctx;
    stbrp_node nodes[512];
    stbrp_init_target(&pack_ctx, atlas_dim, atlas_dim, nodes, 512);
    packed = stbrp_pack_rects(&pack_ctx, rects, kNumChars) == 1;
    if (!packed) atlas_dim *= 2;
  }
  CHECK(packed, "Could not pack SDF atlas for ", name, " even at ", atlas_dim,
        "x", atlas_dim);
  return atlas_dim;
}

// Blit individual SDF bitmaps into a single atlas texture and store
// UV coordinates + positioning metrics into the glyph array.
// Returns a pointer to the atlas pixel data (allocated from `scratch`).
uint8_t* Renderer::BlitGlyphsIntoAtlas(FontInfo& font,
                                       const GlyphBitmap* bitmaps,
                                       const stbrp_rect* rects, int atlas_dim,
                                       ArenaAllocator* scratch) {
  const size_t atlas_bytes = static_cast<size_t>(atlas_dim) * atlas_dim;
  uint8_t* atlas = scratch->NewArray<uint8_t>(atlas_bytes);
  std::memset(atlas, 0, atlas_bytes);

  for (int i = 0; i < kNumChars; ++i) {
    if (!bitmaps[i].data) continue;
    int cp = kFirstChar + i;
    const int dx = rects[i].x;
    const int dy = rects[i].y;
    for (int row = 0; row < bitmaps[i].h; ++row) {
      std::memcpy(&atlas[(dy + row) * atlas_dim + dx],
                  &bitmaps[i].data[static_cast<size_t>(row) * bitmaps[i].w],
                  bitmaps[i].w);
    }
    font.glyphs[cp].s0 = (float)dx / atlas_dim;
    font.glyphs[cp].t0 = (float)dy / atlas_dim;
    font.glyphs[cp].s1 = (float)(dx + bitmaps[i].w) / atlas_dim;
    font.glyphs[cp].t1 = (float)(dy + bitmaps[i].h) / atlas_dim;
    font.glyphs[cp].x_offset = (float)bitmaps[i].xoff;
    font.glyphs[cp].y_offset = (float)bitmaps[i].yoff;
    font.glyphs[cp].width = (float)bitmaps[i].w;
    font.glyphs[cp].height = (float)bitmaps[i].h;
    stbtt_FreeSDF(bitmaps[i].data, 0, 0, nullptr);
  }
  return atlas;
}

ErrorOr<Renderer::FontInfo> Renderer::LoadSDFFromCache(
    sqlite3* db, std::string_view font_name, uint64_t font_hash) {
  SqlStmt stmt(db,
               "SELECT atlas_width, atlas_height, glyph_metrics, atlas_bitmap "
               "FROM sdf_cache WHERE font_name = ? AND font_hash = ?");
  if (!stmt.ok()) return Error::Message("SDF cache query failed");
  stmt.BindText(1, font_name);
  stmt.BindInt64(2, static_cast<int64_t>(font_hash));
  auto row = TRY(stmt.Step());
  if (!row) return Error::Message("SDF cache miss");

  FontInfo font;
  font.atlas_width = stmt.ColumnInt(0);
  font.atlas_height = stmt.ColumnInt(1);

  const auto* metrics =
      static_cast<const uint8_t*>(stmt.ColumnBlob(2));
  const int metrics_size = stmt.ColumnBytes(2);
  constexpr int kGlyphFields = 9;
  const size_t expected_size =
      static_cast<size_t>(kNumChars) * kGlyphFields * sizeof(float);
  if (metrics_size != expected_size) {
    LOG("SDF cache metrics size mismatch for ", font_name, ": got ",
        metrics_size, " expected ", expected_size);
    return Error::Message("SDF cache metrics corrupted");
  }
  // Deserialize field-by-field via memcpy to avoid alignment issues with
  // the SQLite blob pointer on strict-alignment architectures.
  for (int i = 0; i < kNumChars; i++) {
    const uint8_t* src =
        metrics + static_cast<size_t>(i) * kGlyphFields * sizeof(float);
    SDFGlyph& g = font.glyphs[kFirstChar + i];
    std::memcpy(&g.s0, src, 4);
    std::memcpy(&g.t0, src + 4, 4);
    std::memcpy(&g.s1, src + 8, 4);
    std::memcpy(&g.t1, src + 12, 4);
    std::memcpy(&g.x_offset, src + 16, 4);
    std::memcpy(&g.y_offset, src + 20, 4);
    std::memcpy(&g.width, src + 24, 4);
    std::memcpy(&g.height, src + 28, 4);
    std::memcpy(&g.advance, src + 32, 4);
  }

  const void* atlas_blob = stmt.ColumnBlob(3);
  const int atlas_size = stmt.ColumnBytes(3);
  const int expected_atlas = font.atlas_width * font.atlas_height;
  if (atlas_size != expected_atlas) {
    LOG("SDF cache atlas size mismatch for ", font_name);
    return Error::Message("SDF cache atlas corrupted");
  }
  font.texture = renderer_->LoadFontTexture(atlas_blob, font.atlas_width,
                                            font.atlas_height);
  return font;
}

void Renderer::SaveSDFToCache(sqlite3* db, std::string_view font_name,
                              uint64_t font_hash, const FontInfo& font,
                              const uint8_t* atlas_bitmap) {
  SqlStmt stmt(db,
               "INSERT OR REPLACE INTO sdf_cache "
               "(font_name, font_hash, atlas_width, atlas_height, "
               "glyph_metrics, atlas_bitmap) VALUES (?, ?, ?, ?, ?, ?)");
  if (!stmt.ok()) return;
  stmt.BindText(1, font_name);
  stmt.BindInt64(2, static_cast<int64_t>(font_hash));
  stmt.BindInt(3, font.atlas_width);
  stmt.BindInt(4, font.atlas_height);

  constexpr int kGlyphFields = 9;
  float metrics[kNumChars * kGlyphFields];
  for (int i = 0; i < kNumChars; i++) {
    const SDFGlyph& g = font.glyphs[kFirstChar + i];
    float* f = &metrics[static_cast<size_t>(i) * kGlyphFields];
    f[0] = g.s0;
    f[1] = g.t0;
    f[2] = g.s1;
    f[3] = g.t1;
    f[4] = g.x_offset;
    f[5] = g.y_offset;
    f[6] = g.width;
    f[7] = g.height;
    f[8] = g.advance;
  }
  stmt.BindBlobTransient(5, metrics, sizeof(metrics));
  stmt.BindBlob(6, atlas_bitmap, font.atlas_width * font.atlas_height);
  auto step = stmt.Step();
  if (step.is_error()) {
    LOG("SDF cache write failed for ", font_name);
  }
}

void Renderer::LoadFont(const DbAssets::Font& asset) {
  FontInfo font = {};
  CHECK(stbtt_InitFont(&font.font_info, asset.contents,
                       stbtt_GetFontOffsetForIndex(asset.contents, 0)),
        "Could not initialize ", asset.name);
  font.scale = stbtt_ScaleForPixelHeight(&font.font_info, kSDFHeight);
  font.pixel_height = kSDFHeight;
  stbtt_GetFontVMetrics(&font.font_info, &font.ascent, &font.descent,
                        &font.line_gap);

  const uint64_t font_hash =
      rapidhash(asset.contents, asset.size) ^ kSDFCacheVersion;

  auto cache_result = LoadSDFFromCache(db_, asset.name, font_hash);
  if (!cache_result.is_error()) {
    FontInfo cached = cache_result.release_value();
    font.atlas_width = cached.atlas_width;
    font.atlas_height = cached.atlas_height;
    std::memcpy(font.glyphs, cached.glyphs, sizeof(font.glyphs));
    font.texture = cached.texture;
    LOG("SDF cache hit for ", asset.name);
  } else {
    ArenaAllocator scratch(allocator_, 2048 * 2048 + kNumChars * 64);
    auto* bitmaps = scratch.NewArray<GlyphBitmap>(kNumChars);
    auto* rects = scratch.NewArray<stbrp_rect>(kNumChars);

    GenerateSDFBitmaps(font, bitmaps, rects, asset.name);
    const int atlas_dim = PackGlyphRects(rects, asset.name);
    uint8_t* atlas =
        BlitGlyphsIntoAtlas(font, bitmaps, rects, atlas_dim, &scratch);

    font.atlas_width = atlas_dim;
    font.atlas_height = atlas_dim;
    SaveSDFToCache(db_, asset.name, font_hash, font, atlas);
    font.texture =
        renderer_->LoadFontTexture(atlas, font.atlas_width, font.atlas_height);
  }
  LOG("SDF atlas for ", asset.name, ": ", font.atlas_width, "x",
      font.atlas_height);
  fonts_.Push(font);
  font_table_.Insert(asset.name, &fonts_.back());
}

Color ParseColor(std::string_view color) {
  // TODO: Support ANSI escape codes properly.
  for (char c : color) {
    if (c == '[') continue;
    if (c == ';') continue;
    if (c == '7') {
      return MUST(ColorFromTable("lightred"));
    }
    if (c == '3') {
      return MUST(ColorFromTable("blueblue"));
    }
    if (c == '0') return Color::White();
  }
  return Color::White();
}

void Renderer::DrawString(std::string_view font_name, uint32_t size,
                          std::string_view str, FVec2 position) {
  FontInfo* info = nullptr;
  if (!font_table_.Lookup(font_name, &info)) {
    LOG("Could not find ", font_name, " in fonts");
    return;
  }
  CHECK(info != nullptr, "No texture found for ", font_name, " size ", size);
  const uint32_t prev_shader = renderer_->GetCurrentShaderHandle();
  renderer_->SetShaderProgram("sdf");
  const FVec4 oc = outline_color_.ToFloat();
  renderer_->SetSDFOutline(oc.x, oc.y, oc.z, oc.w, outline_thickness_);
  SetTextureDedup(info->texture);
  const Color color = color_;
  FVec2 p = position;
  const float pixel_scale = size / info->pixel_height;
  // Shift baseline down by the ascent so the y parameter refers to the top of
  // the text line rather than the baseline.
  p.y += info->ascent * info->scale * pixel_scale;
  auto handle_char = [&](size_t i, char c) {
    const SDFGlyph& g = info->glyphs[static_cast<unsigned char>(c)];
    if (g.width == 0 || g.height == 0) {
      // Space or unprintable — advance only.
      p.x += g.advance * pixel_scale;
      if ((i + 1) < str.size()) {
        p.x +=
            pixel_scale * info->scale *
            stbtt_GetCodepointKernAdvance(&info->font_info, str[i], str[i + 1]);
      }
      return;
    }
    const float x0 = p.x + g.x_offset * pixel_scale;
    const float y0 = p.y + g.y_offset * pixel_scale;
    const float x1 = x0 + g.width * pixel_scale;
    const float y1 = y0 + g.height * pixel_scale;
    renderer_->PushQuad(FVec(x0, y0), FVec(x1, y1), FVec(g.s0, g.t0),
                        FVec(g.s1, g.t1), FVec(0, 0),
                        /*angle=*/0);
    p.x += g.advance * pixel_scale;
    if ((i + 1) < str.size()) {
      p.x +=
          pixel_scale * info->scale *
          stbtt_GetCodepointKernAdvance(&info->font_info, str[i], str[i + 1]);
    }
  };
  for (size_t i = 0; i < str.size();) {
    const char c = str[i];
    if (c == '\033') {
      size_t st = i + 1, en = i;
      while (en < str.size() && str[en] != 'm') en++;
      if (en >= str.size()) break;
      renderer_->SetActiveColor(ParseColor(str.substr(st, en - st)));
      i = en + 1;
      continue;
    }
    if (c == '\t') {
      for (int j = 0; j < 3; ++j) handle_char(str.size(), ' ');
      handle_char(i, ' ');
      i++;
      continue;
    }
    if (c == '\n') {
      p.x = position.x;
      p.y += pixel_scale * info->scale *
             (info->ascent - info->descent + info->line_gap);
      i++;
      continue;
    }
    handle_char(i, c);
    i++;
  }
  renderer_->SetActiveColor(color);
  if (prev_shader != 0) {
    renderer_->SetShaderByHandle(prev_shader);
  } else {
    renderer_->SetShaderProgram("pre_pass");
  }
}

IVec2 Renderer::TextDimensions(std::string_view font_name, uint32_t size,
                               std::string_view str) {
  FontInfo* info = nullptr;
  if (!font_table_.Lookup(font_name, &info)) {
    LOG("Could not find ", font_name, " in fonts");
    return IVec2();
  }
  const float pixel_scale = size / info->pixel_height;
  const float line_height = pixel_scale * info->scale *
                            (info->ascent - info->descent + info->line_gap);
  float px = 0, max_x = 0;
  float py = line_height;
  for (size_t i = 0; i < str.size(); ++i) {
    const char c = str[i];
    if (c == '\033') {
      while (i < str.size() && str[i] != 'm') i++;
      continue;
    }
    if (c == '\t') {
      px += info->glyphs[static_cast<unsigned char>(' ')].advance *
            pixel_scale * 4;
      continue;
    }
    if (c == '\n') {
      max_x = std::max(max_x, px);
      px = 0;
      py += line_height;
    } else {
      px += info->glyphs[static_cast<unsigned char>(c)].advance * pixel_scale;
      if ((i + 1) < str.size()) {
        px +=
            pixel_scale * info->scale *
            stbtt_GetCodepointKernAdvance(&info->font_info, str[i], str[i + 1]);
      }
    }
  }
  max_x = std::max(max_x, px);
  return IVec2(static_cast<int>(max_x), static_cast<int>(py));
}

float Renderer::MeasureSpan(const FontInfo& info, float pixel_scale,
                            std::string_view str) {
  float px = 0;
  for (size_t i = 0; i < str.size(); ++i) {
    const char c = str[i];
    if (c == '\033') {
      while (i < str.size() && str[i] != 'm') i++;
      continue;
    }
    px += info.glyphs[static_cast<unsigned char>(c)].advance * pixel_scale;
    if ((i + 1) < str.size()) {
      px += pixel_scale * info.scale *
            stbtt_GetCodepointKernAdvance(&info.font_info, str[i], str[i + 1]);
    }
  }
  return px;
}

void Renderer::WordWrapLines(const FontInfo& info, float pixel_scale,
                             std::string_view str, float max_width,
                             FixedArray<WrappedLine>* out) {
  size_t line_start = 0;
  while (line_start < str.size()) {
    // Find the end of this paragraph (next newline or end of string).
    size_t newline_pos = str.find('\n', line_start);
    if (newline_pos == std::string_view::npos) newline_pos = str.size();
    std::string_view paragraph =
        str.substr(line_start, newline_pos - line_start);

    if (paragraph.empty()) {
      out->Push(WrappedLine{paragraph, 0});
      line_start = newline_pos + 1;
      continue;
    }

    size_t word_start = 0;
    size_t current_line_start = 0;
    float current_width = 0;
    float space_width =
        info.glyphs[static_cast<unsigned char>(' ')].advance * pixel_scale;

    while (word_start < paragraph.size()) {
      // Skip leading spaces for measurement.
      size_t ws_end = word_start;
      while (ws_end < paragraph.size() && paragraph[ws_end] == ' ') ws_end++;

      // Find end of word.
      size_t word_end = ws_end;
      while (word_end < paragraph.size() && paragraph[word_end] != ' ')
        word_end++;

      std::string_view word = paragraph.substr(ws_end, word_end - ws_end);
      float word_width = MeasureSpan(info, pixel_scale, word);

      if (current_line_start == word_start) {
        // First word on this line: always place it even if it exceeds
        // max_width.
        current_width = word_width;
        word_start = word_end;
      } else {
        float test_width = current_width + space_width + word_width;
        if (test_width <= max_width) {
          current_width = test_width;
          word_start = word_end;
        } else {
          // Emit the current line (trim trailing spaces).
          std::string_view line_text = paragraph.substr(
              current_line_start, word_start - current_line_start);
          // Trim trailing spaces.
          while (!line_text.empty() && line_text.back() == ' ')
            line_text.remove_suffix(1);
          out->Push(WrappedLine{line_text,
                                MeasureSpan(info, pixel_scale, line_text)});
          current_line_start = ws_end;
          current_width = word_width;
          word_start = word_end;
        }
      }
    }

    // Emit the last line of this paragraph.
    std::string_view last_line = paragraph.substr(
        current_line_start, paragraph.size() - current_line_start);
    while (!last_line.empty() && last_line.back() == ' ')
      last_line.remove_suffix(1);
    out->Push(
        WrappedLine{last_line, MeasureSpan(info, pixel_scale, last_line)});

    line_start = newline_pos + 1;
  }
}

void Renderer::DrawStringWrapped(std::string_view font_name, uint32_t size,
                                 std::string_view str, FVec2 position,
                                 float max_width, TextAlign align) {
  FontInfo* info = nullptr;
  if (!font_table_.Lookup(font_name, &info)) {
    LOG("Could not find ", font_name, " in fonts");
    return;
  }
  const float pixel_scale = size / info->pixel_height;
  const float line_height = pixel_scale * info->scale *
                            (info->ascent - info->descent + info->line_gap);

  // Estimate max lines: text_length / ~4 chars per word is a generous upper
  // bound, plus one per newline, plus 1.
  size_t max_lines = str.size() / 2 + 2;
  ArenaAllocator scratch(allocator_, max_lines * sizeof(WrappedLine) + 256);
  FixedArray<WrappedLine> lines(max_lines, &scratch);
  WordWrapLines(*info, pixel_scale, str, max_width, &lines);

  FVec2 p = position;
  for (size_t i = 0; i < lines.size(); ++i) {
    float x_offset = 0;
    if (align == TextAlign::kCenter) {
      x_offset = std::max(0.0f, (max_width - lines[i].width) * 0.5f);
    } else if (align == TextAlign::kRight) {
      x_offset = std::max(0.0f, max_width - lines[i].width);
    }
    DrawString(font_name, size, lines[i].text, FVec(p.x + x_offset, p.y));
    p.y += line_height;
  }
}

int Renderer::TextWrappedHeight(std::string_view font_name, uint32_t size,
                                std::string_view str, float max_width) {
  FontInfo* info = nullptr;
  if (!font_table_.Lookup(font_name, &info)) {
    LOG("Could not find ", font_name, " in fonts");
    return 0;
  }
  const float pixel_scale = size / info->pixel_height;
  const float line_height = pixel_scale * info->scale *
                            (info->ascent - info->descent + info->line_gap);

  size_t max_lines = str.size() / 2 + 2;
  ArenaAllocator scratch(allocator_, max_lines * sizeof(WrappedLine) + 256);
  FixedArray<WrappedLine> lines(max_lines, &scratch);
  WordWrapLines(*info, pixel_scale, str, max_width, &lines);

  return static_cast<int>(lines.size() * line_height);
}

void Renderer::SetTextOutline(Color color, float thickness) {
  outline_color_ = color;
  outline_thickness_ = thickness;
}

void Renderer::ClearTextOutline() {
  outline_color_ = Color::Zero();
  outline_thickness_ = 0.0f;
}

void Renderer::DrawStringColored(std::string_view font_name, uint32_t size,
                                 const ColoredSegment* segments,
                                 size_t num_segments, FVec2 position,
                                 float max_width, TextAlign align) {
  FontInfo* info = nullptr;
  if (!font_table_.Lookup(font_name, &info)) {
    LOG("Could not find ", font_name, " in fonts");
    return;
  }
  const float pixel_scale = size / info->pixel_height;
  const float line_height = pixel_scale * info->scale *
                            (info->ascent - info->descent + info->line_gap);

  // Concatenate all segment text to compute total length.
  size_t total_len = 0;
  for (size_t i = 0; i < num_segments; ++i) {
    total_len += segments[i].text.size();
  }
  if (total_len == 0) return;

  // Build concatenated string in scratch memory.
  ArenaAllocator scratch(
      allocator_,
      total_len + 1 + (total_len / 2 + 2) * sizeof(WrappedLine) + 256);
  auto* concat_buf =
      reinterpret_cast<char*>(scratch.Alloc(total_len, /*align=*/1));
  size_t offset = 0;
  for (size_t i = 0; i < num_segments; ++i) {
    std::memcpy(concat_buf + offset, segments[i].text.data(),
                segments[i].text.size());
    offset += segments[i].text.size();
  }
  std::string_view concat(concat_buf, total_len);

  // Set up SDF shader and outline.
  const uint32_t prev_shader = renderer_->GetCurrentShaderHandle();
  renderer_->SetShaderProgram("sdf");
  const FVec4 oc = outline_color_.ToFloat();
  renderer_->SetSDFOutline(oc.x, oc.y, oc.z, oc.w, outline_thickness_);
  SetTextureDedup(info->texture);
  const Color saved_color = color_;

  if (max_width <= 0) {
    // No wrapping: render as a single line with color tracking.
    FVec2 p = position;
    p.y += info->ascent * info->scale * pixel_scale;
    size_t seg_idx = 0;
    size_t seg_offset = 0;
    renderer_->SetActiveColor(segments[0].color);
    for (size_t i = 0; i < total_len; ++i) {
      // Advance to the correct segment for this character.
      while (seg_idx < num_segments &&
             seg_offset >= segments[seg_idx].text.size()) {
        seg_offset = 0;
        seg_idx++;
        if (seg_idx < num_segments) {
          renderer_->SetActiveColor(segments[seg_idx].color);
        }
      }
      const char c = concat[i];
      if (c == '\n') {
        p.x = position.x;
        p.y += line_height;
        seg_offset++;
        continue;
      }
      const SDFGlyph& g = info->glyphs[static_cast<unsigned char>(c)];
      if (g.width > 0 && g.height > 0) {
        const float x0 = p.x + g.x_offset * pixel_scale;
        const float y0 = p.y + g.y_offset * pixel_scale;
        const float x1 = x0 + g.width * pixel_scale;
        const float y1 = y0 + g.height * pixel_scale;
        renderer_->PushQuad(FVec(x0, y0), FVec(x1, y1), FVec(g.s0, g.t0),
                            FVec(g.s1, g.t1), FVec(0, 0), /*angle=*/0);
      }
      p.x += g.advance * pixel_scale;
      if ((i + 1) < total_len) {
        p.x += pixel_scale * info->scale *
               stbtt_GetCodepointKernAdvance(&info->font_info, concat[i],
                                             concat[i + 1]);
      }
      seg_offset++;
    }
  } else {
    // Word-wrap the concatenated text, then render lines with color tracking.
    size_t max_lines = total_len / 2 + 2;
    FixedArray<WrappedLine> lines(max_lines, &scratch);
    WordWrapLines(*info, pixel_scale, concat, max_width, &lines);

    FVec2 p = position;
    // Global character index tracks position into the concatenated string,
    // which in turn maps to the correct color segment.
    size_t global_char = 0;
    size_t seg_idx = 0;
    size_t seg_offset = 0;

    for (size_t li = 0; li < lines.size(); ++li) {
      const WrappedLine& line = lines[li];
      float x_offset = 0;
      if (align == TextAlign::kCenter) {
        x_offset = std::max(0.0f, (max_width - line.width) * 0.5f);
      } else if (align == TextAlign::kRight) {
        x_offset = std::max(0.0f, max_width - line.width);
      }

      // The line text is a view into the concatenated buffer.
      // Compute the character offset of this line in the concatenated string.
      size_t line_start = static_cast<size_t>(line.text.data() - concat_buf);

      // Advance global_char past any whitespace/newlines between lines.
      // The wrapping may skip trailing spaces and newlines.
      while (global_char < line_start) {
        // Advance segment tracking past skipped characters.
        while (seg_idx < num_segments &&
               seg_offset >= segments[seg_idx].text.size()) {
          seg_offset = 0;
          seg_idx++;
        }
        global_char++;
        seg_offset++;
      }

      FVec2 lp = FVec(p.x + x_offset, p.y);
      lp.y += info->ascent * info->scale * pixel_scale;

      for (size_t ci = 0; ci < line.text.size(); ++ci) {
        // Advance to the correct segment.
        while (seg_idx < num_segments &&
               seg_offset >= segments[seg_idx].text.size()) {
          seg_offset = 0;
          seg_idx++;
        }
        if (seg_idx < num_segments) {
          renderer_->SetActiveColor(segments[seg_idx].color);
        }

        const char c = line.text[ci];
        const SDFGlyph& g = info->glyphs[static_cast<unsigned char>(c)];
        if (g.width > 0 && g.height > 0) {
          const float x0 = lp.x + g.x_offset * pixel_scale;
          const float y0 = lp.y + g.y_offset * pixel_scale;
          const float x1 = x0 + g.width * pixel_scale;
          const float y1 = y0 + g.height * pixel_scale;
          renderer_->PushQuad(FVec(x0, y0), FVec(x1, y1), FVec(g.s0, g.t0),
                              FVec(g.s1, g.t1), FVec(0, 0), /*angle=*/0);
        }
        lp.x += g.advance * pixel_scale;
        if ((ci + 1) < line.text.size()) {
          lp.x += pixel_scale * info->scale *
                  stbtt_GetCodepointKernAdvance(&info->font_info, line.text[ci],
                                                line.text[ci + 1]);
        }
        global_char++;
        seg_offset++;
      }
      p.y += line_height;
    }
  }

  renderer_->SetActiveColor(saved_color);
  if (prev_shader != 0) {
    renderer_->SetShaderByHandle(prev_shader);
  } else {
    renderer_->SetShaderProgram("pre_pass");
  }
}

}  // namespace G
