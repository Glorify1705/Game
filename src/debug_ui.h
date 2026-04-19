#pragma once
#ifndef _GAME_DEBUG_UI_H
#define _GAME_DEBUG_UI_H

#include <SDL3/SDL.h>

#include <imgui.h>
#include <TextEditor.h>

#include "allocators.h"
#include "circular_buffer.h"
#include "engine.h"
#include "logging.h"
#include "renderer.h"

#ifdef GAME_WITH_IMGUI

namespace G {

// ImGui-based debug overlay rendered on top of the game. Manages its own
// OpenGL state and renders after the engine's batch renderer. Compiled out
// entirely when GAME_WITH_IMGUI is not defined.
class DebugUI {
 public:
  // Begins capturing engine log messages into a ring buffer. Call early in
  // startup (before any LOG calls) so that no messages are lost. The buffer
  // is allocated from the provided allocator.
  void StartLogCapture(Allocator* allocator);

  // Initializes ImGui context, backends, and theme. Must be called after
  // SDL and GL are fully initialized. StartLogCapture() must have been
  // called first.
  void Init(SDL_Window* window, SDL_GLContext gl_context);

  // Tears down ImGui backends and destroys the context.
  void Shutdown();

  // Sets the engine whose subsystems the debug panels inspect.
  void SetEngine(Engine* engine) { engine_ = engine; }

  // Sets the engine arena allocator for the memory panel (not owned by Engine).
  void SetEngineArena(ArenaAllocator* arena) { engine_arena_ = arena; }

  // Sets whether the window should re-center after resize.
  void SetWindowCentered(bool centered) { window_centered_ = centered; }

  // Forwards an SDL event to ImGui for input handling.
  void ProcessEvent(const SDL_Event* event);

  // Starts a new ImGui frame. Call after the engine's batch renderer
  // finishes but before SwapWindow.
  void BeginFrame();

  // Submits ImGui draw data to the GPU. Call after BeginFrame and all
  // ImGui widget calls.
  void EndFrame();

  // Toggles the debug overlay visibility.
  void Toggle();

  // Returns true when the overlay is visible.
  bool visible() const { return visible_; }

  // Returns true when ImGui wants to consume mouse events.
  bool WantCaptureMouse() const;

  // Returns true when ImGui wants to consume keyboard events.
  bool WantCaptureKeyboard() const;

  // Records a frame time sample for the performance graph.
  void AddFrameTimeSample(float ms);

  // Records a Lua memory sample (in KB) for the memory sparkline.
  void AddLuaMemorySample(float kb);

  // Per-phase frame time breakdown (one frame lagged).
  struct FrameBreakdown {
    float update_ms = 0;
    float draw_ms = 0;
    float render_ms = 0;
    float debug_ui_ms = 0;
  };

  // Records a frame breakdown sample for the stacked history chart.
  void AddBreakdownSample(const FrameBreakdown& bd);

  // Captures a log message with its severity level.
  void LogMessage(LogLevel level, const char* message);

  // Per-frame data passed to DrawAll.
  struct FrameContext {
    FrameStats frame_stats;
    float lua_memory_kb;
    size_t lua_memory_bytes;
    size_t cmd_buf_used;
    size_t cmd_buf_capacity;
    FrameBreakdown breakdown;
  };

  // Draws the menu bar and all enabled panels. Call between Begin/EndFrame.
  void DrawAll(const FrameContext& ctx);

  // Returns true if a screenshot was requested via the quick actions menu.
  bool ConsumeScreenshotRequest();

  // Returns true if a hot-reload was requested via the quick actions menu.
  bool ConsumeHotReloadRequest();

  // Returns true if a single frame step was requested (while paused).
  bool ConsumeStepRequest();

  // Returns true if a quit was requested via the Actions menu.
  bool ConsumeQuitRequest();

  // Handles F5/F6/F7 shortcuts. Call from PollEvents before ImGui
  // capture check so shortcuts work regardless of focus.
  void HandleKeyShortcut(SDL_Scancode scancode);

  // Shared state for script/shader code editor tabs.
  struct CodeEditorState {
    TextEditor editor;
    PathBuffer loaded_name;
    bool read_only = true;
  };

