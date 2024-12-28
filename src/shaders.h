#pragma once
#ifndef _GAME_SHADERS_H
#define _GAME_SHADERS_H

#include "array.h"
#include "assets.h"
#include "dictionary.h"
#include "libraries/glad.h"
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

inline void AsOpenglUniform(const DVec2& v, GLint location) {
  glUniform2d(location, v.x, v.y);
}

inline void AsOpenglUniform(const DVec3& v, GLint location) {
  glUniform3d(location, v.x, v.y, v.z);
}

inline void AsOpenglUniform(const DVec4& v, GLint location) {
  glUniform4d(location, v.x, v.y, v.z, v.w);
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

inline void AsOpenglUniform(const DMat2x2 m, GLint location) {
  glUniformMatrix2dv(location, 1, GL_TRUE, m.v);
}

inline void AsOpenglUniform(const DMat3x3 m, GLint location) {
  glUniformMatrix3dv(location, 1, GL_TRUE, m.v);
}

inline void AsOpenglUniform(const DMat4x4 m, GLint location) {
  glUniformMatrix4dv(location, 1, GL_TRUE, m.v);
}

}  // namespace internal

class Shaders {
 public:
  Shaders(Allocator* allocator);
  ~Shaders();

  void CompileAssetShaders(const DbAssets& assets);

  bool Compile(DbAssets::ShaderType type, std::string_view name,
               std::string_view glsl);

  bool Link(std::string_view name, std::string_view vertex_shader,
            std::string_view fragment_shader);

  void UseProgram(std::string_view program);

  std::string_view LastError() const { return last_error_.piece(); };

  template <typename T, typename = std::void_t<decltype(T::kCardinality)>>
  bool SetUniform(const char* name, const T& value) {
    if (!current_program_) return FillError("No program set");
    const GLint uniform = glGetUniformLocation(current_program_, name);
    if (uniform == -1) return FillError("No uniform named ", name);
    internal::AsOpenglUniform(value, uniform);
    return true;
  }

  bool SetUniform(const char* name, int value) {
    if (!current_program_) return FillError("No program set");
    const GLint uniform = glGetUniformLocation(current_program_, name);
    if (uniform == -1) return FillError("No uniform named ", name);
    OPENGL_CALL(glUniform1i(uniform, value));
    return true;
  }

  bool SetUniformF(const char* name, float value) {
    if (!current_program_) return FillError("No program set");
    const GLint uniform = glGetUniformLocation(current_program_, name);
    if (uniform == -1) return FillError("No uniform named ", name);
    OPENGL_CALL(glUniform1f(uniform, value));
    return true;
  }

  GLint AttributeLocation(const char* name) const {
    DCHECK(current_program_, "No program set");
    return glGetAttribLocation(current_program_, name);
  }

 private:
  template <typename... Ts>
  bool FillError(Ts... ts) {
    last_error_.Append(std::forward<Ts>(ts)...);
    return false;
  }

  Allocator* allocator_;
  Dictionary<GLuint> compiled_shaders_;
  Dictionary<GLuint> compiled_programs_;
  FixedArray<GLuint> gl_shader_handles_;
  FixedArray<GLuint> gl_program_handles_;
  FixedStringBuffer<512> last_error_;
  GLuint current_program_ = 0;
};

}  // namespace G

#endif  // _GAME_SHADERS_H
