#pragma once
#ifndef _GAME_DEBUG_UI_H
#define _GAME_DEBUG_UI_H

#include <SDL3/SDL.h>

#include "allocators.h"
#include "circular_buffer.h"
#include "logging.h"
#include "renderer.h"

#ifdef GAME_WITH_IMGUI

namespace G {

class Lua;

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

  // Sets the Lua VM used by the log console's eval input.
  void SetLua(Lua* lua) { lua_ = lua; }

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

  // Captures a log message with its severity level.
  void LogMessage(LogLevel level, const char* message);

  // Draws the performance panel using current engine stats.
  void DrawPerformancePanel(const FrameStats& frame_stats,
                            float lua_memory_kb,
                            size_t cmd_buf_used,
                            size_t cmd_buf_capacity);

  // Draws the log console panel with filtering and Lua eval.
  void DrawLogConsole();

  // Draws the entity inspector panel showing live Lua state.
  void DrawEntityInspector();

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

  bool visible_ = false;
  bool initialized_ = false;
  Allocator* allocator_ = nullptr;
  Lua* lua_ = nullptr;
  SDL_Window* window_ = nullptr;
  CircularBuffer<float>* frame_times_ = nullptr;
  CircularBuffer<float>* lua_memory_samples_ = nullptr;

  // Log console state.
  CircularBuffer<LogEntry>* log_entries_ = nullptr;
  bool log_auto_scroll_ = true;
  bool log_scroll_to_bottom_ = false;
  bool log_level_filter_[6] = {true, true, true, true, true, true};
  char log_text_filter_[128] = {};
  char eval_input_[kEvalInputSize] = {};
};

}  // namespace G

#else  // !GAME_WITH_IMGUI

namespace G {

class Lua;

// Stub implementation when ImGui is compiled out. All methods are no-ops.
class DebugUI {
 public:
  void StartLogCapture(Allocator*) {}
  void Init(SDL_Window*, SDL_GLContext) {}
  void Shutdown() {}
  void SetLua(Lua*) {}
  void ProcessEvent(const SDL_Event*) {}
  void BeginFrame() {}
  void EndFrame() {}
  void Toggle() {}
  bool visible() const { return false; }
  bool WantCaptureMouse() const { return false; }
  bool WantCaptureKeyboard() const { return false; }
  void AddFrameTimeSample(float) {}
  void AddLuaMemorySample(float) {}
  void LogMessage(LogLevel, const char*) {}
  void DrawPerformancePanel(const FrameStats&, float, size_t, size_t) {}
  void DrawLogConsole() {}
  void DrawEntityInspector() {}
};

}  // namespace G

#endif  // GAME_WITH_IMGUI

#endif  // _GAME_DEBUG_UI_H
