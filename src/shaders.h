#pragma once
#ifndef _GAME_SHADERS_H
#define _GAME_SHADERS_H

#include "assets.h"
#include "libraries/glad.h"
#include "logging.h"
#include "lookup_table.h"

namespace G {

class Shaders {
 public:
  Shaders(const Assets& assets);
  ~Shaders();

  bool Compile(ShaderType type, std::string_view name, std::string_view glsl);

  bool Link(std::string_view name, std::string_view vertex_shader,
            std::string_view fragment_shader);

  void UseProgram(std::string_view program) {
    GLuint program_id;
    CHECK(compiled_programs_.Lookup(program, &program_id),
          " could not find program ", program);
    current_program_ = program_id;
    glUseProgram(current_program_);
  }

  std::string_view LastError() const { return last_error_.piece(); };

  template <typename T, typename = std::void_t<decltype(T::kCardinality)>>
  void SetUniform(const char* name, const T& value) {
    DCHECK(current_program_, "No program set");
    const GLint uniform = glGetUniformLocation(current_program_, name);
    CHECK(uniform != -1, "No uniform named ", name);
    value.AsOpenglUniform(uniform);
  }

  void SetUniform(const char* name, int value) {
    DCHECK(current_program_, "No program set");
    const GLint uniform = glGetUniformLocation(current_program_, name);
    CHECK(uniform != -1, "No uniform named ", name);
    glUniform1i(uniform, value);
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

  LookupTable<GLuint> compiled_shaders_;
  LookupTable<GLuint> compiled_programs_;
  FixedStringBuffer<512> last_error_;
  GLuint current_program_ = 0;
};

}  // namespace G

#endif  // _GAME_SHADERS_H