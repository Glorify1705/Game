#pragma once
#ifndef _GAME_GL_H
#define _GAME_GL_H

#include "gl_headers.h"
#include "logging.h"
#include "stringlib.h"

namespace G {
namespace GL {

// Scoped OpenGL state guard base. Pushes a debug group on construction
// (debug builds only), pops it on destruction.
class ScopeBase {
 public:
  ScopeBase(const ScopeBase&) = delete;
  ScopeBase& operator=(const ScopeBase&) = delete;
  ScopeBase(ScopeBase&&) = delete;
  ScopeBase& operator=(ScopeBase&&) = delete;

 protected:
  ScopeBase(const char* label, const char* file, int line) {
#if defined(GAME_WITH_ASSERTS) && !defined(GAME_WEB)
    SetOpenGLLine(file, line);
    SmallBuffer buf;
    buf.Append(label, " [", TrimPath(file), ":", line, "]");
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, buf.str());
#else
    (void)label;
    (void)file;
    (void)line;
#endif
  }

  ~ScopeBase() {
#if defined(GAME_WITH_ASSERTS) && !defined(GAME_WEB)
    glPopDebugGroup();
#endif
  }
};

// Binds a framebuffer for the duration of the scope.
class FramebufferScope final : ScopeBase {
 public:
  FramebufferScope(GLenum target, GLuint fbo,
                   const char* file = __builtin_FILE(),
                   int line = __builtin_LINE())
      : ScopeBase("FramebufferScope", file, line), target_(target) {
    glBindFramebuffer(target, fbo);
  }

  ~FramebufferScope() { glBindFramebuffer(target_, 0); }

 private:
  GLenum target_;
};

// Binds a texture for the duration of the scope.
class TextureScope final : ScopeBase {
 public:
  TextureScope(GLenum target, GLuint tex, const char* file = __builtin_FILE(),
               int line = __builtin_LINE())
      : ScopeBase("TextureScope", file, line), target_(target) {
    glBindTexture(target, tex);
  }

  ~TextureScope() { glBindTexture(target_, 0); }

 private:
  GLenum target_;
};

// Binds a shader program for the duration of the scope.
class ProgramScope final : ScopeBase {
 public:
  ProgramScope(GLuint program, const char* file = __builtin_FILE(),
               int line = __builtin_LINE())
      : ScopeBase("ProgramScope", file, line) {
    glUseProgram(program);
  }

  ~ProgramScope() { glUseProgram(0); }
};

// Binds a vertex array for the duration of the scope.
class VertexArrayScope final : ScopeBase {
 public:
  VertexArrayScope(GLuint vao, const char* file = __builtin_FILE(),
                   int line = __builtin_LINE())
      : ScopeBase("VertexArrayScope", file, line) {
    glBindVertexArray(vao);
  }

  ~VertexArrayScope() { glBindVertexArray(0); }
};

// Enables a GL capability for the duration of the scope.
class EnableScope final : ScopeBase {
 public:
  EnableScope(GLenum cap, const char* file = __builtin_FILE(),
              int line = __builtin_LINE())
      : ScopeBase("EnableScope", file, line), cap_(cap) {
    glEnable(cap);
  }

  ~EnableScope() { glDisable(cap_); }

 private:
  GLenum cap_;
};

// Disables a GL capability for the duration of the scope.
class DisableScope final : ScopeBase {
 public:
  DisableScope(GLenum cap, const char* file = __builtin_FILE(),
               int line = __builtin_LINE())
      : ScopeBase("DisableScope", file, line), cap_(cap) {
    glDisable(cap);
  }

  ~DisableScope() { glEnable(cap_); }

 private:
  GLenum cap_;
};

}  // namespace GL
}  // namespace G

#endif  // _GAME_GL_H
