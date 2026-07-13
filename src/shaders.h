#pragma once
#ifndef _GAME_SHADERS_H
#define _GAME_SHADERS_H

#include "array.h"
#include "assets.h"
#include "dictionary.h"
#include "error.h"
#include "gl_headers.h"
#include "logging.h"
#include "mat.h"
#include "vec.h"

namespace G {
namespace internal {

inline void AsOpenglUniform(const FVec2& v, GLint location) {
  glUniform2f(location, v.x, v.y);
}

inline void AsOpenglUniform(const FVec3& v, GLint location) {
  glUniform3f(location, v.x, v.y, v.z);
}

inline void AsOpenglUniform(const FVec4& v, GLint location) {
  glUniform4f(location, v.x, v.y, v.z, v.w);
}

inline void AsOpenglUniform(const FMat2x2 m, GLint location) {
  glUniformMatrix2fv(location, 1, GL_TRUE, m.v);
}

inline void AsOpenglUniform(const FMat3x3 m, GLint location) {
  glUniformMatrix3fv(location, 1, GL_TRUE, m.v);
}

inline void AsOpenglUniform(const FMat4x4 m, GLint location) {
  glUniformMatrix4fv(location, 1, GL_TRUE, m.v);
}

}  // namespace internal

class Shaders {
 public:
  enum UseCache { kUseCache, kForceCompile };

  Shaders(Allocator* allocator);
  ~Shaders();

  ErrorOr<void> Compile(DbAssets::ShaderType type, std::string_view name,
                        std::string_view glsl, UseCache use_cache);

  ErrorOr<void> Link(std::string_view name, std::string_view vertex_shader,
                     std::string_view fragment_shader, UseCache use_cache);

  void UseProgram(std::string_view program);

  ErrorOr<void> Load(const DbAssets::Shader& shader);

  template <typename T, typename = std::void_t<decltype(T::kCardinality)>>
  ErrorOr<void> SetUniform(const char* name, const T& value) {
    if (!current_program_) return Error::Message("No program set");
    const GLint uniform = glGetUniformLocation(current_program_, name);
    if (uniform == -1) {
      LOG("No uniform '", name, "' in shader '", current_program_name_, "'");
      return Error::Message("No uniform with that name");
    }
    internal::AsOpenglUniform(value, uniform);
    return {};
  }

  ErrorOr<void> SetUniform(const char* name, int value) {
    if (!current_program_) return Error::Message("No program set");
    const GLint uniform = glGetUniformLocation(current_program_, name);
    if (uniform == -1) {
      LOG("No uniform '", name, "' in shader '", current_program_name_, "'");
      return Error::Message("No uniform with that name");
    }
    OPENGL_CALL(glUniform1i(uniform, value));
    return {};
  }

  ErrorOr<void> SetUniformF(const char* name, float value) {
    if (!current_program_) return Error::Message("No program set");
    const GLint uniform = glGetUniformLocation(current_program_, name);
    if (uniform == -1) {
      LOG("No uniform '", name, "' in shader '", current_program_name_, "'");
      return Error::Message("No uniform with that name");
    }
    OPENGL_CALL(glUniform1f(uniform, value));
    return {};
  }

  template <typename T, typename = std::void_t<decltype(T::kCardinality)>>
  void SetUniformSilent(const char* name, const T& value) {
    if (!current_program_) return;
    const GLint uniform = glGetUniformLocation(current_program_, name);
    if (uniform == -1) return;
    internal::AsOpenglUniform(value, uniform);
  }

  void SetUniformSilent(const char* name, int value) {
    if (!current_program_) return;
    const GLint uniform = glGetUniformLocation(current_program_, name);
    if (uniform == -1) return;
    glUniform1i(uniform, value);
  }

  void SetUniformSilentF(const char* name, float value) {
    if (!current_program_) return;
    const GLint uniform = glGetUniformLocation(current_program_, name);
    if (uniform == -1) return;
    glUniform1f(uniform, value);
  }

  // Returns the compiled programs dictionary for debug inspection.
  const Dictionary<GLuint>& programs() const { return compiled_programs_; }

  // Returns the compiled shaders dictionary for debug inspection.
  const Dictionary<GLuint>& shaders() const { return compiled_shaders_; }

  bool HasUniform(const char* name) const {
    if (!current_program_) return false;
    return glGetUniformLocation(current_program_, name) != -1;
  }

  GLint AttributeLocation(const char* name) const {
    DCHECK(current_program_, "No program set");
    return glGetAttribLocation(current_program_, name);
  }

 private:
  Allocator* allocator_;
  Dictionary<GLuint> compiled_shaders_;
  Dictionary<GLuint> compiled_programs_;
  FixedArray<GLuint> gl_shader_handles_;
  FixedArray<GLuint> gl_program_handles_;
  GLuint current_program_ = 0;
  const char* current_program_name_ = "(none)";
};

}  // namespace G

#endif  // _GAME_SHADERS_H
