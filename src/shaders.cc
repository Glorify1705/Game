#include "shaders.h"

ShaderId ShaderId::Make(ShaderType type) {
  const GLuint shader_type =
      type == ShaderType::kVertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
  const GLuint id = glCreateShader(shader_type);
  return ShaderId(type, id);
}

ShaderId ShaderCompiler::CompileOrDie(ShaderType type, const char* name,
                                      const char* data, size_t size) {
  auto shader_id = ShaderId::Make(type);
  const GLuint shader = shader_id.id();
  CHECK(shader != 0, "Could not compile shader ", name);
  glShaderSource(shader, 1, &data, reinterpret_cast<const GLint*>(&size));
  glCompileShader(shader);
  int success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info_log[512] = {0};
    glGetShaderInfoLog(shader, 512, nullptr, info_log);
    DIE("Could not compile shader ", name, ": ", info_log);
  }
  return shader_id;
}

ShaderProgram ShaderCompiler::LinkOrDie(const ShaderId& vertex_shader,
                                        const ShaderId& fragment_shader) {
  ShaderProgram program(glCreateProgram());
  const GLuint shader_program = program.id();
  CHECK(shader_program != 0, "Could not link shaders");
  glAttachShader(shader_program, vertex_shader.id());
  glAttachShader(shader_program, fragment_shader.id());
  glBindFragDataLocation(shader_program, 0, "frag_color");
  glLinkProgram(shader_program);
  int success;
  glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
  if (!success) {
    char info_log[512] = {0};
    glGetProgramInfoLog(shader_program, 512, nullptr, info_log);
    DIE("Could not link shaders: ", info_log);
  }
  return program;
}
