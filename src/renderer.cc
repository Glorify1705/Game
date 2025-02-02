#include "renderer.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "clock.h"
#include "console.h"
#include "filesystem.h"
#include "image.h"
#include "stringlib.h"
#include "transformations.h"
#include "units.h"

namespace G {

constexpr size_t kCommandMemory = 1 << 24;

constexpr size_t BatchRenderer::SizeOfCommand(CommandType t) {
  switch (t) {
    case kRenderQuad:
      return sizeof(RenderQuad);
    case kRenderTrig:
      return sizeof(RenderTriangle);
    case kStartLine:
      return sizeof(StartLine);
    case kAddLinePoint:
      return sizeof(AddLinePoint);
    case kEndLine:
      return sizeof(EndLine);
    case kSetTexture:
      return sizeof(SetTexture);
    case kSetColor:
      return sizeof(SetColor);
    case kSetTransform:
      return sizeof(SetTransform);
    case kSetShader:
      return sizeof(SetShader);
    case kSetLineWidth:
      return sizeof(SetLineWidth);
    case kDone:
      return 0;
  }
  return 0;
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

  CommandType Read(Command* p) {
    if (i_ == commands_->size()) return kDone;
    if (remaining_ == 0) {
      i_++;
      if (i_ == commands_->size()) return kDone;
      remaining_ = (*commands_)[i_].count;
    }
    const QueueEntry& e = (*commands_)[i_];
    remaining_--;
    const auto type = static_cast<CommandType>(e.type);
    size_t size = SizeOfCommand(type);
    std::memcpy(p, &buffer_[pos_], size);
    pos_ += size;
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
    std::memcpy(&command_buffer_[pos_], data, size);
    pos_ += size;
  }
  if (commands_.empty() || commands_.back().type != command ||
      commands_.back().count == kMaxCount) {
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
      viewport_(viewport) {
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
  CHECK(!glGetError(), "Could generate render texture: ", glGetError());
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
  OPENGL_CALL(glGenRenderbuffers(1, &depth_buffer_));
  OPENGL_CALL(glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer_));
  OPENGL_CALL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                                    viewport_.x, viewport_.y));
  OPENGL_CALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                        GL_DEPTH_STENCIL_ATTACHMENT,
                                        GL_RENDERBUFFER, depth_buffer_));
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
  glActiveTexture(GL_TEXTURE0);
}

void BatchRenderer::SetViewport(IVec2 viewport) {
  if (viewport_ == viewport) return;
  LOG("Resizing viewport from ", viewport_, " to ", viewport);
  viewport_ = viewport;
  // Delete the framebuffers and recreate them.
  std::array<GLuint, 2> frame_buffers = {render_target_, downsampled_target_};
  OPENGL_CALL(glDeleteFramebuffers(frame_buffers.size(), frame_buffers.data()));
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
  std::array<GLuint, 4> object_buffers = {vbo_, ebo_, screen_quad_vbo_};
  OPENGL_CALL(glDeleteBuffers(object_buffers.size(), object_buffers.data()));
  std::array<GLuint, 2> frame_buffers = {render_target_, downsampled_target_};
  OPENGL_CALL(glDeleteFramebuffers(frame_buffers.size(), frame_buffers.data()));
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
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                              GL_LINEAR_MIPMAP_LINEAR));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED));
  OPENGL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RED,
                           GL_UNSIGNED_BYTE, data));
  OPENGL_CALL(glGenerateMipmap(GL_TEXTURE_2D));
  CHECK(!glGetError(), "Could generate texture: ", glGetError());
  tex_.Push(tex);
  return index;
}

