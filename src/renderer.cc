#include <cmath>
#include <vector>

#include "clock.h"
#include "console.h"
#include "filesystem.h"
#include "image.h"
#include "renderer.h"
#include "strings.h"
#include "transformations.h"

namespace G {
namespace {}  // namespace

constexpr size_t kCommandMemory = 1 << 24;

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

void BatchRenderer::AddCommand(CommandType command, const void* data,
                               size_t size) {
  if (command != kDone) {
    std::memcpy(&command_buffer_[pos_], data, size);
    pos_ += size;
  }
  if (commands_.empty() || commands_.back().type != command ||
      commands_.back().count == kMaxCount) {
    commands_.Push(QueueEntry{.type = command, .count = 1});
  } else {
    commands_.back().count++;
  }
}

BatchRenderer::BatchRenderer(IVec2 viewport, Shaders* shaders,
                             Allocator* allocator)
    : allocator_(allocator),
      command_buffer_(static_cast<uint8_t*>(
          allocator->Alloc(kCommandMemory, alignof(Command)))),
      commands_(1 << 20, allocator),
      tex_(256, allocator),
      screenshots_(64, allocator),
      shaders_(shaders),
      viewport_(viewport) {
  TIMER();
  glGetIntegerv(GL_MAX_SAMPLES, &antialiasing_samples_);
  LOG("Using ", antialiasing_samples_, " MSAA samples");
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
  SwitchShaderProgram("post_pass");
  const GLint pos_attribute = shaders_->AttributeLocation("input_position");
  OPENGL_CALL(glEnableVertexAttribArray(pos_attribute));
  OPENGL_CALL(glVertexAttribPointer(pos_attribute, 2, GL_FLOAT, GL_FALSE,
                                    4 * sizeof(float), (void*)0));
  const GLint tex_attribute = shaders_->AttributeLocation("input_tex_coord");
  OPENGL_CALL(glEnableVertexAttribArray(tex_attribute));
  OPENGL_CALL(glVertexAttribPointer(tex_attribute, 2, GL_FLOAT, GL_FALSE,
                                    4 * sizeof(float),
                                    (void*)(2 * sizeof(float))));
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
                                      viewport.x, viewport.y, GL_TRUE));
  OPENGL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_2D_MULTISAMPLE, render_texture_,
                                     /*level=*/0));
  CHECK(!glGetError(), "Could generate render texture: ", glGetError());
  // Create downsampled texture data.
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, downsampled_target_));
  OPENGL_CALL(glActiveTexture(GL_TEXTURE1));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, downsampled_texture_));
  OPENGL_CALL(glTexImage2D(
      GL_TEXTURE_2D, /*level=*/0, GL_RGBA, viewport.x, viewport.y,
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
                                    viewport.x, viewport.y));
  OPENGL_CALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                        GL_DEPTH_STENCIL_ATTACHMENT,
                                        GL_RENDERBUFFER, depth_buffer_));
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
  tex_.Push(render_texture_);
  tex_.Push(downsampled_texture_);
  glActiveTexture(GL_TEXTURE0);
  // Load an empty texture, just white pixels, to be able to draw colors without
  // if statements in the shader.
  uint8_t white_pixels[32 * 32 * 4];
  std::memset(white_pixels, 255, sizeof(white_pixels));
  noop_texture_ = LoadTexture(&white_pixels, /*width=*/32, /*height=*/32);
  SetActiveTexture(noop_texture_);
}

void BatchRenderer::SetViewport(IVec2 viewport) {
  if (viewport_ == viewport) return;
  // Rebind texture, downsampled and depth buffer to the size.
  OPENGL_CALL(glActiveTexture(GL_TEXTURE0));
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, render_target_));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, render_texture_));
  OPENGL_CALL(glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
                                      antialiasing_samples_, GL_RGBA,
                                      viewport.x, viewport.y, GL_TRUE));
  OPENGL_CALL(glActiveTexture(GL_TEXTURE1));
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, downsampled_target_));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, downsampled_texture_));
  OPENGL_CALL(glTexImage2D(
      GL_TEXTURE_2D, /*level=*/0, GL_RGBA, viewport.x, viewport.y,
      /*border=*/0, GL_RGBA, GL_UNSIGNED_BYTE, /*pixels=*/nullptr));
  OPENGL_CALL(glRenderbufferStorageMultisample(
      GL_RENDERBUFFER, antialiasing_samples_, GL_DEPTH24_STENCIL8, viewport.x,
      viewport.y));
  viewport_ = viewport;
}

