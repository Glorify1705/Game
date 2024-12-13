#include "shaders.h"

#include "units.h"

namespace G {
namespace {

constexpr std::string_view kPrePassVertexShader = R"(
    #version 460 core

    layout (location = 0) in vec3 input_position;
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
        gl_Position = projection * transform * rotation * vec4(input_position, 1.0);
        tex_coord = input_tex_coord;
        out_color = global_color * (color / 256.0);
    }
  )";

constexpr std::string_view kPrePassFragmentShader = R"(
    #version 460 core
    out vec4 frag_color;

    in vec2 tex_coord;
    in vec4 out_color;

    uniform sampler2D tex;

    void main() {
        vec4 color = texture(tex, tex_coord) * out_color;
        frag_color = color;
    }
  )";

constexpr std::string_view kPostPassVertexShader = R"(
  #version 460 core
  layout (location = 0) in vec2 input_position;
  layout (location = 1) in vec2 input_tex_coord;

  out vec2 tex_coord;

  void main()
  {
      gl_Position = vec4(input_position.x, input_position.y, 0.0, 1.0); 
      tex_coord = input_tex_coord;
  }  
  )";

constexpr std::string_view kPostPassFragmentShader = R"(
  #version 460 core
  out vec4 frag_color;
    
  in vec2 tex_coord;

  uniform sampler2D screen_texture;

  void main() { 
      frag_color = texture(screen_texture, tex_coord);
  }
)";

constexpr std::string_view kFragmentShaderPreamble = R"(
  #version 460 core
)";

constexpr std::string_view kFragmentShaderPostamble = R"(
  out vec4 frag_color;

  in vec2 tex_coord;

  uniform sampler2D screen_texture;

  void main() { 
      frag_color = effect(screen_texture, tex_coord);
  }
)";

}  // namespace

Shaders::Shaders(const DbAssets& assets, Allocator* allocator)
    : compiled_shaders_(allocator),
      compiled_programs_(allocator),
      gl_shader_handles_(128, allocator),
      gl_program_handles_(128, allocator) {
  for (const auto& shader : assets.GetShaders()) {
    ArenaAllocator scratch(allocator, Megabytes(1));
    const size_t total_size = kFragmentShaderPreamble.size() + shader.size +
                              kFragmentShaderPostamble.size() + 1;
    auto* assembled = reinterpret_cast<char*>(scratch.Alloc(total_size, 1));
    size_t assembled_size = 0;
    auto assemble = [&](const void* b, size_t s) {
      std::memcpy(&assembled[assembled_size], b, s);
      assembled_size += s;
    };
    assemble(kFragmentShaderPreamble.data(), kFragmentShaderPreamble.size());
    assemble(shader.contents, shader.size);
    assemble(kFragmentShaderPostamble.data(), kFragmentShaderPostamble.size());
    assembled[assembled_size] = 0;
    CHECK(Compile(shader.type, shader.name,
                  std::string_view(assembled, assembled_size)),
          LastError());
  }
  CHECK(Compile(DbAssets::ShaderType::kVertex, "pre_pass.vert",
                kPrePassVertexShader),
        LastError());
  CHECK(Compile(DbAssets::ShaderType::kFragment, "pre_pass.frag",
                kPrePassFragmentShader),
        LastError());
  CHECK(Link("pre_pass", "pre_pass.vert", "pre_pass.frag"), LastError());
  CHECK(Compile(DbAssets::ShaderType::kVertex, "post_pass.vert",
                kPostPassVertexShader),
        LastError());
  CHECK(Compile(DbAssets::ShaderType::kFragment, "post_pass.frag",
                kPostPassFragmentShader),
        LastError());
  CHECK(Link("post_pass", "post_pass.vert", "post_pass.frag"), LastError());
}

Shaders::~Shaders() {
  for (GLuint handle : gl_shader_handles_) {
    glDeleteShader(handle);
  }
  for (GLuint handle : gl_program_handles_) {
    glDeleteProgram(handle);
  }
}

bool Shaders::Compile(DbAssets::ShaderType type, std::string_view name,
                      std::string_view glsl) {
  if (compiled_shaders_.Contains(name)) {
    LOG("Ignoring already processed shader ", name);
    return true;
  }
  const GLuint shader_type = type == DbAssets::ShaderType::kVertex
                                 ? GL_VERTEX_SHADER
                                 : GL_FRAGMENT_SHADER;
  const GLuint shader = glCreateShader(shader_type);
  CHECK(shader != 0, "Could not compile shader ", name);
  const char* code = glsl.data();
  size_t size = glsl.size();
  OPENGL_CALL(
      glShaderSource(shader, 1, &code, reinterpret_cast<const GLint*>(&size)));
  LOG("Shader = [", name, "] = [", glsl, "]");
  OPENGL_CALL(glCompileShader(shader), "Compiling shader ", name, ": ", glsl);
  int success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info_log[512] = {0};
    glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
    return FillError("Could not compile shader ", name, ": ", info_log);
  }
  LOG("Compiled ",
      (type == DbAssets::ShaderType::kVertex ? "vertex" : "fragment"),
      " shader ", name, " with id ", shader);
  gl_shader_handles_.Push(shader);
  compiled_shaders_.Insert(name, shader);
  return true;
}

bool Shaders::Link(std::string_view name, std::string_view vertex_shader,
                   std::string_view fragment_shader) {
  if (compiled_programs_.Contains(name)) return true;
  GLuint shader_program = glCreateProgram();
  GLuint vertex, fragment;
  if (!compiled_shaders_.Lookup(vertex_shader, &vertex)) {
    return FillError("Could not find vertex shader ", vertex_shader);
  }
  if (!compiled_shaders_.Lookup(fragment_shader, &fragment)) {
    return FillError("Could not find fragment shader ", fragment_shader);
  }
  CHECK(shader_program != 0, "Could not link shaders into ", name);
  OPENGL_CALL(glAttachShader(shader_program, vertex));
  OPENGL_CALL(glAttachShader(shader_program, fragment));
  glBindFragDataLocation(shader_program, 0, "frag_color");
  glLinkProgram(shader_program);
  int success;
  OPENGL_CALL(glGetProgramiv(shader_program, GL_LINK_STATUS, &success));
  if (!success) {
    char info_log[512] = {0};
    glGetProgramInfoLog(shader_program, sizeof(info_log), nullptr, info_log);
    return FillError("Could not link shaders into ", name, ": ", info_log);
  }
  LOG("Linked program ", name, " with id ", shader_program,
      " from vertex shader ", vertex, " (", vertex_shader,
      ") and fragment shader ", fragment, " (", fragment_shader, ")");
  gl_program_handles_.Push(shader_program);
  compiled_programs_.Insert(name, shader_program);
  return true;
}

void Shaders::UseProgram(std::string_view program) {
  GLuint program_id;
  CHECK(compiled_programs_.Lookup(program, &program_id),
        " could not find program ", program);
  current_program_ = program_id;
  OPENGL_CALL(glUseProgram(current_program_));
}

}  // namespace G