 private:
  // Rolling buffer of frame times for the PlotLines graph.
  static constexpr size_t kFrameTimeHistory = 300;
  // Maximum log entries retained in the ring buffer.
  static constexpr size_t kMaxLogEntries = 1024;
  // Maximum length of the Lua eval input field.
  static constexpr size_t kEvalInputSize = 512;

  // A single captured log entry.
  struct LogEntry {
    LogLevel level;
    char text[kMaxLogLineLength + 1];
  };

  // ImGui callback for eval history Up/Down navigation.
  static int EvalHistoryCallback(ImGuiInputTextCallbackData* data);

  // Draws the menu bar with panel toggles, time controls, and quick actions.
  void DrawMenuBar(const FrameContext& ctx);

  // Individual panel draw methods (called by DrawAll when enabled).
  void DrawPerformancePanel(const FrameContext& ctx);
  void DrawLogConsole();
  void DrawEntityInspector();
  void DrawAudioPanel();
  void DrawMemoryPanel(size_t lua_memory_bytes);
  void DrawRendererPanel(const FrameContext& ctx);
  void DrawCameraPanel();
  void DrawPhysicsPanel();
  // Draws the asset viewer with tabbed image, sprite, audio, script views.
  void DrawAssetViewer();
  void DrawAssetImagesTab();
  void DrawAssetSpritesTab();
  void DrawAssetAudioTab();
  void DrawAssetDbTab(const char* label, const char* sql);
  void DrawAssetScriptsTab();
  void DrawAssetShadersTab();
  // Draws the REPL input and output area (embedded in Watch panel).
  void DrawRepl();
  // Returns true if a log entry passes the current level and text filters.
  bool ShouldShowLogEntry(const LogEntry& entry) const;
  // Resizes the window and viewport, re-centering if window_centered_.
  void ResizeWindow(int w, int h);
  // Draws the API docs browser with search.
  void DrawDocsPanel();
  // Draws the variable watch panel with live Lua path resolution.
  void DrawWatchPanel();

  bool visible_ = false;
  bool initialized_ = false;
  bool window_centered_ = false;
  bool window_menu_requested_ = false;
  Allocator* allocator_ = nullptr;
  Engine* engine_ = nullptr;
  ArenaAllocator* engine_arena_ = nullptr;
  SDL_Window* window_ = nullptr;
  CircularBuffer<float>* frame_times_ = nullptr;
  CircularBuffer<float>* lua_memory_samples_ = nullptr;
  CircularBuffer<FrameBreakdown>* breakdown_history_ = nullptr;

  // Panel visibility as a bitset.
  enum Panel : uint64_t {
    kPanelPerformance = 1 << 0,
    kPanelLogConsole = 1 << 1,
    kPanelEntityInspector = 1 << 2,
    kPanelAudio = 1 << 3,
    kPanelMemory = 1 << 4,
    kPanelRenderer = 1 << 5,
    kPanelCamera = 1 << 6,
    kPanelPhysics = 1 << 7,
    kPanelAssets = 1 << 8,
    kPanelSelector = 1 << 9,
    kPanelDocs = 1 << 10,
    kPanelWatch = 1 << 11,
    kPanelAll = kPanelPerformance | kPanelLogConsole | kPanelEntityInspector |
                kPanelAudio | kPanelMemory | kPanelRenderer | kPanelCamera |
                kPanelPhysics | kPanelAssets | kPanelDocs | kPanelWatch,
  };
  // Default panels (also preset index 2).
  static constexpr uint64_t kPanelDefault =
      kPanelPerformance | kPanelLogConsole;
  uint64_t panels_ = kPanelDefault;

  // F5 preset cycle index: 0 = none, 1 = all, 2 = default.
  int panel_preset_ = 2;

  // Helpers for panel visibility.
  bool PanelOpen(Panel p) const { return (panels_ & p) != 0; }
  void TogglePanel(Panel p) { panels_ ^= p; }

  // Draws a menu item bound to a panel flag.
  void PanelMenuItem(const char* label, Panel p);

  // Action requests from menu bar (consumed by game loop).
  bool screenshot_requested_ = false;
  bool hot_reload_requested_ = false;
  bool step_requested_ = false;
  bool quit_requested_ = false;

