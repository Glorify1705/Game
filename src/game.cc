#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_mixer.h"
#include "allocators.h"
#include "assets.h"
#include "circular_buffer.h"
#include "clock.h"
#include "console.h"
#include "input.h"
#include "libraries/glad.h"
#include "libraries/imgui.h"
#include "libraries/imgui_impl_opengl3.h"
#include "libraries/imgui_impl_sdl2.h"
#include "logging.h"
#include "lua.h"
#include "mat.h"
#include "math.h"
#include "physics.h"
#include "renderer.h"
#include "shaders.h"
#include "sound.h"
#include "stats.h"
#include "strings.h"
#include "vec.h"
#include "zip.h"

namespace G {

struct GameParams {
  int screen_width = 1440;
  int screen_height = 1024;
};

void SdlCrash(const char* message) {
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
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unrecoverable error", message,
                           /*window=*/nullptr);
  _INTERNAL_GAME_TRAP();
}

void LogToSDL(LogLevel level, const char* message) {
  switch (level) {
    case LOG_LEVEL_FATAL:
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
    case LOG_LEVEL_INFO:
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
      break;
  }
}

void GLAPIENTRY OpenglMessageCallback(GLenum /*source*/, GLenum type,
                                      GLuint /*id*/, GLenum severity,
                                      GLsizei /*length*/, const GLchar* message,
                                      const void* /*user_param*/) {
  if (type == GL_DEBUG_TYPE_ERROR) {
    LOG("GL ERROR ", " type = ", type, " severity = ", severity,
        " message = ", message);
  }
}

static BumpAllocator* GlobalBumpAllocator() {
  static BumpAllocator allocator(1 * 1024 * 1024 * 1024ULL);  // 1 Gigabytes.
  return &allocator;
}

const uint8_t* ReadAssets(const std::vector<const char*>& arguments) {
  TIMER();
  const char* output_file = arguments.empty() ? "assets.zip" : arguments[0];
  constexpr char kAssetFileName[] = "assets.bin";
  zip_error_t zip_error;
  int zip_error_code;
  zip_t* zip_file = zip_open(output_file, ZIP_RDONLY, &zip_error_code);
  if (zip_file == nullptr) {
    zip_error_init_with_code(&zip_error, zip_error_code);
    DIE("Failed to open ", output_file,
        " as a zip file: ", zip_error_strerror(&zip_error));
  }
  const int64_t num_entries = zip_get_num_entries(zip_file, ZIP_FL_UNCHANGED);
  if (num_entries == 0) LOG("Zip file has no entries");
  CHECK(num_entries == 1, "Expected one file");
  const char* filename = zip_get_name(zip_file, 0, ZIP_FL_ENC_RAW);
  CHECK(!std::strcmp(filename, kAssetFileName), "Expected a file named ",
        kAssetFileName, " found ", filename);
  for (int i = 0; i < num_entries; ++i) {
    LOG("Found file ", zip_get_name(zip_file, i, ZIP_FL_ENC_RAW),
        " in zip file");
  }
  zip_stat_t stat;
  if (zip_stat(zip_file, kAssetFileName, ZIP_FL_ENC_UTF_8, &stat) == -1) {
    DIE("Failed to open ", output_file,
        " as a zip file: ", zip_strerror(zip_file));
  }
  auto* assets_file = zip_fopen(zip_file, kAssetFileName, /*flags=*/0);
  if (assets_file == nullptr) {
    DIE("Failed to decompress ", output_file, ": ", zip_strerror(zip_file));
  }
  auto* buffer = GlobalBumpAllocator()->AllocArray<uint8_t>(stat.size);
  if (zip_fread(assets_file, buffer, stat.size) == -1) {
    DIE("Failed to read decompressed file");
  }
  LOG("Read assets file (", stat.size, " bytes)");
  return buffer;
}

struct EngineModules {
 private:
  const uint8_t* assets_buf_ = nullptr;

 public:
  Assets assets;
  BatchRenderer batch_renderer;
  Keyboard keyboard;
  Mouse mouse;
  Controllers controllers;
  Sound sound;
  Renderer sprite_sheet_renderer;
  Lua lua;
  Physics physics;

  EngineModules(const std::vector<const char*> arguments,
                const GameParams& params)
      : assets_buf_(ReadAssets(arguments)),
        assets(assets_buf_),
        batch_renderer(IVec2(params.screen_width, params.screen_height)),
        sound(&assets),
        sprite_sheet_renderer(assets, &batch_renderer),
        lua("main.lua", &assets),
        physics(FVec(params.screen_width, params.screen_height),
                Physics::kPixelsPerMeter) {
    lua.Register(&sprite_sheet_renderer);
    lua.Register(&keyboard);
    lua.Register(&mouse);
    lua.Register(&controllers);
    lua.Register(&sound);
    lua.Register(&physics);
  }

  void StartFrame() {
    batch_renderer.Clear();
    mouse.InitForFrame();
    keyboard.InitForFrame();
    controllers.InitForFrame();
  }

