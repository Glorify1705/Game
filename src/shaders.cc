#include "shaders.h"

#include "clock.h"
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
    out vec2 screen_coord;

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
        screen_coord = input_position.xy;
    }
  )";

constexpr std::string_view kPrePassFragmentShader = R"(
    #version 460 core
    out vec4 frag_color;

    in vec2 tex_coord;
    in vec4 out_color;
    in vec2 screen_coord;

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
  #line 1
)";

constexpr std::string_view kFragmentShaderPostamble = R"(
  out vec4 frag_color;

  in vec2 tex_coord;
  in vec2 screen_coord;
  in vec4 out_color;

  uniform sampler2D tex;

  void main() { 
      frag_color = effect(out_color, tex, tex_coord, screen_coord);
  }
)";

struct Error {
  std::string_view file;
  int line;
  std::string_view error_msg;
};

int GetLineNumber(std::string_view err) {
  auto beg = err.find('(');
  auto end = err.find(')');
  int l = 0;
  for (size_t i = beg + 1; i < end; ++i) {
    l = 10 * l + (err[i] - '0');
  }
  return l;
}

}  // namespace

Shaders::Shaders(ErrorHandler handler, Allocator* allocator)
    : handler_(handler),
      allocator_(allocator),
      compiled_shaders_(allocator),
      compiled_programs_(allocator),
      gl_shader_handles_(128, allocator),
      gl_program_handles_(128, allocator) {
  // Ensure we have the basic shaders available.
  CHECK(Compile(DbAssets::ShaderType::kVertex, "pre_pass.vert",
                kPrePassVertexShader, kUseCache),
        LastError());
  CHECK(Compile(DbAssets::ShaderType::kFragment, "pre_pass.frag",
                kPrePassFragmentShader, kUseCache),
        LastError());
  CHECK(Link("pre_pass", "pre_pass.vert", "pre_pass.frag", kUseCache),
        LastError());
  CHECK(Compile(DbAssets::ShaderType::kVertex, "post_pass.vert",
                kPostPassVertexShader, kUseCache),
        LastError());
  CHECK(Compile(DbAssets::ShaderType::kFragment, "post_pass.frag",
                kPostPassFragmentShader, kUseCache),
        LastError());
  CHECK(Link("post_pass", "post_pass.vert", "post_pass.frag", kUseCache),
        LastError());
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
                      std::string_view glsl, UseCache use_cache) {
  GLuint shader_idx;
  if (compiled_shaders_.Lookup(name, &shader_idx)) {
    if (use_cache == UseCache::kUseCache) {
      LOG("Ignoring already processed shader ", name);
      return true;
    } else {
      glDeleteShader(shader_idx);
    }
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
  OPENGL_CALL(glCompileShader(shader), "Compiling shader ", name, ": ", glsl);
  int success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info_log[512] = {0};
    glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
    return FillError(name, GetLineNumber(info_log), info_log);
  }
  LOG("Compiled ",
      (type == DbAssets::ShaderType::kVertex ? "vertex" : "fragment"),
      " shader ", name, " with id ", shader);
  gl_shader_handles_.Push(shader);
  compiled_shaders_.Insert(name, shader);
  return true;
}

void Shaders::Reload(const DbAssets::Shader& shader) {
  ArenaAllocator scratch(allocator_, Kilobytes(64));
  const size_t total_size = kFragmentShaderPreamble.size() + shader.size +
                            kFragmentShaderPostamble.size() + 1;
  auto* buf = reinterpret_cast<char*>(scratch.Alloc(total_size, /*align=*/1));
  StringBuffer code(buf, total_size);
  code.Append(kFragmentShaderPreamble);
  code.AppendBuffer(shader.contents, shader.size);
  code.Append(kFragmentShaderPostamble);
  if (!Compile(shader.type, shader.name, code.piece(), kForceCompile)) {
    handler_.handler(handler_.ud, last_error_.file.piece(), last_error_.line,
                     last_error_.error.piece());
  }
}

bool Shaders::Link(std::string_view name, std::string_view vertex_shader,
                   std::string_view fragment_shader, UseCache use_cache) {
  GLuint program_id;
  if (compiled_programs_.Lookup(name, &program_id)) {
    if (use_cache == UseCache::kUseCache) {
      return true;
    } else {
      glDeleteProgram(program_id);
    }
  }
  GLuint shader_program = glCreateProgram();
  GLuint vertex, fragment;
  if (!compiled_shaders_.Lookup(vertex_shader, &vertex)) {
    return FillError(__FILE__, __LINE__, "Could not find vertex shader ",
                     vertex_shader);
  }
  if (!compiled_shaders_.Lookup(fragment_shader, &fragment)) {
    return FillError(__FILE__, __LINE__, "Could not find fragment shader ",
                     fragment_shader);
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
    return FillError(__FILE__, __LINE__, "Could not link shaders into ", name,
                     ": ", info_log);
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
