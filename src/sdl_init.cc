#include "sdl_init.h"

#include <SDL3/SDL.h>

#include <cstdlib>

#include "clock.h"
#include "config.h"
#include "libraries/glad.h"
#include "logging.h"
#include "version.h"

// These headers are only needed for PrintSystemInformation.
#include "box2d/box2d.h"
#include "libraries/sqlite3.h"
#include "lua.h"
#include "physfs.h"

namespace G {

namespace {

#ifndef _INTERNAL_GAME_TRAP
#if __has_builtin(__builtin_debugtrap)
#define _INTERNAL_GAME_TRAP __builtin_debugtrap
#elif __has_builtin(__builtin_trap)
#define _INTERNAL_GAME_TRAP __builtin_trap
#elif _MSC_VER
#define _INTERNAL_GAME_TRAP __debugbreak
#else
#define _INTERNAL_GAME_TRAP std::abort
#endif
#endif

void SdlCrash(const char* message) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unrecoverable error", message,
                           /*window=*/nullptr);
  _INTERNAL_GAME_TRAP();
}

void LogToSDL(LogLevel level, const char* message) {
  switch (level) {
    case LogLevel::kFatal:
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kError:
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kWarn:
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kInfo:
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kDebug:
      SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LogLevel::kTrace:
      SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
  }
}

void GLAPIENTRY OpenglMessageCallback(GLenum /*source*/, GLenum type,
                                      GLuint /*id*/, GLenum severity,
                                      GLsizei /*length*/, const GLchar* message,
                                      const void* user_param) {
  auto l = static_cast<const OpenGLSourceLine*>(user_param);
  if (type == GL_DEBUG_TYPE_ERROR) {
    Log(l->file, l->line, "GL ERROR ", " type = ", type,
        " severity = ", severity, " message = ", message,
        ". Context = ", l->buffer);
  }
}

SDL_Window* CreateWindow(const GameConfig& config) {
  TIMER("Initializing basic attributes");
  CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4),
        "Could not set major version", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6),
        "Could not set minor version", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE),
        "Could not set Core profile", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1),
        "Could not set double buffering version", SDL_GetError());
#ifdef GAME_WITH_ASSERTS
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
  SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
  if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
  if (config.borderless) flags |= SDL_WINDOW_BORDERLESS;
  if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

  SDL_Window* window = nullptr;

  if (config.centered && !config.fullscreen) {
    LOG("Creating centered window");

    SDL_SetHint(SDL_HINT_X11_WINDOW_TYPE, "_NET_WM_WINDOW_TYPE_DIALOG");

    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* display_mode = SDL_GetCurrentDisplayMode(display);
    CHECK(display_mode != nullptr, "Could not get display mode ",
          SDL_GetError());

    const int screen_width = display_mode->w;
    const int screen_height = display_mode->h;

    LOG("Display mode: width = ", display_mode->w,
        " height = ", display_mode->h,
        " refresh rate = ", display_mode->refresh_rate);

    const int window_x = (screen_width - config.window_width) / 2;
    const int window_y = (screen_height - config.window_height) / 2;

    window = SDL_CreateWindow(config.window_title, config.window_width,
                              config.window_height, flags);
    if (window) SDL_SetWindowPosition(window, window_x, window_y);
  } else {
    window = SDL_CreateWindow(config.window_title, config.window_width,
                              config.window_height, flags);
    CHECK(window != nullptr, "Could not initialize window: ", SDL_GetError());
  }

  CHECK(window != nullptr, "Could not create ", config.window_width, "x",
        config.window_height, " window: ", SDL_GetError());
  return window;
}

SDL_GLContext CreateOpenglContext(const GameConfig& config,
                                  SDL_Window* window) {
  LOG("Creating SDL context");
  CHECK(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1),
        " failed to set multi sample buffers: ", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config.msaa_samples),
        " failed to set multi samples: ", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1),
        " failed to set accelerated visual: ", SDL_GetError());
  auto context = SDL_GL_CreateContext(window);
  CHECK(context != nullptr, "Could not load OpenGL context: ", SDL_GetError());
  CHECK(gladLoadGLLoader([](const char* name) -> void* {
          return (void*)SDL_GL_GetProcAddress(name);
        }),
        "Could not load GLAD");
  if (config.vsync_mode != 0) {
    CHECK(SDL_GL_SetSwapInterval(config.vsync_mode),
          "Could not set up VSync to mode ", config.vsync_mode, ": ",
          SDL_GetError());
  }
  const bool supports_opengl_debug = GLAD_GL_VERSION_4_3 && GLAD_GL_KHR_debug;
  if (supports_opengl_debug && config.enable_opengl_debug) {
    LOG("OpenGL Debug Callback Support is enabled!");
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(OpenglMessageCallback,
                           /*userParam=*/GetOpenGLSourceLine());
  } else {
    LOG("OpenGL Debug Callback Support is disabled");
  }
  return context;
}

}  // namespace