size_t BatchRenderer::LoadTexture(const ImageAsset& image) {
  TIMER("Decoding ", FlatbufferStringview(image.name()));
  QoiDesc desc;
  constexpr int kChannels = 4;
  auto* image_bytes =
      QoiDecode(image.contents()->Data(), image.contents()->size(), &desc,
                kChannels, allocator_);
  size_t index = LoadTexture(image_bytes, image.width(), image.height());
  allocator_->Dealloc(image_bytes, image.width() * image.height() * kChannels);
  return index;
}

BatchRenderer::~BatchRenderer() {
  std::array<GLuint, 4> object_buffers = {vbo_, ebo_, screen_quad_vbo_};
  OPENGL_CALL(glDeleteBuffers(object_buffers.size(), object_buffers.data()));
  std::array<GLuint, 2> frame_buffers = {render_target_, downsampled_target_};
  OPENGL_CALL(glDeleteFramebuffers(frame_buffers.size(), frame_buffers.data()));
  OPENGL_CALL(glDeleteVertexArrays(1, &vao_));
  OPENGL_CALL(glDeleteVertexArrays(1, &screen_quad_vao_));
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
  SwitchShaderProgram("pre_pass");
  const GLint pos_attribute = shaders_->AttributeLocation("input_position");
  OPENGL_CALL(glVertexAttribPointer(
      pos_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
      sizeof(VertexData),
      reinterpret_cast<void*>(offsetof(VertexData, position))));
  OPENGL_CALL(glEnableVertexAttribArray(pos_attribute));
  const GLint tex_coord_attribute =
      shaders_->AttributeLocation("input_tex_coord");
  OPENGL_CALL(glVertexAttribPointer(
      tex_coord_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
      sizeof(VertexData),
      reinterpret_cast<void*>(offsetof(VertexData, tex_coords))));
  OPENGL_CALL(glEnableVertexAttribArray(tex_coord_attribute));
  const GLint origin_attribute = shaders_->AttributeLocation("origin");
  OPENGL_CALL(glVertexAttribPointer(
      origin_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
      sizeof(VertexData),
      reinterpret_cast<void*>(offsetof(VertexData, origin))));
  OPENGL_CALL(glEnableVertexAttribArray(origin_attribute));
  const GLint angle_attribute = shaders_->AttributeLocation("angle");
  OPENGL_CALL(glVertexAttribPointer(
      angle_attribute, 1, GL_FLOAT, GL_FALSE, sizeof(VertexData),
      reinterpret_cast<void*>(offsetof(VertexData, angle))));
  OPENGL_CALL(glEnableVertexAttribArray(angle_attribute));
  const GLint color_attribute = shaders_->AttributeLocation("color");
  OPENGL_CALL(glVertexAttribPointer(
      color_attribute, sizeof(Color), GL_UNSIGNED_BYTE, GL_FALSE,
      sizeof(VertexData),
      reinterpret_cast<void*>(offsetof(VertexData, color))));
  OPENGL_CALL(glEnableVertexAttribArray(color_attribute));
  shaders_->SetUniform("global_color", FVec4(1, 1, 1, 1));
  // Render batches by finding changes to the OpenGL context.
  int render_calls = 0;
  size_t indices_start = 0;
  size_t indices_end = 0;
  GLuint texture_unit = 0;
  FMat4x4 transform = FMat4x4::Identity();
  GLint primitives = GL_TRIANGLES;
  float line_width = 1.0;
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
        SwitchShaderProgram(c.set_shader.shader_handle);
        break;
      case kSetLineWidth:
        flush();
        line_width = c.set_line_width.width;
        break;
      case kSetColor:
        break;
      case kDone:
        flush();
        break;
    }
  }
  if (debug_render_) {
    indices_end = indices_start = 0;
    // Draw a red semi transparent quad for all quads.
    SwitchShaderProgram("pre_pass");
    shaders_->SetUniform("tex", noop_texture_);
    shaders_->SetUniform("global_color", FVec4(1, 0, 0, 0.7));
    FMat4x4 transform = FMat4x4::Identity();
    GLint primitives = GL_TRIANGLES;
    float line_width = 1.0;
    auto flush = [&] {
      if (indices_start == indices_end) return;
      glLineWidth(line_width);
      shaders_->SetUniform("projection", Ortho(0, viewport_.x, 0, viewport_.y));
      shaders_->SetUniform("transform", transform);
      OPENGL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_));
      OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, tex_[noop_texture_]));
      const auto indices_start_ptr =
          reinterpret_cast<uintptr_t>(&indices[indices_start]) -
          reinterpret_cast<uintptr_t>(&indices[0]);
      OPENGL_CALL(glDrawElementsInstanced(
          GL_TRIANGLES, indices_end - indices_start, GL_UNSIGNED_INT,
          reinterpret_cast<void*>(indices_start_ptr), 1));
    };
    for (CommandIterator it(command_buffer_, &commands_); !it.Done();) {
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
        case kSetColor:
          flush();
          break;
        case kSetShader:
          // Not gonna use the shader here since its debug drawing.
          flush();
          break;
        case kSetLineWidth:
          flush();
          line_width = c.set_line_width.width;
          break;
        case kDone:
          flush();
          break;
      }
    }
  }
  // Downsample framebuffer.
  glActiveTexture(GL_TEXTURE0);
  OPENGL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, render_target_));
  OPENGL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, downsampled_target_));
  OPENGL_CALL(glBlitFramebuffer(0, 0, viewport_.x, viewport_.y, 0, 0,
                                viewport_.x, viewport_.y, GL_COLOR_BUFFER_BIT,
                                GL_NEAREST));
  // Second pass.
  OPENGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
  TakeScreenshots();
  OPENGL_CALL(glClearColor(0.f, 0.f, 0.f, 0.f));
  OPENGL_CALL(glClear(GL_COLOR_BUFFER_BIT));
  SwitchShaderProgram("post_pass");
  glActiveTexture(GL_TEXTURE1);
  shaders_->SetUniform("screen_texture", 1);
  OPENGL_CALL(glBindVertexArray(screen_quad_vao_));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, downsampled_texture_));
  OPENGL_CALL(glDrawArrays(GL_TRIANGLES, 0, 6));
  render_calls++;
  WATCH_EXPR("Vertexes ", vertices.size());
  WATCH_EXPR("Indices ", indices.size());
  WATCH_EXPR("Vertex Memory", vertices.bytes());
  WATCH_EXPR("Indices Memory", indices.size());
  WATCH_EXPR("Render calls", render_calls);
}