void BatchRenderer::Render(Allocator* scratch) {
  // Setup OpenGL state.
  OPENGL_CALL(glEnable(GL_MULTISAMPLE));
  OPENGL_CALL(glViewport(0, 0, viewport_.x, viewport_.y));
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, render_target_));
  OPENGL_CALL(glClearColor(0.f, 0.f, 0.f, 0.f));
  OPENGL_CALL(glEnable(GL_BLEND));
  OPENGL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
  OPENGL_CALL(glBlendEquation(GL_FUNC_ADD));
  OPENGL_CALL(glDisable(GL_DEPTH_TEST));
  OPENGL_CALL(glClear(GL_COLOR_BUFFER_BIT));
  OPENGL_CALL(glEnable(GL_LINE_SMOOTH));
  // Compute size of data.
  size_t vertices_count = 0, indices_count = 0;
  for (CommandIterator it(command_buffer_, &commands_); !it.Done();) {
    switch (Command c; it.Read(&c)) {
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
    switch (Command c; it.Read(&c)) {
      case kRenderQuad: {
        const RenderQuad& q = c.quad;
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
        const RenderTriangle& t = c.triangle;
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
        const AddLinePoint& l = c.add_line_point;
        vertices.Push({.position = l.p0,
                       .tex_coords = FVec(0, 0),
                       .origin = FVec(0, 0),
                       .angle = 0,
                       .color = color});
        indices.Push(current);
      }; break;
      case kSetColor:
        color = c.set_color.color;
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
    shaders_->SetUniform("global_color", color.ToFloat());
  };
  set_program_state("pre_pass");
  // Render batches by finding changes to the OpenGL context.
  int render_calls = 0;
  size_t indices_start = 0;
  size_t indices_end = 0;
  GLuint texture_unit = 0;
  FMat4x4 transform = FMat4x4::Identity();
  GLint primitives = GL_TRIANGLES;
  float line_width = 2.5;
  for (CommandIterator it(command_buffer_, &commands_); !it.Done();) {
    auto flush = [&] {
      if (indices_start == indices_end) return;
      glLineWidth(line_width);
      glActiveTexture(GL_TEXTURE0 + texture_unit);
      shaders_->SetUniform("tex", texture_unit);
      shaders_->SetUniform("projection", Ortho(0, viewport_.x, 0, viewport_.y));
      shaders_->SetUniform("transform", transform);
      OPENGL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_));
      OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, tex_[texture_unit]));
      const auto indices_start_ptr =
          reinterpret_cast<uintptr_t>(&indices[indices_start]) -
          reinterpret_cast<uintptr_t>(&indices[0]);
      OPENGL_CALL(glDrawElementsInstanced(
          primitives, indices_end - indices_start, GL_UNSIGNED_INT,
          reinterpret_cast<void*>(indices_start_ptr), 1));
      render_calls++;
      indices_start = indices_end;
    };
    switch (Command c; it.Read(&c)) {
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
        break;
      case kSetTransform:
        flush();
        transform = c.set_transform.transform;
        break;
      case kSetTexture:
        flush();
        texture_unit = c.set_texture.texture_unit;
        break;
      case kSetShader:
        flush();
        set_program_state(StringByHandle(c.set_shader.shader_handle));
        break;
      case kSetLineWidth:
        flush();
        line_width = c.set_line_width.width;
        break;
      case kSetColor:
        color = c.set_color.color;
        break;
      case kDone:
        flush();
        break;
    }
  }
  // Downsample framebuffer.
  OPENGL_CALL(glActiveTexture(GL_TEXTURE0));
  OPENGL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, render_target_));
  OPENGL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, downsampled_target_));
  OPENGL_CALL(glBlitFramebuffer(0, 0, viewport_.x, viewport_.y, 0, 0,
                                viewport_.x, viewport_.y, GL_COLOR_BUFFER_BIT,
                                GL_NEAREST));
  // Second pass.
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
  OPENGL_CALL(glClearColor(0.f, 0.f, 0.f, 0.f));
  OPENGL_CALL(glClear(GL_COLOR_BUFFER_BIT));
  shaders_->UseProgram("post_pass");
  glActiveTexture(GL_TEXTURE1);
  shaders_->SetUniform("screen_texture", 1);
  shaders_->SetUniform("color", color.ToFloat());
  OPENGL_CALL(glBindVertexArray(screen_quad_vao_));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, downsampled_texture_));
  OPENGL_CALL(glViewport(0, 0, viewport_.x, viewport_.y));
  OPENGL_CALL(glDrawArrays(GL_TRIANGLES, 0, 6));
  render_calls++;
}

