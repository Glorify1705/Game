#pragma once
#ifndef _GAME_GL_HEADERS_H
#define _GAME_GL_HEADERS_H

// Selects the OpenGL headers for the target platform. Desktop uses the glad
// loader (OpenGL 4.1 core); the web target uses Emscripten's OpenGL ES 3.0
// headers directly, which map onto WebGL2 — glad's loader only covers
// desktop GL and ES 2.0, so it cannot provide the ES 3.0 entry points.
#ifdef GAME_WEB
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#else
#include "libraries/glad.h"
#endif

#endif  // _GAME_GL_HEADERS_H