void BatchRenderer::RequestScreenshot(
    uint8_t* pixels, size_t width, size_t height,
    void (*callback)(uint8_t*, size_t, size_t, void*), void* userdata) {
  ScreenshotRequest req;
  req.out_buffer = pixels;
  req.width = width;
  req.height = height;
  req.callback = callback;
  req.userdata = userdata;
  screenshots_.Push(req);
}

void BatchRenderer::TakeScreenshots() {
  if (screenshots_.empty()) return;
  // TODO: use an arena.
  struct RGBA {
    uint8_t r, g, b, a;
  };
  const size_t width = viewport_.x;
  const size_t height = viewport_.y;
  // TODO: The renderer already asks for memory, we should use the provided
  // one and flip the rows in place.
  const size_t elements = width * height;
  auto* buffer = NewArray<RGBA>(elements, allocator_);
  auto* flipped = NewArray<RGBA>(elements, allocator_);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
  for (size_t i = 0; i < width * height; ++i) buffer[i].a = 255;
  {
    // OpenGL reads memory in reverse row order. Flip the rows.
    auto* ptr = flipped;
    for (size_t r = 0; r < height; ++r) {
      std::memcpy(ptr, &buffer[(height - 1 - r) * width], width * sizeof(RGBA));
      ptr += width;
    }
  }
  for (const ScreenshotRequest& req : screenshots_) {
    auto* ptr = req.out_buffer;
    for (size_t r = 0; r < req.height; ++r) {
      std::memcpy(ptr, &flipped[r * width], req.width * sizeof(RGBA));
      ptr += req.width * sizeof(RGBA);
    }
    req.callback(req.out_buffer, req.width, req.height, req.userdata);
  }
  DeallocArray(flipped, elements, allocator_);
  DeallocArray(buffer, elements, allocator_);
  screenshots_.Clear();
}

