#include <cmath>
#include <vector>

#include "clock.h"
#include "console.h"
#include "image.h"
#include "renderer.h"
#include "strings.h"
#include "transformations.h"

namespace G {
namespace {}  // namespace

constexpr size_t kCommandMemory = 1 << 24;

BatchRenderer::BatchRenderer(IVec2 viewport, Shaders* shaders,
                             Allocator* allocator)
    : allocator_(allocator),
      command_buffer_(static_cast<uint8_t*>(
          allocator->Alloc(kCommandMemory, alignof(Command)))),
      commands_(1 << 20, allocator),
      tex_(256, allocator),
      screenshots_(64, allocator),
      current_color_(Color::White()),
      shaders_(shaders),
      viewport_(viewport) {
  TIMER();
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);
  // Generate the quad for the post pass step.
  glGenVertexArrays(1, &screen_quad_vao_);
  glGenBuffers(1, &screen_quad_vbo_);
  glBindVertexArray(screen_quad_vao_);
  std::array<float, 24> screen_quad_vertices = {
      // Vertex position and Tex coord in Normalized Device Coordinates.
      -1.0f, 1.0f,  0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f,
      1.0f,  -1.0f, 1.0f, 0.0f, -1.0f, 1.0f,  0.0f, 1.0f,
      1.0f,  -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  1.0f, 1.0f};
  glBindBuffer(GL_ARRAY_BUFFER, screen_quad_vbo_);
  glBufferData(GL_ARRAY_BUFFER, screen_quad_vertices.size() * sizeof(float),
               screen_quad_vertices.data(), GL_STATIC_DRAW);
  program_handle_ = StringIntern("post_pass");
  shaders_->UseProgram(StringByHandle(program_handle_));
  const GLint pos_attribute = shaders_->AttributeLocation("input_position");
  glEnableVertexAttribArray(pos_attribute);
  glVertexAttribPointer(pos_attribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void*)0);
  const GLint tex_attribute = shaders_->AttributeLocation("input_tex_coord");
  glEnableVertexAttribArray(tex_attribute);
  glVertexAttribPointer(tex_attribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void*)(2 * sizeof(float)));
  // Create a render target for the viewport.
  glGenFramebuffers(1, &render_target_);
  glBindFramebuffer(GL_FRAMEBUFFER, render_target_);
  glGenTextures(1, &render_texture_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, render_texture_);
  glTexImage2D(GL_TEXTURE_2D, /*level=*/0, GL_RGBA, viewport.x, viewport.y,
               /*border=*/0, GL_RGBA, GL_UNSIGNED_BYTE, /*pixels=*/nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         render_texture_, /*level=*/0);
  CHECK(!glGetError(), "Could generate texture: ", glGetError());
  glGenRenderbuffers(1, &depth_buffer_);
  glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viewport.x,
                        viewport.y);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, depth_buffer_);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  tex_.Push(render_texture_);
  // Load an empty texture, just white pixels, to be able to draw colors without
  // if statements in the shader.
  uint8_t white_pixels[32 * 32 * 4];
  std::memset(white_pixels, 255, sizeof(white_pixels));
  noop_texture_ = LoadTexture(&white_pixels, /*width=*/32, /*height=*/32);
}

void BatchRenderer::SetViewport(IVec2 viewport) {
  if (viewport_ != viewport) {
    // Rebind texture and depth buffer to the size.
    glBindTexture(GL_TEXTURE_2D, render_texture_);
    glTexImage2D(GL_TEXTURE_2D, /*level=*/0, GL_RGBA, viewport.x, viewport.y,
                 /*border=*/0, GL_RGBA, GL_UNSIGNED_BYTE, /*pixels=*/nullptr);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viewport.x,
                          viewport.y);
    viewport_ = viewport;
  }
}

size_t BatchRenderer::LoadTexture(const ImageAsset& image) {
  TIMER("Decoding ", FlatbufferStringview(image.filename()));
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
  std::array<GLuint, 4> buffers = {vbo_, ebo_, screen_quad_vbo_};
  glDeleteBuffers(buffers.size(), buffers.data());
  glDeleteFramebuffers(1, &render_target_);
  glDeleteRenderbuffers(1, &depth_buffer_);
  glDeleteVertexArrays(1, &vao_);
  glDeleteVertexArrays(1, &screen_quad_vbo_);
  glDeleteTextures(tex_.size(), tex_.data());
  allocator_->Dealloc(command_buffer_, kCommandMemory);
}