  // Log console state.
  CircularBuffer<LogEntry>* log_entries_ = nullptr;
  bool log_auto_scroll_ = true;
  bool log_scroll_to_bottom_ = false;
  bool log_level_filter_[6] = {true, true, true, true, true, true};
  char log_text_filter_[128] = {};
  char eval_input_[kEvalInputSize] = {};

  // Eval history ring buffer (defined in debug_ui.cc).
  enum { kEvalHistoryMax = 64 };
  char eval_history_entries_[kEvalHistoryMax][kEvalInputSize] = {};
  int eval_history_count_ = 0;
  int eval_history_pos_ = -1;

  // Entity inspector state.
  char inspector_filter_[128] = {};

  // API docs browser state.
  char docs_filter_[128] = {};

  // Watch panel state.
  static constexpr size_t kMaxWatches = 32;
  static constexpr size_t kWatchPathSize = 128;
  struct WatchEntry {
    char path[kWatchPathSize] = {};
  };
  WatchEntry watches_[kMaxWatches] = {};
  int watch_count_ = 0;
  char watch_input_[kWatchPathSize] = {};

  // REPL state (embedded in Watch panel).
  struct ReplEntry {
    char text[kMaxLogLineLength + 1] = {};
    bool is_input;
    bool is_error;
  };
  static constexpr size_t kMaxReplEntries = 256;
  CircularBuffer<ReplEntry>* repl_entries_ = nullptr;
  bool repl_scroll_to_bottom_ = false;
  enum ReplLang { kLua, kFennel };
  ReplLang repl_lang_ = kLua;
  TextEditor repl_editor_;
  bool repl_editor_init_ = false;

  // Script/shader editor state.
  CodeEditorState script_editor_;
  CodeEditorState shader_editor_;

  // ImGui callback for REPL history Up/Down navigation.
  static int ReplHistoryCallback(ImGuiInputTextCallbackData* data);

  // Asset viewer state.
  char asset_filter_[128] = {};

  // Audio preview state.
  uint32_t preview_source_ = 0;
  bool has_preview_ = false;
  PathBuffer preview_name_;

  // Camera drag-to-pan override state.
  bool camera_override_ = false;
  float camera_saved_x_ = 0.0f;
  float camera_saved_y_ = 0.0f;

  // Texture zoom popup state.
  GLuint zoom_texture_ = 0;
  float zoom_tex_w_ = 0.0f;
  float zoom_tex_h_ = 0.0f;
  float zoom_level_ = 1.0f;
  uint8_t* zoom_pixels_ = nullptr;
  size_t zoom_pixels_size_ = 0;
};

}  // namespace G

#else  // !GAME_WITH_IMGUI

namespace G {

// Stub implementation when ImGui is compiled out. All methods are no-ops.
class DebugUI {
 public:
  void StartLogCapture(Allocator*) {}
  void Init(SDL_Window*, SDL_GLContext) {}
  void Shutdown() {}
  void SetEngine(Engine*) {}
  void SetEngineArena(ArenaAllocator*) {}
  void SetWindowCentered(bool) {}
  void ProcessEvent(const SDL_Event*) {}
  void BeginFrame() {}
  void EndFrame() {}
  void Toggle() {}
  bool visible() const { return false; }
  bool WantCaptureMouse() const { return false; }
  bool WantCaptureKeyboard() const { return false; }
  void AddFrameTimeSample(float) {}
  void AddLuaMemorySample(float) {}
  struct FrameBreakdown {};
  void AddBreakdownSample(const FrameBreakdown&) {}
  void LogMessage(LogLevel, const char*) {}
  struct FrameContext {};
  void DrawAll(const FrameContext&) {}
  bool ConsumeScreenshotRequest() { return false; }
  bool ConsumeHotReloadRequest() { return false; }
  bool ConsumeStepRequest() { return false; }
  bool ConsumeQuitRequest() { return false; }
  void HandleKeyShortcut(SDL_Scancode) {}
};

}  // namespace G

#endif  // GAME_WITH_IMGUI

#endif  // _GAME_DEBUG_UI_H
