#include "shaders.h"

namespace G {

ShaderId ShaderId::Make(ShaderType type) {
  const GLuint shader_type =
      type == ShaderType::kVertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
  const GLuint id = glCreateShader(shader_type);
  return ShaderId(type, id);
}

ShaderId ShaderCompiler::CompileOrDie(ShaderType type, std::string_view name,
                                      std::string_view glsl) {
  auto shader_id = ShaderId::Make(type);
  const GLuint shader = shader_id.id();
  CHECK(shader != 0, "Could not compile shader ", name);
  const char* code = glsl.data();
  size_t size = glsl.size();
  glShaderSource(shader, 1, &code, reinterpret_cast<const GLint*>(&size));
  glCompileShader(shader);
  int success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info_log[512] = {0};
    glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
    DIE("Could not compile shader ", name, ": ", info_log);
  }
  LOG("Compiled ", (type == ShaderType::kVertex ? "vertex" : "fragment"),
      " shader ", name, " with id ", shader_id.id());
  return shader_id;
}

ShaderProgram ShaderCompiler::LinkOrDie(std::string_view name,
                                        const ShaderId& vertex_shader,
                                        const ShaderId& fragment_shader) {
  ShaderProgram program(glCreateProgram());
  const GLuint shader_program = program.id();
  CHECK(shader_program != 0, "Could not link shaders into ", name);
  glAttachShader(shader_program, vertex_shader.id());
  glAttachShader(shader_program, fragment_shader.id());
  glBindFragDataLocation(shader_program, 0, "frag_color");
  glLinkProgram(shader_program);
  int success;
  glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
  if (!success) {
    char info_log[512] = {0};
    glGetProgramInfoLog(shader_program, sizeof(info_log), nullptr, info_log);
    DIE("Could not link shaders into ", name, ": ", info_log);
  }
  LOG("Linked program ", name, " with id ", program.id());
  return program;
}

}  // namespace G