#pragma once
#ifndef _GAME_SHADERS_H
#define _GAME_SHADERS_H

#include "glad.h"
#include "logging.h"

enum class ShaderType { kVertex, kFragment };

class ShaderId {
 public:
  ShaderId(ShaderType type, unsigned int id) : type_(type), id_(id) {}

  ShaderId(const ShaderId&) = delete;
  ShaderId& operator=(ShaderId&) = delete;

  static ShaderId Make(ShaderType type);

  ShaderId(ShaderId&& other) : type_(other.type_), id_(other.id_) {
    other.id_ = 0;
  }

  ~ShaderId() {
    if (id_) glDeleteShader(id_);
  }

  ShaderType type() const { return type_; }
  GLuint id() const { return id_; }

 private:
  ShaderType type_;
  GLuint id_;
};

class ShaderProgram {
 public:
  ShaderProgram(GLuint id) : id_(id) {}

  GLuint id() { return id_; }
  void Use() { glUseProgram(id_); }

  ShaderProgram(const ShaderProgram&) = delete;
  ShaderProgram& operator=(ShaderProgram&) = delete;

  ShaderProgram(ShaderProgram&& other) : id_(other.id_) { other.id_ = 0; }

  template <typename T, typename = std::void_t<decltype(T::kCardinality)>>
  void SetUniform(const char* name, const T& value) {
    const GLint uniform = glGetUniformLocation(id_, name);
    CHECK(uniform != -1, "No uniform named ", name);
    value.AsOpenglUniform(uniform);
  }

  void SetUniform(const char* name, int value) {
    const GLint uniform = glGetUniformLocation(id_, name);
    CHECK(uniform != -1, "No uniform named ", name);
    glUniform1i(uniform, value);
  }

  ~ShaderProgram() {
    if (id_) glDeleteProgram(id_);
  }

 private:
  GLuint id_;
};

class ShaderCompiler {
 public:
  ShaderId CompileOrDie(ShaderType type, const char* name, const char* data,
                        size_t size);

  ShaderProgram LinkOrDie(const ShaderId& vertex_shader,
                          const ShaderId& fragment_shader);
};

#endif  // _GAME_SHADERS_H