BatchRenderer::Screenshot BatchRenderer::TakeScreenshot(
    Allocator* allocator) const {
  Screenshot result;
  const IVec2 viewport = GetViewport();
  size_t bytes = viewport.x;
  bytes *= viewport.y * sizeof(Color);
  auto* buffer = allocator->Alloc(bytes, /*align=*/4);
  glReadnPixels(0, 0, viewport.x, viewport.y, GL_RGBA, GL_UNSIGNED_BYTE, bytes,
                buffer);
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

Renderer::Renderer(const DbAssets& assets, BatchRenderer* renderer,
                   Allocator* allocator)
    : allocator_(allocator),
      renderer_(renderer),
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

void Renderer::LoadSprite(const DbAssets::Sprite& sprite) {
  // TODO: error handling.
  CHECK(textures_table_.Contains(sprite.spritesheet), "Unknown sprite sheet ",
        sprite.spritesheet);
  loaded_sprites_.Push(sprite);
  loaded_sprites_table_.Insert(sprite.name, &loaded_sprites_.back());
}

void Renderer::LoadImage(const DbAssets::Image& image) {
  if (!textures_table_.Contains(image.name)) {
    LOG("Loading texture for image ", image.name);
    textures_table_.Insert(image.name, textures_.size());
    textures_.Push(renderer_->LoadTexture(image));
  }
  loaded_images_.Push(image);
  loaded_images_table_.Insert(image.name, &loaded_images_.back());
}

void Renderer::LoadSpritesheet(const DbAssets::Spritesheet& spritesheet) {
  DbAssets::Image* image;
  // TODO: error handling.
  CHECK(loaded_images_table_.Lookup(spritesheet.image, &image), "No image ",
        spritesheet.image, " for spritesheet ", spritesheet.name);
  LOG("Loading texture ", spritesheet.name);
  CHECK(image != nullptr, "Unknown image ", spritesheet.image,
        " for spritesheet ", spritesheet.name);
  textures_table_.Insert(spritesheet.name, textures_.size());
  textures_.Push(renderer_->LoadTexture(*image));
  loaded_spritesheets_.Push(spritesheet);
  loaded_spritesheets_table_.Insert(spritesheet.name,
                                    &loaded_spritesheets_.back());
}

bool Renderer::DrawSprite(std::string_view sprite_name, FVec2 position,
                          float angle) {
  DbAssets::Sprite* sprite = nullptr;
  if (!loaded_sprites_table_.Lookup(sprite_name, &sprite)) {
    return false;
  }
  return DrawSprite(*sprite, position, angle);
}

bool Renderer::DrawSprite(const DbAssets::Sprite& sprite, FVec2 position,
                          float angle) {
  DbAssets::Spritesheet* spritesheet;
  if (!loaded_spritesheets_table_.Lookup(sprite.spritesheet, &spritesheet)) {
    return false;
  }
  uint32_t texture_index;
  CHECK(textures_table_.Lookup(spritesheet->name, &texture_index),
        "No spritesheet texture for ", sprite.name, "(spritesheet ",
        spritesheet->name, ")");
  renderer_->SetActiveTexture(textures_[texture_index]);
  const float x = sprite.x, y = sprite.y, w = sprite.width, h = sprite.height;
  const FVec2 p0(position - FVec(w / 2.0, h / 2.0));
  const FVec2 p1(position + FVec(w / 2.0, h / 2.0));
  const FVec2 q0(
      FVec(1.0 * x / spritesheet->width, 1.0 * y / spritesheet->height));
  const FVec2 q1(1.0f * (x + w) / spritesheet->width,
                 1.0f * (y + h) / spritesheet->height);
  renderer_->PushQuad(p0, p1, q0, q1, position, angle);
  return true;
}

bool Renderer::DrawImage(std::string_view image_name, FVec2 position,
                         float angle) {
  DbAssets::Image* image = nullptr;
  if (!loaded_images_table_.Lookup(image_name, &image)) {
    return false;
  }
  return DrawImage(*image, position, angle);
}

bool Renderer::DrawImage(const DbAssets::Image& image, FVec2 position,
                         float angle) {
  uint32_t texture_index;
  CHECK(textures_table_.Lookup(image.name, &texture_index),
        "No spritesheet texture for image ", image.name);
  renderer_->SetActiveTexture(textures_[texture_index]);
  const float w = image.width, h = image.height;
  const FVec2 p0(position - FVec(w / 2.0, h / 2.0));
  const FVec2 p1(position + FVec(w / 2.0, h / 2.0));
  const FVec2 q0(0, 0);
  const FVec2 q1(1.0, 1.0);
  renderer_->PushQuad(p0, p1, q0, q1, position, angle);
  return true;
}

void Renderer::DrawRect(FVec2 top_left, FVec2 bottom_right, float angle) {
  renderer_->ClearTexture();
  const FVec2 center = (top_left + bottom_right) / 2;
  renderer_->PushQuad(top_left, bottom_right, FVec(0, 0), FVec(1, 1),
                      /*origin=*/center, angle);
}

void Renderer::DrawLine(FVec2 p0, FVec2 p1) {
  renderer_->ClearTexture();
  renderer_->BeginLine();
  FVec2 ps[2] = {p0, p1};
  renderer_->PushLinePoints(ps, 2);
  renderer_->FinishLine();
}

void Renderer::DrawLines(const FVec2* ps, size_t n) {
  renderer_->ClearTexture();
  renderer_->BeginLine();
  renderer_->PushLinePoints(ps, n);
  renderer_->FinishLine();
}

void Renderer::DrawTriangle(FVec2 p1, FVec2 p2, FVec2 p3) {
  renderer_->ClearTexture();
  renderer_->PushTriangle(p1, p2, p3, FVec(0, 0), FVec(1, 0), FVec(1, 1));
}

void Renderer::DrawCircle(FVec2 center, float radius) {
  renderer_->ClearTexture();
  constexpr size_t kTriangles = 22;
  auto for_index = [&](int index) {
    const int i = index % kTriangles;
    constexpr double kAngle = (2.0 * M_PI) / kTriangles;
    return FVec(center.x + radius * std::cos(kAngle * i),
                center.y + radius * std::sin(kAngle * i));
  };
  for (size_t i = 0; i < kTriangles; ++i) {
    renderer_->PushTriangle(center, for_index(i), for_index(i + 1), FVec(0, 0),
                            FVec(1, 0), FVec(1, 1));
  }
}

void Renderer::LoadFont(const DbAssets::Font& asset) {
  ArenaAllocator scratch(allocator_, kAtlasSize * 5);
  const float pixel_height = 100;
  uint8_t* atlas = scratch.NewArray<uint8_t>(kAtlasSize);
  FontInfo font;
  CHECK(stbtt_InitFont(&font.font_info, asset.contents,
                       stbtt_GetFontOffsetForIndex(asset.contents, 0)),
        "Could not initialize ", asset.name);
  font.scale = stbtt_ScaleForPixelHeight(&font.font_info, pixel_height);
  font.pixel_height = pixel_height;
  {
    TIMER("Building font atlas for ", asset.name);
    stbtt_GetFontVMetrics(&font.font_info, &font.ascent, &font.descent,
                          &font.line_gap);
    stbtt_PackBegin(&font.context, atlas, kAtlasWidth, kAtlasHeight,
                    kAtlasWidth, 1, /*alloc_context=*/&scratch);
    stbtt_PackSetOversampling(&font.context, 2, 2);
    CHECK(stbtt_PackFontRange(&font.context, asset.contents, 0, pixel_height,
                              32, 126 - 32 + 1, font.chars) == 1,
          "Could not load font ", asset.name, ", atlas is too small");
    stbtt_PackEnd(&font.context);
  }
  font.texture = renderer_->LoadFontTexture(atlas, kAtlasWidth, kAtlasHeight);
  fonts_.Push(font);
  font_table_.Insert(asset.name, &fonts_.back());
}

Color ParseColor(std::string_view color) {
  // TODO: Support ANSI escape codes properly.
  for (char c : color) {
    Color color;
    if (c == '[') continue;
    if (c == ';') continue;
    if (c == '7') {
      ColorFromTable("lightred", &color);
      return color;
    }
    if (c == '3') {
      ColorFromTable("blueblue", &color);
      return color;
    }
    if (c == '0') return Color::White();
  }
  return Color::White();
}

void Renderer::DrawText(std::string_view font_name, uint32_t size,
                        std::string_view str, FVec2 position) {
  FontInfo* info = nullptr;
  if (!font_table_.Lookup(font_name, &info)) {
    LOG("Could not find ", font_name, " in fonts");
    return;
  }
  CHECK(info != nullptr, "No texture found for ", font_name, " size ", size);
  renderer_->SetActiveTexture(info->texture);
  const Color color = color_;
  FVec2 p = position;
  float pixel_scale = size / info->pixel_height;
  auto handle_char = [&](size_t i, char c) {
    stbtt_aligned_quad q;
    float x = 0, y = 0;
    stbtt_GetPackedQuad(info->chars, kAtlasWidth, kAtlasHeight, c - 32, &x, &y,
                        &q,
                        /*align_to_integer=*/false);
    const float x0 = p.x + q.x0 * pixel_scale;
    const float x1 = p.x + q.x1 * pixel_scale;
    const float y0 = p.y + q.y0 * pixel_scale;
    const float y1 = p.y + q.y1 * pixel_scale;
    renderer_->PushQuad(FVec(x0, y0), FVec(x1, y1), FVec(q.s0, q.t0),
                        FVec(q.s1, q.t1), FVec(0, 0),
                        /*angle=*/0);
    p.x += x * pixel_scale;
    if ((i + 1) < str.size()) {
      p.x += info->scale * stbtt_GetCodepointKernAdvance(&info->font_info,
                                                         str[i], str[i + 1]);
    }
  };
  for (size_t i = 0; i < str.size();) {
    const char c = str[i];
    if (c == '\033') {
      // Skip ANSI escape sequence.
      size_t st = i + 1, en = i;
      while (str[en] != 'm') en++;
      renderer_->SetActiveColor(ParseColor(str.substr(st, en - st)));
      i = en + 1;
      continue;
    }
    if (c == '\t') {
      // Add 4 spaces.
      for (int j = 0; j < 4; ++j) handle_char(str.size(), ' ');
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
}

IVec2 Renderer::TextDimensions(std::string_view font_name, uint32_t size,
                               std::string_view str) {
  FontInfo* info = nullptr;
  if (!font_table_.Lookup(font_name, &info)) {
    LOG("Could not find ", font_name, " in fonts");
    return IVec2();
  }
  auto p = FVec2::Zero();
  const float scale = stbtt_ScaleForPixelHeight(&info->font_info, size);
  p.y = scale * (info->ascent - info->descent + info->line_gap);
  float x = 0;
  for (size_t i = 0; i < str.size(); ++i) {
    const char c = str[i];
    if (c == '\n') {
      x = std::max(x, p.x);
      p.x = 0;
      p.y += scale * (info->ascent - info->descent + info->line_gap);
    } else {
      int width, bearing;
      stbtt_GetCodepointHMetrics(&info->font_info, c, &width, &bearing);
      p.x += scale * width;
      if ((i + 1) < str.size()) {
        p.x += scale * stbtt_GetCodepointKernAdvance(&info->font_info, str[i],
                                                     str[i + 1]);
      }
    }
  }
  x = std::max(x, p.x);
  return IVec2(static_cast<int>(x), static_cast<int>(p.y));
}

}  // namespace G
