#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_mixer.h"
#include "allocators.h"
#include "assets.h"
#include "circular_buffer.h"
#include "clock.h"
#include "config.h"
#include "console.h"
#include "filesystem.h"
#include "image.h"
#include "input.h"
#include "libraries/glad.h"
#include "libraries/imgui.h"
#include "libraries/imgui_impl_opengl3.h"
#include "libraries/imgui_impl_sdl2.h"
#include "logging.h"
#include "lua.h"
#include "mat.h"
#include "math.h"
#include "packer.h"
#include "physics.h"
#include "renderer.h"
#include "shaders.h"
#include "sound.h"
#include "stats.h"
#include "strings.h"
#include "units.h"
#include "vec.h"
#include "zip.h"

namespace G {

constexpr size_t kEngineMemory = Gigabytes(4);
using EngineAllocator = StaticAllocator<kEngineMemory>;

static EngineAllocator* GlobalEngineAllocator() {
  static auto* allocator = new EngineAllocator;
  return allocator;
}

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
                                      const void* user_param) {
  auto l = static_cast<const OpenGLSourceLine*>(user_param);
  if (type == GL_DEBUG_TYPE_ERROR) {
    Log(l->file, l->line, "GL ERROR ", " type = ", type,
        " severity = ", severity, " message = ", message);
    _INTERNAL_GAME_TRAP();
  }
}

Assets* GetAssets(const std::vector<const char*>& arguments,
                  Allocator* allocator) {
  if (arguments.empty()) {
    LOG("Reading assets from assets.zip since no file was provided");
    return ReadAssets("assets.zip", allocator);
  }
  if (Extension(arguments[0]) == "zip") {
    LOG("Reading assets from ", arguments[0]);
    return ReadAssets(arguments[0], allocator);
  }
  LOG("Packing all files in directory ", arguments[0]);
  return PackFiles(arguments[0], allocator);
}

struct EngineModules {
  EngineModules(Assets* assets, const GameConfig& config,
                SDL_Window* sdl_window, Allocator* allocator)
      : config(&config),
        filesystem(allocator),
        window(sdl_window),
        shaders(*assets, allocator),
        batch_renderer(IVec2(config.window_width, config.window_height),
                       &shaders, allocator),
        keyboard(allocator),
        controllers(*assets, allocator),
        sound(*assets, allocator),
        renderer(*assets, &batch_renderer, allocator),
        lua(assets, SystemAllocator::Instance()),
        physics(FVec(config.window_width, config.window_height),
                Physics::kPixelsPerMeter, allocator),
        frame_allocator(allocator, Megabytes(128)) {
    filesystem.Initialize(config);
  }

  void InitializeLua() {
    lua.LoadLibraries();
    lua.Register(&shaders);
    lua.Register(&batch_renderer);
    lua.Register(&renderer);
    lua.Register(window);
    lua.Register(&keyboard);
    lua.Register(&mouse);
    lua.Register(&controllers);
    lua.Register(&shaders);
    lua.Register(&sound);
    lua.Register(&filesystem);
    lua.Register(&physics);
    lua.Register(&DebugConsole::Instance());
    lua.LoadScripts();
    lua.LoadMain();
  }

  void StartFrame() {
    frame_allocator.Reset();
    batch_renderer.Clear();
    mouse.InitForFrame();
    keyboard.InitForFrame();
    controllers.InitForFrame();
  }

