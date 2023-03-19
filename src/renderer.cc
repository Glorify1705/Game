#include <cmath>
#include <vector>

#include "clock.h"
#include "console.h"
#include "image.h"
#include "renderer.h"
#include "transformations.h"

namespace G {
namespace {

constexpr std::string_view kVertexShader = R"(
    #version 460 core

    layout (location = 0) in vec2 input_position;
    layout (location = 1) in vec2 input_tex_coord;
    layout (location = 2) in vec2 origin;
    layout (location = 3) in float angle;
    layout (location = 4) in vec4 color;
        
    uniform mat4x4 projection;
    uniform mat4x4 transform;    
    uniform vec4 global_color;


    out vec2 tex_coord;
    out vec4 out_color;

    mat4 RotateZ(float angle) {
      mat4 result = mat4(1.0);
      result[0][0] = cos(angle);
      result[1][0] = -sin(angle);
      result[0][1] = sin(angle);
      result[1][1] = cos(angle);
      return result;
    }

    mat4 Translate(vec2 pos) {
      mat4 result = mat4(1.0);
      result[3][0] = pos.x;
      result[3][1] = pos.y;
      return result;
    }

    void main() {
        mat4 rotation = Translate(origin) * RotateZ(angle) * Translate(-origin);
        gl_Position = projection * transform * rotation * vec4(input_position, 0.0, 1.0);
        tex_coord = input_tex_coord;
        out_color = global_color * color;
    }
  )";
constexpr std::string_view kFragmentShader = R"(
    #version 460 core
    out vec4 frag_color;

    in vec2 tex_coord;
    in vec4 out_color;

    uniform sampler2D tex;

    void main() {
        frag_color = texture(tex, tex_coord) * out_color;
    }
  )";

template <typename T>
size_t ByteSize(const std::vector<T>& v) {
  return v.size() * sizeof(T);
}

}  // namespace

BatchRenderer::BatchRenderer(IVec2 viewport)
    : vertex_shader_(compiler_.CompileOrDie(ShaderType::kVertex, "quad.vert",
                                            kVertexShader.data(),
                                            kVertexShader.size())),
      fragment_shader_(compiler_.CompileOrDie(
          ShaderType::kFragment, "quad.frag", kFragmentShader.data(),
          kFragmentShader.size())),
      shader_program_(compiler_.LinkOrDie(vertex_shader_, fragment_shader_)),
      viewport_(viewport) {
  TIMER();
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);
  // Load an empty texture, just white pixels, to be able to draw colors without
  // if statements in the shader.
  uint8_t white_pixels[32 * 32 * 4];
  std::memset(white_pixels, 255, sizeof(white_pixels));
  LoadTexture(&white_pixels, /*width=*/32, /*height=*/32);
}

GLuint BatchRenderer::LoadTexture(const ImageAsset& image) {
  TIMER("Decoding ", FlatbufferStringview(image.filename()));
  qoi_desc desc;
  // TODO: Use an arena for this.
  auto* image_bytes =
      qoi_decode(image.contents()->Data(), image.contents()->size(), &desc,
                 /*channels=*/4);
  GLuint texture = LoadTexture(image_bytes, image.width(), image.height());
  free(image_bytes);
  return texture;
}

BatchRenderer::~BatchRenderer() {
  glDeleteBuffers(1, &vbo_);
  glDeleteBuffers(1, &ebo_);
}