  void HandleEvent(const SDL_Event& event) {
    if (event.type == SDL_WINDOWEVENT) {
      if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        IVec2 new_viewport(event.window.data1, event.window.data2);
        batch_renderer.SetViewport(new_viewport);
      }
    }
    keyboard.PushEvent(event);
    mouse.PushEvent(event);
    controllers.PushEvent(event);
  }

  void Render() {
    sprite_sheet_renderer.BeginFrame();
    lua.Draw();
    sprite_sheet_renderer.FlushFrame();
    batch_renderer.Render();
  }

  ~EngineModules() { delete[] assets_buf_; }
};

void InitializeSDL() {
  SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
  CHECK(SDL_Init(SDL_INIT_EVERYTHING) == 0,
        "Could not initialize SDL: ", SDL_GetError());
  CHECK(Mix_OpenAudio(44100, MIX_INIT_OGG, 2, 2048) == 0,
        "Could not initialize audio: ", Mix_GetError());
  SetLogSink(LogToSDL);
  SetCrashHandler(SdlCrash);
  CHECK(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0,
        "Could not initialize SDL joysticks: ", SDL_GetError());
  SDL_JoystickEventState(SDL_ENABLE);
  SDL_GameControllerEventState(SDL_ENABLE);
  SDL_ShowCursor(false);
}

void PrintDependencyVersions() {
  SDL_version compiled, linked;
  SDL_VERSION(&compiled);
  SDL_GetVersion(&linked);
  LOG("Using Compiled SDL ",
      SDL_VERSIONNUM(compiled.major, compiled.minor, compiled.patch));
  LOG("Using Linked SDL ",
      SDL_VERSIONNUM(linked.major, linked.minor, linked.patch));
  LOG("Using OpenGL Version: ", glGetString(GL_VERSION));
  LOG("Using GLAD Version: ", GLVersion.major, ".", GLVersion.minor);
  LOG("Using ", LUA_VERSION);
  const SDL_version& mix_version = *Mix_Linked_Version();
  LOG("Using Linked SDL Mixer ",
      SDL_VERSIONNUM(mix_version.major, mix_version.minor, mix_version.patch));
  LOG("Using Compiled Mixer ", SDL_MIXER_COMPILEDVERSION);
  LOG("Using Flatbuffers ", FLATBUFFERS_VERSION_MAJOR, ".",
      FLATBUFFERS_VERSION_MINOR, ".", FLATBUFFERS_VERSION_REVISION);
  LOG("Using Box2D ", b2_version.major, ".", b2_version.minor, ".",
      b2_version.revision);
  LOG("Using Dear ImGUI Version: ", IMGUI_VERSION);
}

SDL_Window* CreateWindow(const GameParams& params) {
  LOG("Initializing basic attributes");
  CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0,
        "Could not set major version", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) == 0,
        "Could not set minor version", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE) == 0,
        "Could not set Core profile", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) == 0,
        "Could not set double buffering version", SDL_GetError());
  auto* window =
      SDL_CreateWindow("Game", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       params.screen_width, params.screen_height,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  CHECK(window != nullptr, "Could not initialize window: ", SDL_GetError());
  return window;
}

SDL_GLContext CreateOpenglContext(SDL_Window* window) {
  LOG("Creating SDL context");
  auto context = SDL_GL_CreateContext(window);
  CHECK(context != nullptr, "Could not load OpenGL context: ", SDL_GetError());
  CHECK(gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress),
        "Could not load GLAD");
  CHECK(SDL_GL_SetSwapInterval(1) == 0, "Could not set up VSync: ",
        SDL_GetError());  // Sync update with monitor vertical.
  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(OpenglMessageCallback, /*userParam=*/nullptr);
  return context;
}