  void ForwardEventToLua(const SDL_Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
      if (!config->enable_debug_ui || !io.WantCaptureKeyboard) {
        if (event.type == SDL_KEYDOWN) {
          lua.HandleKeypressed(event.key.keysym.scancode);
        }
        if (event.type == SDL_KEYUP) {
          lua.HandleKeyreleased(event.key.keysym.scancode);
        }
      }
    }
    if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
         event.type == SDL_MOUSEMOTION) &&
        (!config->enable_debug_ui || !io.WantCaptureKeyboard)) {
      if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          lua.HandleMousePressed(0);
        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
          lua.HandleMousePressed(1);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          lua.HandleMousePressed(2);
        }
      }
      if (event.type == SDL_MOUSEBUTTONUP) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          lua.HandleMouseReleased(0);
        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
          lua.HandleMouseReleased(1);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          lua.HandleMouseReleased(2);
        }
      }
      if (event.type == SDL_MOUSEMOTION) {
        lua.HandleMouseMoved(FVec2(event.motion.x, event.motion.y),
                             FVec2(event.motion.xrel, event.motion.yrel));
      }
    }
  }

  void HandleEvent(const SDL_Event& event) {
    if (event.type == SDL_WINDOWEVENT) {
      if (config->resizable && event.window.event == SDL_WINDOWEVENT_RESIZED) {
        IVec2 new_viewport(event.window.data1, event.window.data2);
        batch_renderer.SetViewport(new_viewport);
        physics.UpdateDimensions(new_viewport);
      }
    }
    ImGuiIO& io = ImGui::GetIO();
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
      if (!config->enable_debug_ui || !io.WantCaptureKeyboard) {
        keyboard.PushEvent(event);
      }
    }
    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
        event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL) {
      if (!config->enable_debug_ui || !io.WantCaptureMouse) {
        mouse.PushEvent(event);
      }
    }
    controllers.PushEvent(event);
    ForwardEventToLua(event);
  }

  void Render() {
    renderer.BeginFrame();
    lua.Draw();
    renderer.FlushFrame();
    batch_renderer.Render(&frame_allocator);
  }

  const GameConfig* config;
  Filesystem filesystem;
  SDL_Window* window;
  Shaders shaders;
  BatchRenderer batch_renderer;
  Keyboard keyboard;
  Mouse mouse;
  Controllers controllers;
  Sound sound;
  Renderer renderer;
  Lua lua;
  Physics physics;
  ArenaAllocator frame_allocator;
};

void InitializeLogging() {
  SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
  SetLogSink(LogToSDL);
  SetCrashHandler(SdlCrash);
}

void InitializeSDL(const GameConfig& config) {
  CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER |
                 SDL_INIT_EVENTS) == 0,
        "Could not initialize SDL: ", SDL_GetError());
  CHECK(Mix_OpenAudio(44100, MIX_INIT_OGG, 2, 2048) == 0,
        "Could not initialize audio: ", Mix_GetError());
  if (config.enable_joystick) {
    CHECK(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0,
          "Could not initialize SDL joysticks: ", SDL_GetError());
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);
  }
  SDL_ShowCursor(false);
}

void PrintSystemInformation() {
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
  PHYSFS_Version physfs_version;
  PHYSFS_getLinkedVersion(&physfs_version);
  LOG("Using PhysFS ", physfs_version.major, ".", physfs_version.minor, ".",
      physfs_version.patch);
  LOG("Running on platform: ", SDL_GetPlatform());
  LOG("Have ", SDL_GetCPUCount(), " logical cores");
}

SDL_Window* CreateWindow(const GameConfig& config) {
  TIMER("Initializing basic attributes");
  CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0,
        "Could not set major version", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) == 0,
        "Could not set minor version", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE) == 0,
        "Could not set Core profile", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) == 0,
        "Could not set double buffering version", SDL_GetError());
  uint32_t flags = SDL_WINDOW_OPENGL;
  if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
  if (config.borderless) flags |= SDL_WINDOW_BORDERLESS;
  auto* window = SDL_CreateWindow(config.window_title, SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, config.window_width,
                                  config.window_height, flags);
  CHECK(window != nullptr, "Could not initialize window: ", SDL_GetError());
  return window;
}

