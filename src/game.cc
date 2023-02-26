#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "SDL.h"
#include "SDL_mixer.h"
#include "allocators.h"
#include "assets.h"
#include "circular_buffer.h"
#include "clock.h"
#include "debug_ui.h"
#include "glad.h"
#include "input.h"
#include "logging.h"
#include "lua.h"
#include "mat.h"
#include "math.h"
#include "packer.h"
#include "physics.h"
#include "qoi.h"
#include "renderer.h"
#include "shaders.h"
#include "sound.h"
#include "stats.h"
#include "stb_truetype.h"
#include "strings.h"
#include "vec.h"

struct GameParams {
  int screen_width = 1440;
  int screen_height = 1024;
};

void GLAPIENTRY OpenglMessageCallback(GLenum /*source*/, GLenum type,
                                      GLuint /*id*/, GLenum severity,
                                      GLsizei /*length*/, const GLchar* message,
                                      const void* /*user_param*/) {
  if (type == GL_DEBUG_TYPE_ERROR) {
    LOG("GL ERROR ", " type = ", type, " severity = ", severity,
        " message = ", message);
  }
}

const uint8_t* ReadAssets(const std::vector<const char*>& arguments) {
  const char* file = arguments.empty() ? "assets.fbs" : arguments[0];
  FILE* f = std::fopen(file, "rb");
  CHECK(f != nullptr, "Could not read ", file);
  std::fseek(f, 0, SEEK_END);
  const size_t fsize = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  auto* buffer = new uint8_t[fsize + 1];
  CHECK(std::fread(buffer, fsize, 1, f) == 1, " failed to read ", file);
  fclose(f);
  LOG("Read assets file (", fsize, " bytes)");
  return buffer;
}

struct EngineModules {
 private:
  const uint8_t* assets_buf_ = nullptr;

 public:
  Assets assets;
  QuadRenderer quad_renderer;
  DebugConsole debug_console;
  Keyboard keyboard;
  Mouse mouse;
  Sound sound;
  SpriteSheetRenderer sprite_sheet_renderer;
  Lua lua;
  Physics physics;

  EngineModules(const std::vector<const char*> arguments,
                const GameParams& params)
      : assets_buf_(ReadAssets(arguments)),
        assets(assets_buf_),
        quad_renderer(IVec2(params.screen_width, params.screen_height)),
        debug_console(&quad_renderer),
        sound(&assets),
        sprite_sheet_renderer("sheet.xml", &assets, &quad_renderer),
        lua("main.lua", &assets) {
    lua.Register(&sprite_sheet_renderer);
    lua.Register(&keyboard);
    lua.Register(&mouse);
    lua.Register(&sound);
    lua.Register(&physics);
  }

  ~EngineModules() { delete[] assets_buf_; }
};

SDL_Window* CreateWindow(const GameParams& params) {
  SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
  CHECK(SDL_Init(SDL_INIT_EVERYTHING) == 0,
        "Could not initialize SDL: ", SDL_GetError());
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
  SDL_GL_SetSwapInterval(1);  // Sync update with monitor vertical.
  auto* window =
      SDL_CreateWindow("Game", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       params.screen_width, params.screen_height,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  CHECK(window != nullptr, "Could not initialize window: ", SDL_GetError());
  return window;
}

SDL_GLContext CreateOpenglContext(SDL_Window* window) {
  auto context = SDL_GL_CreateContext(window);
  CHECK(context != nullptr, "Could not load OpenGL context: ", SDL_GetError());
  CHECK(gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress),
        "Could not load GLAD");
  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(OpenglMessageCallback, /*userParam=*/nullptr);
  return context;
}

class Game {
 public:
  Game(int argc, const char* argv[])
      : arguments_(argv + 1, argv + argc),
        window_(CreateWindow(params_)),
        context_(CreateOpenglContext(window_)),
        e_(arguments_, params_) {}

  ~Game() {
    LOG("Initiating shutdown");
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
  }

  void Init() {
    CHECK(Mix_OpenAudio(44100, MIX_INIT_OGG, 2, 2048) == 0,
          "Could not initialize audio: ", Mix_GetError());
    SDL_ShowCursor(false);
    e_.lua.Init();
  }

  void Run() {
    double last_frame = NowInMillis();
    constexpr double kStep = TimeStepInMillis();
    constexpr double kStepsPerFrame = 1.0 / TimeStepInMillis();
    double t = 0, accum = 0;
    for (;;) {
      const double now = NowInMillis();
      const double frame_time = now - last_frame;
      last_frame = now;
      accum += frame_time;
      if (accum < kStepsPerFrame) {
        SDL_Delay(1);
        continue;
      }
      const auto frame_start = NowInMillis();
      StartFrame();
      for (SDL_Event event; SDL_PollEvent(&event);) {
        if (event.type == SDL_WINDOWEVENT) {
          if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            params_.screen_width = event.window.data1;
            params_.screen_height = event.window.data2;
            e_.quad_renderer.SetViewport(
                IVec2(params_.screen_width, params_.screen_height));
          }
        }
        if (event.type == SDL_QUIT) {
          return;
        }
        e_.keyboard.PushEvent(event);
        e_.mouse.PushEvent(event);
        if (event.type == SDL_KEYDOWN) {
          if (e_.keyboard.IsDown(SDLK_TAB)) e_.debug_console.Toggle();
          if (e_.keyboard.IsDown(SDLK_q)) return;
        }
      }
      while (accum >= kStepsPerFrame) {
        Update(t, kStep);
        t += kStepsPerFrame;
        accum -= kStepsPerFrame;
      }
      Render();
      stats_.AddSample(NowInMillis() - frame_start);
    }
  }

  void StartFrame() {
    e_.quad_renderer.Clear();
    e_.debug_console.Clear();
    e_.mouse.InitForFrame();
    e_.keyboard.InitForFrame();
  }

  // Update state given current time t and frame delta dt, both in ms.
  void Update(double t, double dt) {
    e_.lua.Update(t, dt);
    e_.debug_console.PushText(
        StrCat("Mouse position ", Mouse::GetPosition(), "\n"),
        FVec2(params_.screen_width - 300, 0));
    e_.debug_console.PushText(StrCat("Frame Stats: ", stats_),
                              FVec2(0, params_.screen_height - 20));
  }

  void ClearWindow() {
    glViewport(0, 0, params_.screen_width, params_.screen_height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  void Render() {
    ClearWindow();
    e_.sprite_sheet_renderer.BeginFrame();
    e_.lua.Render();
    e_.sprite_sheet_renderer.FlushFrame();
    e_.debug_console.Render();
    e_.quad_renderer.Render();
    SDL_GL_SwapWindow(window_);
  }

 private:
  std::vector<const char*> arguments_;
  GameParams params_;
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_;
  EngineModules e_;
  Stats stats_;
};

void GameMain(int argc, const char* argv[]) {
  Game g(argc, argv);
  g.Init();
  g.Run();
}
int main(int argc, const char* argv[]) {
  if (argc > 1 && !strcmp(argv[1], "packer")) {
    CHECK(argc > 3, "Usage: <output file> <files to pack>");
    PackerMain(argv[2], std::vector(argv + 3, argv + argc));
  } else {
    GameMain(argc, argv);
  }
  return 0;
}