size_t BatchRenderer::LoadTexture(const void* data, size_t width,
                                  size_t height) {
  GLuint tex;
  const size_t index = tex_.size();
  glGenTextures(1, &tex);
  glActiveTexture(GL_TEXTURE0 + index);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  CHECK(!glGetError(), "Could generate texture: ", glGetError());
  tex_.Push(tex);
  return index;
}

void BatchRenderer::Render(Allocator* scratch) {
  // Setup OpenGL state.
  glViewport(0, 0, viewport_.x, viewport_.y);
  glBindFramebuffer(GL_FRAMEBUFFER, render_target_);
  glClearColor(0.f, 0.f, 0.f, 0.f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBlendEquation(GL_FUNC_ADD);
  glDisable(GL_DEPTH_TEST);
  glClear(GL_COLOR_BUFFER_BIT);
  // Compute size of data.
  size_t vertices_count = 0, indices_count = 0;
  for (CommandIterator it(command_buffer_, &commands_); !it.Done();) {
    Command c;
    CommandType type = it.Read(&c);
    if (type == kDone) break;
    switch (type) {
      case kRenderQuad:
        vertices_count += 4;
        indices_count += 6;
        break;
      case kRenderTrig:
        vertices_count += 3;
        indices_count += 3;
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
    Command c;
    CommandType type = it.Read(&c);
    size_t current = vertices.size();
    switch (type) {
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
                       .origin = t.origin,
                       .angle = t.angle,
                       .color = color});
        vertices.Push({.position = FVec(t.p1.x, t.p1.y),
                       .tex_coords = t.q1,
                       .origin = t.origin,
                       .angle = t.angle,
                       .color = color});
        vertices.Push({.position = FVec(t.p2.x, t.p2.y),
                       .tex_coords = t.q2,
                       .origin = t.origin,
                       .angle = t.angle,
                       .color = color});
        for (int i : {0, 1, 2}) {
          indices.Push(current + i);
        }
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
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, vertices.bytes(), vertices.data(),
               GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.bytes(), indices.data(),
               GL_STATIC_DRAW);
  shaders_->UseProgram("pre_pass");
  const GLint pos_attribute = shaders_->AttributeLocation("input_position");
  glVertexAttribPointer(
      pos_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
      sizeof(VertexData),
      reinterpret_cast<void*>(offsetof(VertexData, position)));
  glEnableVertexAttribArray(pos_attribute);
  const GLint tex_coord_attribute =
      shaders_->AttributeLocation("input_tex_coord");
  glVertexAttribPointer(
      tex_coord_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
      sizeof(VertexData),
      reinterpret_cast<void*>(offsetof(VertexData, tex_coords)));
  glEnableVertexAttribArray(tex_coord_attribute);
  const GLint origin_attribute = shaders_->AttributeLocation("origin");
  glVertexAttribPointer(origin_attribute, FVec2::kCardinality, GL_FLOAT,
                        GL_FALSE, sizeof(VertexData),
                        reinterpret_cast<void*>(offsetof(VertexData, origin)));
  glEnableVertexAttribArray(origin_attribute);
  const GLint angle_attribute = shaders_->AttributeLocation("angle");
  glVertexAttribPointer(angle_attribute, 1, GL_FLOAT, GL_FALSE,
                        sizeof(VertexData),
                        reinterpret_cast<void*>(offsetof(VertexData, angle)));
  glEnableVertexAttribArray(angle_attribute);
  const GLint color_attribute = shaders_->AttributeLocation("color");
  glVertexAttribPointer(color_attribute, sizeof(Color), GL_UNSIGNED_BYTE,
                        GL_FALSE, sizeof(VertexData),
                        reinterpret_cast<void*>(offsetof(VertexData, color)));
  glEnableVertexAttribArray(color_attribute);
  shaders_->SetUniform("global_color", FVec4(1, 1, 1, 1));
  // Render batches by finding changes to the OpenGL context.
  int render_calls = 0;
  size_t indices_start = 0;
  size_t indices_end = 0;
  GLuint texture_unit = 0;
  FMat4x4 transform = FMat4x4::Identity();
  for (CommandIterator it(command_buffer_, &commands_); !it.Done();) {
    Command c;
    CommandType type = it.Read(&c);
    auto flush = [&] {
      if (indices_start == indices_end) return;
      shaders_->SetUniform("tex", texture_unit);
      shaders_->SetUniform("projection", Ortho(0, viewport_.x, 0, viewport_.y));
      shaders_->SetUniform("transform", transform);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
      glBindTexture(GL_TEXTURE_2D, tex_[texture_unit]);
      const auto indices_start_ptr =
          reinterpret_cast<uintptr_t>(&indices[indices_start]) -
          reinterpret_cast<uintptr_t>(&indices[0]);
      glDrawElementsInstanced(GL_TRIANGLES, indices_end - indices_start,
                              GL_UNSIGNED_INT,
                              reinterpret_cast<void*>(indices_start_ptr), 1);
      render_calls++;
      indices_start = indices_end;
    };
    switch (type) {
      case kRenderQuad:
        indices_end += 6;
        break;
      case kRenderTrig:
        indices_end += 3;
        break;
      case kSetTransform:
        flush();
        transform = c.set_transform.transform;
        break;
      case kSetTexture:
        flush();
        texture_unit = c.set_texture.texture_unit;
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
    shaders_->SetUniform("tex", noop_texture_);
    shaders_->SetUniform("global_color", FVec4(1, 0, 0, 0.7));
    FMat4x4 transform = FMat4x4::Identity();
    auto flush = [&] {
      if (indices_start == indices_end) return;
      shaders_->SetUniform("projection", Ortho(0, viewport_.x, 0, viewport_.y));
      shaders_->SetUniform("transform", transform);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
      glBindTexture(GL_TEXTURE_2D, tex_[noop_texture_]);
      const auto indices_start_ptr =
          reinterpret_cast<uintptr_t>(&indices[indices_start]) -
          reinterpret_cast<uintptr_t>(&indices[0]);
      glDrawElementsInstanced(GL_TRIANGLES, indices_end - indices_start,
                              GL_UNSIGNED_INT,
                              reinterpret_cast<void*>(indices_start_ptr), 1);
    };
    for (CommandIterator it(command_buffer_, &commands_); !it.Done();) {
      Command c;
      CommandType type = it.Read(&c);
      switch (type) {
        case kRenderQuad:
          indices_end += 6;
          break;
        case kRenderTrig:
          indices_end += 3;
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
        case kDone:
          flush();
          break;
      }
    }
  }
  // Second pass
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClearColor(0.f, 0.f, 0.f, 0.f);
  glClear(GL_COLOR_BUFFER_BIT);
  shaders_->UseProgram(StringByHandle(program_handle_));
  shaders_->SetUniform("screen_texture", 0);
  glBindVertexArray(screen_quad_vao_);
  glBindTexture(GL_TEXTURE_2D, render_texture_);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  render_calls++;
  WATCH_EXPR("Vertexes ", vertices.size());
  WATCH_EXPR("Indices ", indices.size());
  WATCH_EXPR("Vertex Memory", vertices.bytes());
  WATCH_EXPR("Indices Memory", indices.size());
  WATCH_EXPR("Render calls", render_calls);
  TakeScreenshots();
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
      textures_(256, allocator),
      sprites_table_(allocator),
      sprites_(1 << 20, allocator),
      font_table_(allocator),
      fonts_(64, allocator) {
  TIMER();
  LoadSpreadsheets(assets);
  LoadFonts(assets, kFontSize);
}

void Renderer::BeginFrame() {
  transform_stack_.Clear();
  transform_stack_.Push(FMat4x4::Identity());
  ApplyTransform(FMat4x4::Identity());
}

const SpriteAsset* Renderer::sprite(std::string_view name) const {
  uint32_t handle;
  if (!sprites_table_.Lookup(name, &handle)) return nullptr;
  return sprites_[handle];
}

void Renderer::Push() { transform_stack_.Push(transform_stack_.back()); }

void Renderer::Pop() {
  transform_stack_.Pop();
  renderer_->SetActiveTransform(transform_stack_.back());
}

Color Renderer::SetColor(Color color) {
  return renderer_->SetActiveColor(color);
}

void Renderer::Draw(FVec2 position, float angle, const SpriteAsset& sprite) {
  uint32_t spritesheet_index = sprite.spritesheet();
  const SpritesheetAsset* spritesheet =
      assets_->GetSpritesheetByIndex(spritesheet_index);
  CHECK(spritesheet != nullptr, "No spritesheet for ",
        FlatbufferStringview(sprite.name()));
  renderer_->SetActiveTexture(textures_[spritesheet_index]);
  const FVec2 p0(position - FVec(sprite.width() / 2.0, sprite.height() / 2.0));
  const FVec2 p1(position + FVec(sprite.width() / 2.0, sprite.height() / 2.0));
  const FVec2 q0(FVec(1.0 * sprite.x() / spritesheet->width(),
                      1.0 * sprite.y() / spritesheet->height()));
  const FVec2 q1(1.0f * (sprite.x() + sprite.width()) / spritesheet->width(),
                 1.0f * (sprite.y() + sprite.height()) / spritesheet->height());
  renderer_->PushQuad(p0, p1, q0, q1, position, angle);
}

void Renderer::DrawRect(FVec2 top_left, FVec2 bottom_right, float angle) {
  renderer_->ClearTexture();
  const FVec2 center = (top_left + bottom_right) / 2;
  renderer_->PushQuad(top_left, bottom_right, FVec(0, 0), FVec(1, 1),
                      /*origin=*/center, angle);
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
                            FVec(1, 0), FVec(1, 1), center,
                            /*angle=*/0);
  }
}

void Renderer::LoadSpreadsheets(const Assets& assets) {
  for (size_t i = 0; i < assets.spritesheets(); ++i) {
    auto* sheet = assets.GetSpritesheetByIndex(i);
    std::string_view image_name = FlatbufferStringview(sheet->image_name());
    auto* image = assets.GetImage(image_name);
    CHECK(image != nullptr, "Unknown image ", image_name, " for spritesheet ",
          FlatbufferStringview(sheet->filename()));
    textures_.Push(renderer_->LoadTexture(*image));
    for (const SpriteAsset* sprite : *sheet->sprites()) {
      sprites_table_.Insert(FlatbufferStringview(sprite->name()),
                            sprites_.size());
      sprites_.Push(sprite);
    }
  }
}

void Renderer::LoadFonts(const Assets& assets, float pixel_height) {
  uint8_t* atlas = NewArray<uint8_t>(kAtlasSize, allocator_);
  for (size_t i = 0; i < assets.fonts(); ++i) {
    FontInfo font;
    const FontAsset& font_asset = *assets.GetFontByIndex(i);
    const uint8_t* font_buffer = font_asset.contents()->data();
    CHECK(stbtt_InitFont(&font.font_info, font_buffer,
                         stbtt_GetFontOffsetForIndex(font_buffer, 0)),
          "Could not initialize ", FlatbufferStringview(font_asset.filename()));
    font.scale = stbtt_ScaleForPixelHeight(&font.font_info, pixel_height);
    stbtt_GetFontVMetrics(&font.font_info, &font.ascent, &font.descent,
                          &font.line_gap);
    stbtt_PackBegin(&font.context, atlas, kAtlasWidth, kAtlasHeight,
                    kAtlasWidth, 1, /*alloc_context=*/allocator_);
    stbtt_PackSetOversampling(&font.context, 2, 2);
    CHECK(stbtt_PackFontRange(&font.context, font_buffer, 0, pixel_height, 0,
                              256, font.chars.data()) == 1,
          "Could not load font");
    stbtt_PackEnd(&font.context);
    const size_t bytes = 4 * kAtlasWidth * kAtlasHeight;
    uint8_t* buffer = NewArray<uint8_t>(4 * kAtlasSize, allocator_);
    for (size_t i = 0, j = 0; j < kAtlasSize; j++, i += 4) {
      std::memset(&buffer[i], atlas[j], 4);
    }
    font.texture = renderer_->LoadTexture(buffer, kAtlasWidth, kAtlasHeight);
    allocator_->Dealloc(buffer, bytes);
    font_table_.Insert(FlatbufferStringview(font_asset.filename()), i);
    fonts_.Push(font);
  }
  allocator_->Dealloc(atlas, kAtlasSize);
}

void Renderer::DrawText(std::string_view font, float size, std::string_view str,
                        FVec2 position) {
  const FontInfo* info = &fonts_[font_table_.LookupOrDie(font)];
  renderer_->SetActiveTexture(info->texture);
  FVec2 p = position;
  p.y += info->scale * (info->ascent - info->descent);
  for (char c : str) {
    if (c == '\n') {
      p.x = position.x;
      p.y += info->scale * (info->ascent - info->descent);
    } else if (c == '\t') {
      p.x += size * 2;
    } else {
      stbtt_aligned_quad q;
      stbtt_GetPackedQuad(info->chars.data(), kAtlasWidth, kAtlasHeight, c,
                          &p.x, &p.y, &q,
                          /*align_to_integer=*/true);
      renderer_->PushQuad(FVec(q.x0, q.y1), FVec(q.x1, q.y0), FVec(q.s0, q.t1),
                          FVec(q.s1, q.t0), FVec(0, 0),
                          /*angle=*/0);
    }
  }
}

}  // namespace G