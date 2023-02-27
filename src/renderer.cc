#include "renderer.h"

#include <cmath>
#include <vector>

#include "transformations.h"

namespace {

constexpr std::string_view kVertexShader = R"(
    #version 460 core

    layout (location = 0) in vec2 input_position;
    layout (location = 1) in vec2 input_tex_coord;
    layout (location = 2) in vec2 origin;
    layout (location = 3) in float angle;
        
    uniform mat4x4 projection;
    uniform mat4x4 transform;
    uniform vec4 color;

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
        out_color = color;
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

QuadRenderer::QuadRenderer(IVec2 viewport)
    : vertex_shader_(compiler_.CompileOrDie(ShaderType::kVertex, "quad.vert",
                                            kVertexShader.data(),
                                            kVertexShader.size())),
      fragment_shader_(compiler_.CompileOrDie(
          ShaderType::kFragment, "quad.frag", kFragmentShader.data(),
          kFragmentShader.size())),
      shader_program_(compiler_.LinkOrDie(vertex_shader_, fragment_shader_)),
      viewport_(viewport) {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);
  // Load an empty texture, just white pixels, to be able to draw colors without
  // if statements in the shader.
  uint8_t white_pixels[32 * 32 * 4];
  std::memset(white_pixels, 255, sizeof(white_pixels));
  LoadTexture(&white_pixels, /*width=*/32, /*height=*/32);
}

QuadRenderer::~QuadRenderer() {
  glDeleteBuffers(1, &vbo_);
  glDeleteBuffers(1, &ebo_);
}

GLuint QuadRenderer::LoadTexture(const void* data, size_t width,
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

void QuadRenderer::PushQuad(FVec2 p0, FVec2 p1, FVec2 q0, FVec2 q1,
                            FVec2 origin, float angle) {
  auto& batch = batches_.back();
  size_t current = vertices_.size();
  vertices_.push_back({.position = FVec2(p0.x, p1.y),
                       .tex_coords = FVec2(q0.x, q1.y),
                       .origin = origin,
                       .angle = angle});
  vertices_.push_back(
      {.position = p1, .tex_coords = q1, .origin = origin, .angle = angle});
  vertices_.push_back({.position = FVec2(p1.x, p0.y),
                       .tex_coords = FVec2(q1.x, q0.y),
                       .origin = origin,
                       .angle = angle});
  vertices_.push_back(
      {.position = p0, .tex_coords = q0, .origin = origin, .angle = angle});
  for (int i : {0, 1, 3, 1, 2, 3}) {
    indices_.push_back(current + i);
    batch.indices_count++;
  }
}

void QuadRenderer::Render() {
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, ByteSize(vertices_), vertices_.data(),
               GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, ByteSize(indices_), indices_.data(),
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
  for (const auto& batch : batches_) {
    shader_program_.SetUniform("tex", batch.texture_unit);
    shader_program_.SetUniform("projection",
                               Ortho(0, viewport_.x, 0, viewport_.y));
    shader_program_.SetUniform("color", batch.rgba_color);
    shader_program_.SetUniform("transform", batch.transform);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBindTexture(GL_TEXTURE_2D, tex_[batch.texture_unit]);
    const auto indices_start =
        reinterpret_cast<uintptr_t>(&indices_[batch.indices_start]) -
        reinterpret_cast<uintptr_t>(&indices_[0]);
    glDrawElementsInstanced(GL_TRIANGLES, batch.indices_count, GL_UNSIGNED_INT,
                            reinterpret_cast<void*>(indices_start), 1);
  }
}

SpriteSheetRenderer::SpriteSheetRenderer(Assets* assets, QuadRenderer* renderer)
    : renderer_(renderer) {
  for (size_t i = 0; i < assets->spritesheets(); ++i) {
    auto* sheet = assets->GetSpritesheetByIndex(i);
    for (const auto* texture : *sheet->sub_texture()) {
      const char* spritesheet = texture->spritesheet()->c_str();
      size_t len = texture->spritesheet()->size();
      if (!sheet_info_.Lookup(spritesheet, len)) {
        const char* image_name = sheet->image_name()->c_str();
        auto* image = assets->GetImage(image_name);
        CHECK(image != nullptr, "Unknown image ", image_name,
              " for spritesheet ", spritesheet);
        Sheet info = {.texture = renderer->LoadTexture(*image),
                      .width = image->width(),
                      .height = image->height()};
        sheet_info_.Insert(spritesheet, len, info);
      }
      subtexts_.Insert(texture->name()->c_str(), texture->name()->size(),
                       texture);
    }
  }
}

void SpriteSheetRenderer::BeginFrame() {
  transform_stack_.Clear();
  transform_stack_.Push(FMat4x4::Identity());
  ApplyTransform(FMat4x4::Identity());
}

const assets::Subtexture* SpriteSheetRenderer::sub_texture(const char* name,
                                                           size_t length) {
  const assets::Subtexture* result = nullptr;
  if (!subtexts_.Lookup(name, length, &result)) return nullptr;
  return result;
}

void SpriteSheetRenderer::Push() {
  transform_stack_.Push(transform_stack_.back());
}

void SpriteSheetRenderer::Pop() {
  transform_stack_.Pop();
  renderer_->SetActiveTransform(transform_stack_.back());
}

void SpriteSheetRenderer::SetColor(FVec4 color) {
  renderer_->SetActiveColor(color);
}

void SpriteSheetRenderer::Draw(FVec2 position, float angle,
                               const assets::Subtexture& texture) {
  Sheet sheet;
  CHECK(sheet_info_.Lookup(texture.spritesheet()->c_str(),
                           texture.spritesheet()->size(), &sheet));
  renderer_->SetActiveTexture(sheet.texture);
  const FVec2 p0(position -
                 FVec2(texture.width() / 2.0, texture.height() / 2.0));
  const FVec2 p1(position +
                 FVec2(texture.width() / 2.0, texture.height() / 2.0));
  FVec2 q0(
      FVec2(1.0 * texture.x() / sheet.width, 1.0 * texture.y() / sheet.height));
  FVec2 q1(1.0 * (texture.x() + texture.width()) / sheet.width,
           1.0 * (texture.y() + texture.height()) / sheet.height);
  renderer_->PushQuad(p0, p1, q0, q1, position, angle);
}

void SpriteSheetRenderer::DrawRect(FVec2 top_left, FVec2 bottom_right,
                                   float angle) {
  const FVec2 center = (top_left + bottom_right) / 2;
  renderer_->PushQuad(top_left, bottom_right, FVec2(0, 0), FVec2(1, 1),
                      /*origin=*/center, angle);
}