class DebugUi {
 public:
  DebugUi(SDL_Window* window, SDL_GLContext context, Stats* frame_stats,
          EngineModules* modules)
      : stats_(frame_stats), modules_(modules) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 130");
  }

  ~DebugUi() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
  }

  void Toggle() { show_ = !show_; }

  void Render() {
    if (!show_) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    RenderTopWidget();
    ImGui::Begin("Debug Information");
    if (ImGui::TreeNode("Console")) {
      ImGui::BeginChild("Scrolling");
      DebugConsole::instance().ForAllLines([this](std::string_view line) {
        ImGui::TextUnformatted(line.data(), line.end());
      });
      ImGui::EndChild();
      ImGui::TreePop();
    }
    if (ImGui::Button("Copy")) CopyToClipboard();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  void ProcessEvent(const SDL_Event& event) {
    ImGui_ImplSDL2_ProcessEvent(&event);
  }

 private:
  void CopyToClipboard() {
    size_t buffer_sz = 0, pos = 0;
    auto& console = DebugConsole::instance();
    console.ForAllLines([this, &buffer_sz](std::string_view line) {
      buffer_sz += line.size() + 1;
    });
    auto* buffer = new char[buffer_sz + 1];
    console.ForAllLines([this, &buffer, &pos](std::string_view line) {
      std::memcpy(&buffer[pos], line.data(), line.size());
      pos += line.size();
      buffer[pos++] = '\n';
    });
    buffer[pos] = '\0';
    SDL_SetClipboardText(buffer);
    delete[] buffer;
  }
  void RenderTopWidget() {
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;
    if (frame_stats_location_ >= 0) {
      constexpr float kPad = 10.0f;
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImVec2 work_pos = viewport->WorkPos;
      ImVec2 work_size = viewport->WorkSize;
      ImVec2 window_pos, window_pos_pivot;
      window_pos.x = (frame_stats_location_ & 1)
                         ? (work_pos.x + work_size.x - kPad)
                         : (work_pos.x + kPad);
      window_pos.y = (frame_stats_location_ & 2)
                         ? (work_pos.y + work_size.y - kPad)
                         : (work_pos.y + kPad);
      window_pos_pivot.x = (frame_stats_location_ & 1) ? 1.0f : 0.0f;
      window_pos_pivot.y = (frame_stats_location_ & 2) ? 1.0f : 0.0f;
      ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
      window_flags |= ImGuiWindowFlags_NoMove;
    }
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("Frame Stats", nullptr, window_flags);
    ImGui::TextUnformatted(StrCat("Frame Stats: ", *stats_).c_str());
    if (ImGui::BeginTable("Watchers", 2)) {
      DebugConsole::instance().ForAllWatchers(
          [this](std::string_view key, std::string_view value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(key.data(), key.end());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.data(), value.end());
          });
      ImGui::EndTable();
    }
    ImGui::End();
  }

  Stats* stats_;
  EngineModules* modules_;
  char expressions_[1 << 20];
  LookupTable<const char*> expression_table_;
  bool show_ = false;
  int frame_stats_location_ = 1;
};

class Game {
 public:
  Game(int argc, const char* argv[]) : arguments_(argv + 1, argv + argc) {
    TIMER("Setup");
    // Initialize the debug console.
    DebugConsole::instance();
    InitializeSDL();
    window_ = CreateWindow(params_);
    context_ = CreateOpenglContext(window_);
    PrintDependencyVersions();
  }

  ~Game() {
    e_.reset();
    debug_ui_.reset();
    if (SDL_WasInit(SDL_INIT_HAPTIC) != 0) {
      SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    }
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
  }

  void Init() {
    TIMER("Game Initialization");
    e_ = std::make_unique<EngineModules>(arguments_, params_);
    debug_ui_ = std::make_unique<DebugUi>(window_, context_, &stats_, e_.get());
    e_->lua.Register(&DebugConsole::instance());
    e_->lua.Init();
  }

  void Run() {
    double last_frame = NowInSeconds();
    constexpr double kStep = TimeStepInSeconds();
    double t = 0, accum = 0;
    for (;;) {
      if (e_->lua.Stopped()) return;
      const double now = NowInSeconds();
      const double frame_time = now - last_frame;
      last_frame = now;
      accum += frame_time;
      if (accum < kStep) {
        SDL_Delay(1);
        continue;
      }
      const auto frame_start = NowInSeconds();
      e_->StartFrame();
      for (SDL_Event event; SDL_PollEvent(&event);) {
        if (event.type == SDL_QUIT) {
          return;
        }
        ImGui_ImplSDL2_ProcessEvent(&event);
        e_->HandleEvent(event);
        if (event.type == SDL_KEYDOWN) {
          if (e_->keyboard.IsDown(SDL_SCANCODE_TAB)) {
            e_->batch_renderer.ToggleDebugRender();
            debug_ui_->Toggle();
          }
        }
      }
      while (accum >= kStep) {
        Update(t, kStep);
        t += kStep;
        accum -= kStep;
      }
      Render();
      stats_.AddSample(NowInSeconds() - frame_start);
    }
  }

  // Update state given current time t and frame delta dt, both in ms.
  void Update(double t, double dt) {
    WATCH_VAR(t);
    WATCH_VAR(dt);
    e_->physics.Update(dt);
    e_->lua.Update(t, dt);
    WATCH_EXPR("Mouse Position ", e_->mouse.GetPosition());
  }

  void Render() {
    e_->Render();
    debug_ui_->Render();
    SDL_GL_SwapWindow(window_);
  }

 private:
  std::vector<const char*> arguments_;
  GameParams params_;
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_;
  std::unique_ptr<DebugUi> debug_ui_;
  std::unique_ptr<EngineModules> e_;
  Stats stats_;
};

void GameMain(int argc, const char* argv[]) {
  // Ensure we don't overflow the stack in any module by allocating Game on the
  // heap.
  auto g = std::make_unique<Game>(argc, argv);
  g->Init();
  g->Run();
}

}  // namespace G

int main(int argc, const char* argv[]) {
  G::GameMain(argc, argv);
  return 0;
}