void InitializeLogging() {
#ifdef GAME_WITH_ASSERTS
  SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
#else
  SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
#endif
  SetLogSink(LogToSDL);
  SetCrashHandler(SdlCrash);
}

SdlContext InitializeSdl(const GameConfig& config,
                         SDL_AudioStreamCallback audio_cb,
                         void* audio_cb_userdata) {
  SdlContext ctx;
  SDL_SetAppMetadata(config.app_name[0] != '\0' ? config.app_name : "game",
                     GAME_VERSION_STR,
                     config.org_name[0] != '\0' ? config.org_name : nullptr);
  // Use EGL instead of GLX. EGL is the modern, platform-agnostic path and
  // works on X11, Wayland, and future targets. GLX is X11-only legacy and
  // breaks under ASan.
  SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");
  CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS),
        "Could not initialize SDL: ", SDL_GetError());
  if (config.enable_joystick) {
    CHECK(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD),
          "Could not initialize SDL joysticks: ", SDL_GetError());
    SDL_SetJoystickEventsEnabled(true);
    SDL_SetGamepadEventsEnabled(true);
  }
  SDL_HideCursor();

  ctx.window = CreateWindow(config);
  ctx.gl_context = CreateOpenglContext(config, ctx.window);

  SDL_AudioSpec spec = {SDL_AUDIO_F32, kAudioChannels, kAudioSampleRate};
  ctx.audio_stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, audio_cb_userdata);
  CHECK(ctx.audio_stream != nullptr,
        "Could not open audio stream: ", SDL_GetError());
  ctx.audio_device = SDL_GetAudioStreamDevice(ctx.audio_stream);
  return ctx;
}

void ShutdownSdl(SdlContext* ctx) {
  // Destroy the audio stream first so the callback thread stops before
  // subsystems it may reference go away.
  if (ctx->audio_stream != nullptr) {
    SDL_DestroyAudioStream(ctx->audio_stream);
    ctx->audio_stream = nullptr;
  }
  if (SDL_WasInit(SDL_INIT_HAPTIC) != 0) {
    SDL_QuitSubSystem(SDL_INIT_HAPTIC);
  }
  if (SDL_WasInit(SDL_INIT_JOYSTICK)) {
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
  }
  if (SDL_WasInit(SDL_INIT_GAMEPAD)) {
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  }
  if (ctx->gl_context != nullptr) {
    SDL_GL_DestroyContext(ctx->gl_context);
    ctx->gl_context = nullptr;
  }
  if (ctx->window != nullptr) {
    SDL_DestroyWindow(ctx->window);
    ctx->window = nullptr;
  }
  SDL_Quit();
}

void PrintSystemInformation() {
  LOG("Compiled with ", COMPILER, " version ", COMPILER_VERSION);
  LOG("Using SDL ", SDL_GetVersion());
  LOG("Using OpenGL Version: ", glGetString(GL_VERSION));
  LOG("Using GLAD Version: ", GLVersion.major, ".", GLVersion.minor);
  LOG("Using ", LUA_VERSION);
  LOG("Using Box2D ", b2_version.major, ".", b2_version.minor, ".",
      b2_version.revision);
  PHYSFS_Version physfs_version;
  PHYSFS_getLinkedVersion(&physfs_version);
  LOG("Using PhysFS ", physfs_version.major, ".", physfs_version.minor, ".",
      physfs_version.patch);
  LOG("Using SQLite Version ", SQLITE_VERSION);
  LOG("Running on platform: ", SDL_GetPlatform());
  LOG("Have ", SDL_GetNumLogicalCPUCores(), " logical cores");
}

IVec2 GetWindowViewport(SDL_Window* window) {
  IVec2 result;
  SDL_GetWindowSizeInPixels(window, &result.x, &result.y);
  return result;
}

}  // namespace G