GLuint BatchRenderer::LoadTexture(const void* data, size_t width,
                                  size_t height) {
  CHECK(unit_ < tex_.size(), "Out of texture units");
  glGenTextures(1, &tex_[unit_]);
  glActiveTexture(GL_TEXTURE0 + unit_);
  glBindTexture(GL_TEXTURE_2D, tex_[unit_]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  CHECK(!glGetError(), "Could generate texture: ", glGetError());
  return unit_++;
}

void BatchRenderer::PushQuad(FVec2 p0, FVec2 p1, FVec2 q0, FVec2 q1,
                             FVec2 origin, float angle) {
  auto& batch = batches_.back();
  size_t current = vertices_.size();
  vertices_.Push({.position = FVec2(p0.x, p1.y),
                  .tex_coords = FVec2(q0.x, q1.y),
                  .origin = origin,
                  .angle = angle,
                  .color = batch.rgba_color});
  vertices_.Push({.position = p1,
                  .tex_coords = q1,
                  .origin = origin,
                  .angle = angle,
                  .color = batch.rgba_color});
  vertices_.Push({.position = FVec2(p1.x, p0.y),
                  .tex_coords = FVec2(q1.x, q0.y),
                  .origin = origin,
                  .angle = angle,
                  .color = batch.rgba_color});
  vertices_.Push({.position = p0,
                  .tex_coords = q0,
                  .origin = origin,
                  .angle = angle,
                  .color = batch.rgba_color});
  for (int i : {0, 1, 3, 1, 2, 3}) {
    indices_.Push(current + i);
    batch.indices_count++;
  }
}

void BatchRenderer::PushTriangle(FVec2 p0, FVec2 p1, FVec2 p2, FVec2 q0,
                                 FVec2 q1, FVec2 q2, FVec2 origin,
                                 float angle) {
  auto& batch = batches_.back();
  size_t current = vertices_.size();
  vertices_.Push({.position = p0,
                  .tex_coords = q0,
                  .origin = origin,
                  .angle = angle,
                  .color = batch.rgba_color});
  vertices_.Push({.position = p1,
                  .tex_coords = q1,
                  .origin = origin,
                  .angle = angle,
                  .color = batch.rgba_color});
  vertices_.Push({.position = p2,
                  .tex_coords = q2,
                  .origin = origin,
                  .angle = angle,
                  .color = batch.rgba_color});
  for (int i : {0, 1, 2}) {
    indices_.Push(current + i);
    batch.indices_count++;
  }
}

void BatchRenderer::Render() {
  glViewport(0, 0, viewport_.x, viewport_.y);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(0.f, 0.f, 0.f, 0.f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, vertices_.bytes(), vertices_.data(),
               GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.bytes(), indices_.data(),
               GL_STATIC_DRAW);
  shader_program_.Use();
  const GLint pos_attribute =
      glGetAttribLocation(shader_program_.id(), "input_position");
  glVertexAttribPointer(
      pos_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
      sizeof(VertexData),
      reinterpret_cast<void*>(offsetof(VertexData, position)));
  glEnableVertexAttribArray(pos_attribute);
  const GLint tex_coord_attribute =
      glGetAttribLocation(shader_program_.id(), "input_tex_coord");
  glVertexAttribPointer(
      tex_coord_attribute, FVec2::kCardinality, GL_FLOAT, GL_FALSE,
      sizeof(VertexData),
      reinterpret_cast<void*>(offsetof(VertexData, tex_coords)));
  glEnableVertexAttribArray(tex_coord_attribute);
  const GLint origin_attribute =
      glGetAttribLocation(shader_program_.id(), "origin");
  glVertexAttribPointer(origin_attribute, FVec2::kCardinality, GL_FLOAT,
                        GL_FALSE, sizeof(VertexData),
                        reinterpret_cast<void*>(offsetof(VertexData, origin)));
  glEnableVertexAttribArray(origin_attribute);
  const GLint angle_attribute =
      glGetAttribLocation(shader_program_.id(), "angle");
  glVertexAttribPointer(angle_attribute, 1, GL_FLOAT, GL_FALSE,
                        sizeof(VertexData),
                        reinterpret_cast<void*>(offsetof(VertexData, angle)));
  glEnableVertexAttribArray(angle_attribute);
  const GLint color_attribute =
      glGetAttribLocation(shader_program_.id(), "color");
  glVertexAttribPointer(color_attribute, FVec4::kCardinality, GL_FLOAT,
                        GL_FALSE, sizeof(VertexData),
                        reinterpret_cast<void*>(offsetof(VertexData, color)));
  glEnableVertexAttribArray(color_attribute);
  shader_program_.SetUniform("global_color", FVec4(1, 1, 1, 1));
  for (const auto& batch : batches_) {
    if (batch.indices_count == 0) continue;
    shader_program_.SetUniform("tex", batch.texture_unit);
    shader_program_.SetUniform("projection",
                               Ortho(0, viewport_.x, 0, viewport_.y));
    shader_program_.SetUniform("transform", batch.transform);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBindTexture(GL_TEXTURE_2D, tex_[batch.texture_unit]);
    const auto indices_start =
        reinterpret_cast<uintptr_t>(&indices_[batch.indices_start]) -
        reinterpret_cast<uintptr_t>(&indices_[0]);
    glDrawElementsInstanced(GL_TRIANGLES, batch.indices_count, GL_UNSIGNED_INT,
                            reinterpret_cast<void*>(indices_start), 1);
  }
  if (debug_render_) {
    // Draw a red semi transparent quad for all quads.
    shader_program_.SetUniform("tex", 0);
    shader_program_.SetUniform("global_color", FVec4(1, 0, 0, 0.2));
    for (const auto& batch : batches_) {
      shader_program_.SetUniform("projection",
                                 Ortho(0, viewport_.x, 0, viewport_.y));
      shader_program_.SetUniform("transform", batch.transform);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
      glBindTexture(GL_TEXTURE_2D, 0);
      const auto indices_start =
          reinterpret_cast<uintptr_t>(&indices_[batch.indices_start]) -
          reinterpret_cast<uintptr_t>(&indices_[0]);
      glDrawElementsInstanced(GL_TRIANGLES, batch.indices_count,
                              GL_UNSIGNED_INT,
                              reinterpret_cast<void*>(indices_start), 1);
    }
  }
  WATCH_EXPR("Batches", batches_.size());
  WATCH_EXPR("Vertex Memory", vertices_.bytes());
  WATCH_EXPR("Batch used memory", batches_.bytes());
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
  auto* buffer = new RGBA[width * height];
  auto* flipped = new RGBA[width * height];
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
  delete[] buffer;
  delete[] flipped;
  screenshots_.Clear();
}

Renderer::Renderer(const Assets& assets, BatchRenderer* renderer)
    : renderer_(renderer) {
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
  const SpriteAsset* result = nullptr;
  if (!sprites_.Lookup(name, &result)) return nullptr;
  return result;
}

void Renderer::Push() { transform_stack_.Push(transform_stack_.back()); }

void Renderer::Pop() {
  transform_stack_.Pop();
  renderer_->SetActiveTransform(transform_stack_.back());
}

void Renderer::SetColor(FVec4 color) { renderer_->SetActiveColor(color); }

void Renderer::Draw(FVec2 position, float angle, const SpriteAsset& sprite) {
  std::string_view spritesheet = FlatbufferStringview(sprite.spritesheet());
  if (current_spritesheet_ != spritesheet) {
    CHECK(spritesheet_info_.Lookup(spritesheet, &current_),
          "could not find texture [", spritesheet, "]");
  }
  renderer_->SetActiveTexture(current_.texture);
  const FVec2 p0(position - FVec2(sprite.width() / 2.0, sprite.height() / 2.0));
  const FVec2 p1(position + FVec2(sprite.width() / 2.0, sprite.height() / 2.0));
  const FVec2 q0(FVec2(1.0 * sprite.x() / current_.width,
                       1.0 * sprite.y() / current_.height));
  const FVec2 q1(1.0 * (sprite.x() + sprite.width()) / current_.width,
                 1.0 * (sprite.y() + sprite.height()) / current_.height);
  renderer_->PushQuad(p0, p1, q0, q1, position, angle);
}

void Renderer::DrawRect(FVec2 top_left, FVec2 bottom_right, float angle) {
  renderer_->SetActiveTexture(0);
  const FVec2 center = (top_left + bottom_right) / 2;
  renderer_->PushQuad(top_left, bottom_right, FVec2(0, 0), FVec2(1, 1),
                      /*origin=*/center, angle);
}

void Renderer::DrawCircle(FVec2 center, float radius) {
  renderer_->SetActiveTexture(0);
  constexpr size_t kTriangles = 30;
  auto for_index = [&](int index) {
    const int i = index % kTriangles;
    return FVec(center.x + radius * std::cos(2 * i * M_PI / kTriangles),
                center.y + radius * std::sin(2 * i * M_PI / kTriangles));
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
    for (const SpriteAsset* sprite : *sheet->sprites()) {
      std::string_view spritesheet =
          FlatbufferStringview(sprite->spritesheet());
      if (!spritesheet_info_.Lookup(spritesheet)) {
        const char* image_name = sheet->image_name()->c_str();
        auto* image = assets.GetImage(image_name);
        CHECK(image != nullptr, "Unknown image ", image_name,
              " for spritesheet ", spritesheet);
        SheetTexture info = {.texture = renderer_->LoadTexture(*image),
                             .width = image->width(),
                             .height = image->height()};
        LOG("Loaded spritesheet ", spritesheet);
        spritesheet_info_.Insert(spritesheet, info);
      }
      std::string_view sprite_name = FlatbufferStringview(sprite->name());
      sprites_.Insert(sprite_name, sprite);
    }
  }
}

void Renderer::LoadFonts(const Assets& assets, float pixel_height) {
  DCHECK(assets.fonts() < fonts_.size(), "Too many fonts");
  for (size_t i = 0; i < assets.fonts(); ++i) {
    FontInfo& font = fonts_[i];
    const FontAsset& font_asset = *assets.GetFontByIndex(i);
    const uint8_t* font_buffer = font_asset.contents()->data();
    CHECK(stbtt_InitFont(&font.font_info, font_buffer,
                         stbtt_GetFontOffsetForIndex(font_buffer, 0)),
          "Could not initialize ", FlatbufferStringview(font_asset.filename()));
    font.scale = stbtt_ScaleForPixelHeight(&font.font_info, pixel_height);
    stbtt_GetFontVMetrics(&font.font_info, &font.ascent, &font.descent,
                          &font.line_gap);
    stbtt_PackBegin(&font.context, font.atlas.data(), kAtlasWidth, kAtlasHeight,
                    kAtlasWidth, 1, nullptr);
    stbtt_PackSetOversampling(&font.context, 2, 2);
    CHECK(stbtt_PackFontRange(&font.context, font_buffer, 0, pixel_height, 0,
                              256, font.chars.data()) == 1,
          "Could not load font");
    stbtt_PackEnd(&font.context);
    uint8_t* buffer = new uint8_t[4 * font.atlas.size()];
    for (size_t i = 0, j = 0; j < font.atlas.size(); j++, i += 4) {
      std::memset(&buffer[i], font.atlas[j], 4);
    }
    font.texture = renderer_->LoadTexture(buffer, kAtlasWidth, kAtlasHeight);
    delete[] buffer;
    font_table_.Insert(FlatbufferStringview(font_asset.filename()), &font);
  }
}

void Renderer::DrawText(std::string_view font, float size, std::string_view str,
                        FVec2 position) {
  FontInfo* info = nullptr;
  CHECK(font_table_.Lookup(font, &info), "No font called ", font);
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
      renderer_->PushQuad(FVec2(q.x0, q.y1), FVec2(q.x1, q.y0),
                          FVec2(q.s0, q.t1), FVec2(q.s1, q.t0), FVec2(0, 0),
                          /*angle=*/0);
    }
  }
}

}  // namespace G