SDL_GLContext CreateOpenglContext(const GameConfig& config,
                                  SDL_Window* window) {
  LOG("Creating SDL context");
  CHECK(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1) == 0,
        " failed to set multi sample buffers: ", SDL_GetError());
  CHECK(
      SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config.msaa_samples) == 0,
      " failed to set multi samples: ", SDL_GetError());
  CHECK(SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1) == 0,
        " failed to set accelerated visual: ", SDL_GetError());
  auto context = SDL_GL_CreateContext(window);
  CHECK(context != nullptr, "Could not load OpenGL context: ", SDL_GetError());
  CHECK(gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress),
        "Could not load GLAD");
  if (config.vsync_mode != 0) {
    CHECK(SDL_GL_SetSwapInterval(config.vsync_mode) == 0,
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

class DebugUi {
 public:
  DebugUi(SDL_Window* window, SDL_GLContext context, Stats* frame_stats,
          EngineModules* modules, Allocator* allocator)
      : stats_(frame_stats), modules_(modules), expression_table_(allocator) {
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
      DebugConsole::Instance().ForAllLines([](std::string_view line) {
        ImGui::TextUnformatted(line.data(), line.end());
      });
      ImGui::EndChild();
      ImGui::TreePop();
    }
    if (ImGui::Button("Copy", ImVec2(ImGui::GetWindowSize().x, 0.0f))) {
      CopyToClipboard();
    }
    ImGui::InputText("Screenshot filename", screenshot_filename_,
                     sizeof(screenshot_filename_));
    if (ImGui::Button("Take Screenshot")) {
      LOG("Requesting screenshot for ", screenshot_filename_);
      const IVec2 viewport = modules_->batch_renderer.GetViewport();
      modules_->batch_renderer.RequestScreenshot(screenshot_, viewport.x,
                                                 viewport.y, this);
    }
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  void ProcessEvent(const SDL_Event& event) {
    ImGui_ImplSDL2_ProcessEvent(&event);
  }

  void HandleScreenshot(uint8_t* buffer, size_t width, size_t height) {
    TIMER("Screenshot");
    FixedStringBuffer<kMaxLogLineLength> err;
    CHECK(WritePixelsToImage(screenshot_filename_, buffer, width, height,
                             &modules_->filesystem, &err, &allocator_),
          "Could not write screenshot: ", err);
    LOG("Wrote screenshot with width ", width, " and height ", height);
  }

 private:
  void CopyToClipboard() {
    size_t buffer_sz = 0, pos = 0;
    auto& console = DebugConsole::Instance();
    console.ForAllLines(
        [&buffer_sz](std::string_view line) { buffer_sz += line.size() + 1; });
    auto* buffer = new char[buffer_sz + 1];
    console.ForAllLines([&buffer, &pos](std::string_view line) {
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
    if (stats_->samples() > 1) {
      FixedStringBuffer<kMaxLogLineLength> fps_line(
          "Frame Stats: ", *stats_, " FPS: ", 1.0 / stats_->avg());
      ImGui::TextUnformatted(fps_line.str());
    }
    FixedStringBuffer<kMaxLogLineLength> allocs_line(
        "Lua Allocations: ", modules_->lua.AllocatorStats());
    ImGui::TextUnformatted(allocs_line.str());
    auto* allocator = GlobalEngineAllocator();
    FixedStringBuffer<kMaxLogLineLength> memory_used_line(
        "Allocator memory used: ", allocator->used_memory(), " / ",
        allocator->total_memory(), " (",
        ((100.0 * allocator->used_memory()) / allocator->total_memory()), ")");
    ImGui::TextUnformatted(memory_used_line.str());
    const StringTable::Stats stats = StringTable::Instance().stats();
    FixedStringBuffer<kMaxLogLineLength> strings_used_line;
    strings_used_line.Set(
        "Strings used: ", stats.strings_used, " / ", stats.total_strings, " (",
        ((100.0 * stats.strings_used) / stats.total_strings), ")");
    ImGui::TextUnformatted(strings_used_line.str());
    strings_used_line.Set(
        "String space used: ", stats.space_used, " / ", stats.total_space, " (",
        ((100.0 * stats.space_used) / stats.total_space), ")");
    ImGui::TextUnformatted(strings_used_line.str());
    if (ImGui::BeginTable("Watchers", 2)) {
      DebugConsole::Instance().ForAllWatchers(
          [](std::string_view key, std::string_view value) {
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
  Dictionary<const char*> expression_table_;
  bool show_ = false;
  int frame_stats_location_ = 1;
  char screenshot_filename_[256];
  static constexpr size_t kWidth = 4096;
  static constexpr size_t kHeight = 4096;
  static constexpr size_t kChannels = 4;
  static constexpr size_t kTotalSize = kWidth * kHeight * kChannels;
  uint8_t screenshot_[kTotalSize];
  StaticAllocator<kTotalSize> allocator_;
};

class Game {
 public:
  Game(int argc, const char* argv[], Allocator* allocator)
      : arguments_(argv + 1, argv + argc), allocator_(allocator) {
    TIMER("Setup");
    InitializeLogging();
    // Initialize the debug console.
    DebugConsole::Instance();
    LOG("Program name = ", argv[0], " args = ", arguments_.size());
    for (size_t i = 0; i < arguments_.size(); ++i) {
      LOG("argv[", i, "] = ", arguments_[i]);
    }
    PHYSFS_CHECK(PHYSFS_init(argv[0]),
                 "Could not initialize PhysFS: ", argv[0]);
    assets_ = GetAssets(arguments_, allocator_);
    LoadConfig(*assets_, &config_, allocator_);
    {
      TIMER("SDL2 initialization");
      InitializeSDL(config_);
      window_ = CreateWindow(config_);
      context_ = CreateOpenglContext(config_, window_);
    }
    PrintSystemInformation();
  }

  ~Game() {
    Destroy(allocator_, e_);
    Destroy(allocator_, debug_ui_);
    if (SDL_WasInit(SDL_INIT_HAPTIC) != 0) {
      SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    }
    Mix_Quit();
    if (SDL_WasInit(SDL_INIT_JOYSTICK)) {
      SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
      SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    PHYSFS_CHECK(PHYSFS_deinit(), "Could not close PhysFS");
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
  }

  void Init() {
    TIMER("Game Initialization");
    e_ = New<EngineModules>(allocator_, assets_, config_, window_, allocator_);
    debug_ui_ =
        New<DebugUi>(allocator_, window_, context_, &stats_, e_, allocator_);
    e_->InitializeLua();
    e_->lua.Init();
  }

  void Run() {
    double last_frame = NowInSeconds();
    constexpr double kStep = TimeStepInSeconds();
    double t = 0, accum = 0;
    for (;;) {
      if (e_->lua.Stopped()) return;
      if (e_->lua.HasError() && e_->keyboard.IsDown(SDL_SCANCODE_Q)) {
        e_->lua.Stop();
        return;
      }
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
          e_->lua.HandleQuit();
          return;
        }
        ImGui_ImplSDL2_ProcessEvent(&event);
        e_->HandleEvent(event);
        if (event.type == SDL_KEYDOWN) {
          if (e_->keyboard.IsDown(SDL_SCANCODE_TAB)) {
            if (config_.enable_debug_rendering)
              e_->batch_renderer.ToggleDebugRender();
            if (config_.enable_debug_ui) debug_ui_->Toggle();
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

  void RenderCrashScreen(std::string_view error) {
    const IVec2 viewport = e_->batch_renderer.GetViewport();
    e_->renderer.SetColor(Color::Black());
    e_->renderer.DrawRect(/*top_left=*/FVec(0, 0), FVec(viewport.x, viewport.y),
                          /*angle=*/0);
    e_->renderer.SetColor(Color::White());
    e_->renderer.DrawText("debug_font.ttf", 20, error, FVec(50, 50));
  }

  // Update state given current time t and frame delta dt, both in ms.
  void Update(double t, double dt) {
    WATCH_VAR(t);
    WATCH_VAR(dt);
    char error[1024];
    if (size_t error_len = e_->lua.Error(error, sizeof(error) - 1);
        error_len > 0) {
      e_->sound.Stop();
      RenderCrashScreen(std::string_view(error, error_len));
    } else {
      e_->physics.Update(dt);
      e_->lua.Update(t, dt);
    }
    WATCH_EXPR("Mouse Position ", e_->mouse.GetPosition());
  }

  void Render() {
    e_->Render();
    debug_ui_->Render();
    SDL_GL_SwapWindow(window_);
  }

 private:
  const std::vector<const char*> arguments_;
  Allocator* allocator_;
  Assets* assets_ = nullptr;
  GameConfig config_;
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_;
  DebugUi* debug_ui_;
  EngineModules* e_;
  Stats stats_;
};

void GameMain(int argc, const char* argv[]) {
  auto* allocator = GlobalEngineAllocator();
  // Ensure we don't overflow the stack in any module by allocating Game on
  // the heap.
  auto* g = New<Game>(allocator, argc, argv, allocator);
  g->Init();
  g->Run();
  Destroy(allocator, g);
}

}  // namespace G

int main(int argc, const char* argv[]) {
  G::GameMain(argc, argv);
  return 0;
}