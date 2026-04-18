#pragma once
#ifndef _GAME_DEBUG_UI_H
#define _GAME_DEBUG_UI_H

#include <SDL3/SDL.h>

#include "allocators.h"
#include "camera.h"
#include "circular_buffer.h"
#include "logging.h"
#include "physics.h"
#include "renderer.h"
#include "sound.h"

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

  // Sets the batch renderer for viewport resizing from the debug UI.
  void SetBatchRenderer(BatchRenderer* br) { batch_renderer_ = br; }

  // Sets the sound system for the audio debug panel.
  void SetSound(Sound* sound) { sound_ = sound; }

  // Sets the engine arena allocator for the memory debug panel.
  void SetEngineArena(ArenaAllocator* arena) { engine_arena_ = arena; }

  // Sets the frame allocator for the memory debug panel.
  void SetFrameArena(ArenaAllocator* arena) { frame_arena_ = arena; }

  // Sets the renderer for the renderer debug panel.
  void SetRenderer(Renderer* renderer) { renderer_ = renderer; }

  // Sets the shaders for the renderer debug panel.
  void SetShaders(Shaders* shaders) { shaders_ = shaders; }

  // Sets the camera for the camera debug panel.
  void SetCamera(Camera* camera) { camera_ = camera; }

  // Sets the physics world for the physics debug panel.
  void SetPhysics(Physics* physics) { physics_ = physics; }

  // Sets the asset database for the asset viewer panel.
  void SetDb(sqlite3* db) { db_ = db; }

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

  // Per-frame data passed to DrawAll.
  struct FrameContext {
    FrameStats frame_stats;
    float lua_memory_kb;
    size_t lua_memory_bytes;
    size_t cmd_buf_used;
    size_t cmd_buf_capacity;
  };

  // Draws the menu bar and all enabled panels. Call between Begin/EndFrame.
  void DrawAll(const FrameContext& ctx);

  // Returns true if a screenshot was requested via the quick actions menu.
  bool ConsumeScreenshotRequest();

  // Returns true if a hot-reload was requested via the quick actions menu.
  bool ConsumeHotReloadRequest();

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
  Sound* sound_ = nullptr;
  ArenaAllocator* engine_arena_ = nullptr;
  ArenaAllocator* frame_arena_ = nullptr;
  Renderer* renderer_ = nullptr;
  Shaders* shaders_ = nullptr;
  Camera* camera_ = nullptr;
  Physics* physics_ = nullptr;
  sqlite3* db_ = nullptr;
  SDL_Window* window_ = nullptr;
  BatchRenderer* batch_renderer_ = nullptr;
  CircularBuffer<float>* frame_times_ = nullptr;
  CircularBuffer<float>* lua_memory_samples_ = nullptr;

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
  void DrawAssetViewer();

  // Panel visibility toggles.
  bool show_performance_ = true;
  bool show_log_console_ = true;
  bool show_entity_inspector_ = false;
  bool show_audio_ = false;
  bool show_memory_ = false;
  bool show_renderer_ = false;
  bool show_camera_ = false;
  bool show_physics_ = false;
  bool show_assets_ = false;

  // Action requests from menu bar (consumed by game loop).
  bool screenshot_requested_ = false;
  bool hot_reload_requested_ = false;

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
  void SetBatchRenderer(BatchRenderer*) {}
  void SetSound(Sound*) {}
  void SetEngineArena(ArenaAllocator*) {}
  void SetFrameArena(ArenaAllocator*) {}
  void SetRenderer(Renderer*) {}
  void SetShaders(Shaders*) {}
  void SetCamera(Camera*) {}
  void SetPhysics(Physics*) {}
  void SetDb(sqlite3*) {}
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
  struct FrameContext {};
  void DrawAll(const FrameContext&) {}
  bool ConsumeScreenshotRequest() { return false; }
  bool ConsumeHotReloadRequest() { return false; }
};

}  // namespace G

#endif  // GAME_WITH_IMGUI

#endif  // _GAME_DEBUG_UI_H