Renderer::Renderer(const Assets& assets, BatchRenderer* renderer,
                   Allocator* allocator)
    : allocator_(allocator),
      assets_(&assets),
      renderer_(renderer),
      transform_stack_(128, allocator),
      textures_table_(allocator),
      textures_(256, allocator),
      sprites_table_(allocator),
      sprites_(1 << 20, allocator),
      font_table_(allocator),
      fonts_(512, allocator) {}

void Renderer::BeginFrame() {
  transform_stack_.Clear();
  transform_stack_.Push(FMat4x4::Identity());
  ApplyTransform(FMat4x4::Identity());
  SetColor(Color::White());
  SetLineWidth(1.0f);
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

void Renderer::Draw(std::string_view spritename, FVec2 position, float angle) {
  const SpriteAsset* sprite = nullptr;
  if (uint32_t handle; sprites_table_.Lookup(spritename, &handle)) {
    sprite = sprites_[handle];
  } else {
    sprite = LoadSprite(spritename);
  }
  Draw(*sprite, position, angle);
}

void Renderer::Draw(const SpriteAsset& sprite, FVec2 position, float angle) {
  auto sprite_name = FlatbufferStringview(sprite.name());
  uint32_t spritesheet_index = sprite.spritesheet();
  const SpritesheetAsset* spritesheet =
      assets_->GetSpritesheetByIndex(spritesheet_index);
  CHECK(spritesheet != nullptr, "No spritesheet for ", sprite_name);
  renderer_->SetActiveTexture(textures_[spritesheet_index]);
  const float x = sprite.x(), y = sprite.y(), w = sprite.width(),
              h = sprite.height();
  const FVec2 p0(position - FVec(w / 2.0, h / 2.0));
  const FVec2 p1(position + FVec(w / 2.0, h / 2.0));
  const FVec2 q0(
      FVec(1.0 * x / spritesheet->width(), 1.0 * y / spritesheet->height()));
  const FVec2 q1(1.0f * (x + w) / spritesheet->width(),
                 1.0f * (y + h) / spritesheet->height());
  renderer_->PushQuad(p0, p1, q0, q1, position, angle);
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
  renderer_->PushLinePoint(p0);
  renderer_->PushLinePoint(p1);
  renderer_->FinishLine();
}

void Renderer::DrawLines(const FVec2* ps, size_t n) {
  renderer_->ClearTexture();
  renderer_->BeginLine();
  for (size_t i = 0; i < n; ++i) {
    renderer_->PushLinePoint(ps[i]);
  }
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

const Renderer::FontInfo* Renderer::LoadFont(std::string_view font_name,
                                             uint32_t font_size) {
  FixedStringBuffer<kMaxPathLength + 2> font_key(font_name, "_", font_size);
  if (uint32_t handle = 0; font_table_.Lookup(font_key.str(), &handle)) {
    return &fonts_[handle];
  }
  const FontAsset* asset = assets_->GetFont(font_name);
  if (asset == nullptr) return nullptr;
  ArenaAllocator scratch(allocator_, kAtlasSize * 5);
  const float pixel_height = font_size;
  uint8_t* atlas = NewArray<uint8_t>(kAtlasSize, &scratch);
  FontInfo font;
  const uint8_t* font_buffer = asset->contents()->data();
  CHECK(stbtt_InitFont(&font.font_info, font_buffer,
                       stbtt_GetFontOffsetForIndex(font_buffer, 0)),
        "Could not initialize ", FlatbufferStringview(asset->name()));
  font.scale = stbtt_ScaleForPixelHeight(&font.font_info, pixel_height);
  stbtt_GetFontVMetrics(&font.font_info, &font.ascent, &font.descent,
                        &font.line_gap);
  stbtt_PackBegin(&font.context, atlas, kAtlasWidth, kAtlasHeight, kAtlasWidth,
                  1, /*alloc_context=*/allocator_);
  stbtt_PackSetOversampling(&font.context, 2, 2);
  CHECK(stbtt_PackFontRange(&font.context, font_buffer, 0, pixel_height, 0, 256,
                            font.chars.data()) == 1,
        "Could not load font");
  stbtt_PackEnd(&font.context);
  uint8_t* buffer = NewArray<uint8_t>(4 * kAtlasSize, &scratch);
  for (size_t i = 0, j = 0; j < kAtlasSize; j++, i += 4) {
    std::memset(&buffer[i], atlas[j], 4);
  }
  font.texture = renderer_->LoadTexture(buffer, kAtlasWidth, kAtlasHeight);
  font_table_.Insert(font_key.str(), fonts_.size());
  fonts_.Push(font);
  return &fonts_.back();
}

const SpriteAsset* Renderer::LoadSprite(std::string_view name) {
  const SpriteAsset* sprite = assets_->GetSprite(name);
  const SpritesheetAsset* sheet =
      assets_->GetSpritesheetByIndex(sprite->spritesheet());
  std::string_view spritesheet_name = FlatbufferStringview(sheet->name());
  if (!textures_table_.Contains(spritesheet_name)) {
    std::string_view image_name = FlatbufferStringview(sheet->image_name());
    auto* image = assets_->GetImage(image_name);
    CHECK(image != nullptr, "Unknown image ", image_name, " for spritesheet ",
          spritesheet_name);
    textures_table_.Insert(image_name, textures_.size());
    textures_.Push(renderer_->LoadTexture(*image));
  }
  sprites_table_.Insert(name, sprites_.size());
  sprites_.Push(sprite);
  return sprite;
}

Color ParseColor(std::string_view color) {
  // TODO: Support ANSI escape codes properly.
  for (char c : color) {
    if (c == '[') continue;
    if (c == ';') continue;
    if (c == '7') return ColorFromTable("lightred");
    if (c == '0') return Color::White();
  }
  return Color::White();
}

void Renderer::DrawText(std::string_view font_name, uint32_t size,
                        std::string_view str, FVec2 position) {
  const FontInfo* info = LoadFont(font_name, size);
  renderer_->SetActiveTexture(info->texture);
  FVec2 p = position;
  const Color color = SetColor(Color::White());
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
    if (c == '\n') {
      p.x = position.x;
      p.y += info->scale * (info->ascent - info->descent + info->line_gap);
      i++;
      continue;
    }
    stbtt_aligned_quad q;
    stbtt_GetPackedQuad(info->chars.data(), kAtlasWidth, kAtlasHeight, c, &p.x,
                        &p.y, &q,
                        /*align_to_integer=*/false);
    renderer_->PushQuad(FVec(q.x0, q.y0), FVec(q.x1, q.y1), FVec(q.s0, q.t0),
                        FVec(q.s1, q.t1), FVec(0, 0),
                        /*angle=*/0);
    if ((i + 1) < str.size()) {
      p.x += info->scale * stbtt_GetCodepointKernAdvance(&info->font_info,
                                                         str[i], str[i + 1]);
    }
    i++;
  }
  renderer_->SetActiveColor(color);
}

IVec2 Renderer::TextDimensions(std::string_view font_name, uint32_t size,
                               std::string_view str) {
  const FontInfo* info = LoadFont(font_name, size);
  auto p = FVec2::Zero();
  p.y = info->scale * (info->ascent - info->descent + info->line_gap);
  for (size_t i = 0; i < str.size(); ++i) {
    const char c = str[i];
    if (c == '\n') {
      p.y += info->scale * (info->ascent - info->descent + info->line_gap);
    } else {
      int width, bearing;
      stbtt_GetCodepointHMetrics(&info->font_info, c, &width, &bearing);
      p.x += info->scale * width;
      if ((i + 1) < str.size()) {
        p.x += info->scale * stbtt_GetCodepointKernAdvance(&info->font_info,
                                                           str[i], str[i + 1]);
      }
    }
  }
  return IVec2(static_cast<int>(p.x), static_cast<int>(p.y));
}

}  // namespace G