#include "debug_ui.h"

#ifdef GAME_WITH_IMGUI

#include <cctype>
#include <cstring>
#include <string_view>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include "engine.h"
#include "libraries/sqlite3.h"
#include "lua.h"
#include "platform.h"
#include "string_table.h"

namespace G {

namespace {

// Shared helpers used by all panel files.
#include "debug_ui_helpers.inc.cc"

}  // namespace

// Global pointer used by the LogSink callback to route messages to the
// DebugUI ring buffer. Only one DebugUI instance exists at a time.
DebugUI* g_debug_ui = nullptr;

// The original log sink installed before we intercept.
LogSink g_original_sink = nullptr;

namespace {

// Log sink that forwards to both the original sink and the DebugUI buffer.
void DebugUILogSink(LogLevel level, const char* message) {
  if (g_original_sink) g_original_sink(level, message);
  if (g_debug_ui) g_debug_ui->LogMessage(level, message);
}

// Routes ImGui allocations through the engine's SystemAllocator.
void* ImGuiAllocFunc(size_t size, void* /*user_data*/) {
  return SystemAllocator::Instance()->Alloc(size, /*align=*/16);
}

void ImGuiFreeFunc(void* ptr, void* /*user_data*/) {
  SystemAllocator::Instance()->Dealloc(ptr, /*sz=*/0);
}

}  // namespace

// Core lifecycle.

void DebugUI::StartLogCapture(Allocator* allocator) {
  allocator_ = allocator;
  log_entries_ =
      allocator_->New<CircularBuffer<LogEntry>>(kMaxLogEntries, allocator_);
  g_original_sink = GetLogSink();
  g_debug_ui = this;
  SetLogSink(DebugUILogSink);
}

void DebugUI::Init(SDL_Window* window, SDL_GLContext gl_context) {
  ImGui::SetAllocatorFunctions(ImGuiAllocFunc, ImGuiFreeFunc);
  ImGui::CreateContext();
  window_ = window;
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.FrameRounding = 2.0f;
  style.Colors[ImGuiCol_WindowBg].w = 0.85f;
  ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init(/*glsl_version=*/"#version 150");
  frame_times_ = allocator_->New<CircularBuffer<float>>(kFrameTimeHistory,
                                                        allocator_);
  lua_memory_samples_ = allocator_->New<CircularBuffer<float>>(
      kFrameTimeHistory, allocator_);
  breakdown_history_ = allocator_->New<CircularBuffer<FrameBreakdown>>(
      kFrameTimeHistory, allocator_);
  repl_entries_ = allocator_->New<CircularBuffer<ReplEntry>>(
      kMaxReplEntries, allocator_);
  initialized_ = true;
  LOG("Debug UI initialized (Dear ImGui ", IMGUI_VERSION, ")");
}

void DebugUI::Shutdown() {
  if (!initialized_) return;
  SetLogSink(g_original_sink);
  g_debug_ui = nullptr;
  g_original_sink = nullptr;
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  initialized_ = false;
}

int DebugUI::EvalHistoryCallback(ImGuiInputTextCallbackData* data) {
  auto* ui = static_cast<DebugUI*>(data->UserData);
  if (ui->eval_history_count_ == 0) return 0;
  if (data->EventKey == ImGuiKey_UpArrow) {
    if (ui->eval_history_pos_ == -1) {
      ui->eval_history_pos_ = ui->eval_history_count_ - 1;
    } else if (ui->eval_history_pos_ > 0 &&
               ui->eval_history_pos_ >
                   ui->eval_history_count_ - kEvalHistoryMax) {
      ui->eval_history_pos_--;
    }
  } else if (data->EventKey == ImGuiKey_DownArrow) {
    if (ui->eval_history_pos_ != -1) {
      ui->eval_history_pos_++;
      if (ui->eval_history_pos_ >= ui->eval_history_count_) {
        ui->eval_history_pos_ = -1;
      }
    }
  }
  const char* text = (ui->eval_history_pos_ >= 0)
                         ? ui->eval_history_entries_[ui->eval_history_pos_ %
                                                     kEvalHistoryMax]
                         : "";
  data->DeleteChars(0, data->BufTextLen);
  data->InsertChars(0, text);
  return 0;
}

int DebugUI::ReplHistoryCallback(ImGuiInputTextCallbackData* data) {
  return EvalHistoryCallback(data);
}

void DebugUI::ProcessEvent(const SDL_Event* event) {
  if (!initialized_) return;
  ImGui_ImplSDL3_ProcessEvent(event);
}

void DebugUI::BeginFrame() {
  if (!initialized_ || !visible_) return;
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void DebugUI::EndFrame() {
  if (!initialized_ || !visible_) return;
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void DebugUI::Toggle() { visible_ = !visible_; }

bool DebugUI::WantCaptureMouse() const {
  if (!initialized_ || !visible_) return false;
  return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::WantCaptureKeyboard() const {
  if (!initialized_ || !visible_) return false;
  return ImGui::GetIO().WantCaptureKeyboard;
}

void DebugUI::AddFrameTimeSample(float ms) {
  if (!initialized_) return;
  frame_times_->Push(ms);
}

void DebugUI::AddLuaMemorySample(float kb) {
  if (!initialized_) return;
  lua_memory_samples_->Push(kb);
}

void DebugUI::AddBreakdownSample(const DebugUI::FrameBreakdown& bd) {
  if (!initialized_) return;
  breakdown_history_->Push(bd);
}

void DebugUI::LogMessage(LogLevel level, const char* message) {
  if (log_entries_ == nullptr) return;
  LogEntry entry;
  entry.level = level;
  size_t len = strlen(message);
  if (len > kMaxLogLineLength) len = kMaxLogLineLength;
  memcpy(entry.text, message, len);
  entry.text[len] = '\0';
  log_entries_->Push(entry);
  if (log_auto_scroll_) log_scroll_to_bottom_ = true;
}

// Panel implementations (unity build includes).
#include "debug_ui_performance.inc.cc"
#include "debug_ui_log.inc.cc"
#include "debug_ui_inspector.inc.cc"
#include "debug_ui_panels.inc.cc"
#include "debug_ui_docs.inc.cc"
#include "debug_ui_watch.inc.cc"
#include "debug_ui_assets.inc.cc"

// Menu bar and dispatch.

void DebugUI::DrawMenuBar(const FrameContext& ctx) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("Panels")) {
      PanelMenuItem("Performance", kPanelPerformance);
      PanelMenuItem("Log Console", kPanelLogConsole);
      PanelMenuItem("Entity Inspector", kPanelEntityInspector);
      PanelMenuItem("Audio", kPanelAudio);
      PanelMenuItem("Memory", kPanelMemory);
      PanelMenuItem("Renderer", kPanelRenderer);
      PanelMenuItem("Camera", kPanelCamera);
      PanelMenuItem("Physics", kPanelPhysics);
      PanelMenuItem("Assets", kPanelAssets);
      PanelMenuItem("API Docs", kPanelDocs);
      PanelMenuItem("Watch", kPanelWatch);
      ImGui::Separator();
      if (ImGui::MenuItem("Cycle Presets", "F5")) {
        static constexpr uint64_t kPresets[] = {0, kPanelAll, kPanelDefault};
        panel_preset_ = (panel_preset_ + 1) % 3;
        panels_ = (panels_ & ~kPanelAll) | kPresets[panel_preset_];
      }
      if (ImGui::MenuItem("Panel Picker", "F6")) {
        TogglePanel(kPanelSelector);
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Actions")) {
      if (ImGui::MenuItem("Screenshot (F12)")) screenshot_requested_ = true;
      if (ImGui::MenuItem("Hot Reload")) hot_reload_requested_ = true;
      if (ImGui::MenuItem("Run GC")) engine_->lua.RunGc();
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) quit_requested_ = true;
      ImGui::EndMenu();
    }
    if (window_ != nullptr && ImGui::BeginMenu("Window")) {
      int w = 0, h = 0;
      SDL_GetWindowSize(window_, &w, &h);
      ImGui::Text("Current: %dx%d", w, h);
      ImGui::Separator();
      struct Preset {
        const char* label;
        int w, h;
      };
      const Preset presets[] = {
          {"800x600", 800, 600},     {"1280x720", 1280, 720},
          {"1440x900", 1440, 900},   {"1920x1080", 1920, 1080},
          {"2560x1440", 2560, 1440},
      };
      for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); ++i) {
        bool current = (w == presets[i].w && h == presets[i].h);
        if (ImGui::MenuItem(presets[i].label, nullptr, current)) {
          SDL_SetWindowSize(window_, presets[i].w, presets[i].h);
          if (engine_ != nullptr) {
            engine_->batch_renderer.SetViewport(
                IVec2(presets[i].w, presets[i].h));
          }
        }
      }
      ImGui::EndMenu();
    }
    // Inline controls: Quit, Pause/Play, Step, timescale, Center, Drag.
    ImGui::Separator();
    if (ImGui::SmallButton("Quit")) quit_requested_ = true;
    ImGui::SameLine();
    float ts = engine_->lua.TimeScale();
    bool paused = (ts == 0.0f);
    if (ImGui::SmallButton(paused ? "Play" : "Pause")) {
      engine_->lua.SetTimeScale(paused ? 1.0f : 0.0f);
      ts = engine_->lua.TimeScale();
    }
    ImGui::SameLine();
    if (paused && ImGui::SmallButton("Step")) {
      step_requested_ = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::SliderFloat("##timescale", &ts, 0.0f, 4.0f, "%.2fx")) {
      engine_->lua.SetTimeScale(ts);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Center")) {
      SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED);
    }
    ImGui::SameLine();
    ImGui::SmallButton("Drag");
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
      int wx = 0, wy = 0;
      SDL_GetWindowPosition(window_, &wx, &wy);
      ImVec2 delta = ImGui::GetMouseDragDelta(0);
      ImGui::ResetMouseDragDelta(0);
      SDL_SetWindowPosition(window_, wx + static_cast<int>(delta.x),
                            wy + static_cast<int>(delta.y));
    }
    // FPS readout on the right.
    if (frame_times_ != nullptr && frame_times_->size() > 0) {
      float last_ms = (*frame_times_)[frame_times_->size() - 1];
      float fps = (last_ms > 0.0f) ? 1000.0f / last_ms : 0.0f;
      SmallBuffer fps_text;
      fps_text.AppendF("%.0f FPS", static_cast<double>(fps));
      float text_width = ImGui::CalcTextSize(fps_text.str()).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - text_width - 10);
      ImGui::TextUnformatted(fps_text.str());
    }
    ImGui::EndMainMenuBar();
  }
}

void DebugUI::PanelMenuItem(const char* label, Panel p) {
  bool open = PanelOpen(p);
  if (ImGui::MenuItem(label, nullptr, &open)) TogglePanel(p);
}

void DebugUI::DrawAll(const FrameContext& ctx) {
  if (!initialized_ || !visible_ || engine_ == nullptr) return;
  DrawMenuBar(ctx);
  if (PanelOpen(kPanelPerformance)) DrawPerformancePanel(ctx);
  if (PanelOpen(kPanelLogConsole)) DrawLogConsole();
  if (PanelOpen(kPanelEntityInspector)) DrawEntityInspector();
  if (PanelOpen(kPanelAudio)) DrawAudioPanel();
  if (PanelOpen(kPanelMemory)) DrawMemoryPanel(ctx.lua_memory_bytes);
  if (PanelOpen(kPanelRenderer)) DrawRendererPanel(ctx);
  if (PanelOpen(kPanelCamera)) DrawCameraPanel();
  if (PanelOpen(kPanelPhysics)) DrawPhysicsPanel();
  if (PanelOpen(kPanelAssets)) DrawAssetViewer();
  if (PanelOpen(kPanelDocs)) DrawDocsPanel();
  if (PanelOpen(kPanelWatch)) DrawWatchPanel();

  // F6 panel selector floating window.
  if (PanelOpen(kPanelSelector)) {
    ImGui::SetNextWindowPos(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Panel Selector", &open,
                     ImGuiWindowFlags_AlwaysAutoResize)) {
      PanelMenuItem("Performance", kPanelPerformance);
      PanelMenuItem("Log Console", kPanelLogConsole);
      PanelMenuItem("Entity Inspector", kPanelEntityInspector);
      PanelMenuItem("Audio", kPanelAudio);
      PanelMenuItem("Memory", kPanelMemory);
      PanelMenuItem("Renderer", kPanelRenderer);
      PanelMenuItem("Camera", kPanelCamera);
      PanelMenuItem("Physics", kPanelPhysics);
      PanelMenuItem("Assets", kPanelAssets);
      PanelMenuItem("API Docs", kPanelDocs);
      PanelMenuItem("Watch", kPanelWatch);
    }
    ImGui::End();
    if (!open) TogglePanel(kPanelSelector);
  }

  // Texture zoom popup.
  if (zoom_texture_ != 0) {
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Texture Zoom", &open,
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
      ImGui::SliderFloat("Zoom", &zoom_level_, 0.25f, 8.0f, "%.2fx");
      ImGui::SameLine();
      if (ImGui::Button("1:1")) zoom_level_ = 1.0f;
      ImGui::SameLine();
      if (ImGui::Button("Fit")) {
        float avail_w = ImGui::GetContentRegionAvail().x;
        float avail_h = ImGui::GetContentRegionAvail().y;
        zoom_level_ =
            (zoom_tex_w_ > 0 && zoom_tex_h_ > 0)
                ? fmin(avail_w / zoom_tex_w_, avail_h / zoom_tex_h_)
                : 1.0f;
      }
      if (ImGui::BeginChild("ZoomRegion", ImVec2(0, 0), false,
                            ImGuiWindowFlags_HorizontalScrollbar)) {
        float w = zoom_tex_w_ * zoom_level_;
        float h = zoom_tex_h_ * zoom_level_;
        ImVec2 img_pos = ImGui::GetCursorScreenPos();
        ImGui::Image(
            static_cast<ImTextureID>(static_cast<uintptr_t>(zoom_texture_)),
            ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
        // Color under cursor.
        if (ImGui::IsItemHovered() && zoom_pixels_ != nullptr) {
          ImVec2 mouse = ImGui::GetMousePos();
          float rel_x = (mouse.x - img_pos.x) / zoom_level_;
          float rel_y = (mouse.y - img_pos.y) / zoom_level_;
          int px = static_cast<int>(rel_x);
          int py = static_cast<int>(zoom_tex_h_ - 1.0f - rel_y);
          int tw = static_cast<int>(zoom_tex_w_);
          int th = static_cast<int>(zoom_tex_h_);
          if (px >= 0 && px < tw && py >= 0 && py < th) {
            int offset = (py * tw + px) * 4;
            float r = zoom_pixels_[offset + 0] / 255.0f;
            float g = zoom_pixels_[offset + 1] / 255.0f;
            float b = zoom_pixels_[offset + 2] / 255.0f;
            float a = zoom_pixels_[offset + 3] / 255.0f;
            ImVec4 color(r, g, b, a);
            ImGui::BeginTooltip();
            ImGui::ColorButton("##pixel", color,
                               ImGuiColorEditFlags_AlphaPreview,
                               ImVec2(32, 32));
            ImGui::SameLine();
            ImGui::Text("(%d, %d)\n#%02X%02X%02X%02X\nRGBA: %d %d %d %d",
                        px, py, zoom_pixels_[offset],
                        zoom_pixels_[offset + 1], zoom_pixels_[offset + 2],
                        zoom_pixels_[offset + 3], zoom_pixels_[offset],
                        zoom_pixels_[offset + 1], zoom_pixels_[offset + 2],
                        zoom_pixels_[offset + 3]);
            ImGui::EndTooltip();
          }
        }
        // Drag to pan via scroll.
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
          ImVec2 delta = ImGui::GetMouseDragDelta(0);
          ImGui::ResetMouseDragDelta(0);
          ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
          ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
        }
      }
      ImGui::EndChild();
    }
    ImGui::End();
    if (!open) {
      zoom_texture_ = 0;
      if (zoom_pixels_ != nullptr) {
        allocator_->Dealloc(zoom_pixels_, zoom_pixels_size_);
        zoom_pixels_ = nullptr;
        zoom_pixels_size_ = 0;
      }
    }
  }
}

bool DebugUI::ConsumeScreenshotRequest() {
  bool r = screenshot_requested_;
  screenshot_requested_ = false;
  return r;
}

bool DebugUI::ConsumeHotReloadRequest() {
  bool r = hot_reload_requested_;
  hot_reload_requested_ = false;
  return r;
}

bool DebugUI::ConsumeStepRequest() {
  bool r = step_requested_;
  step_requested_ = false;
  return r;
}

bool DebugUI::ConsumeQuitRequest() {
  bool r = quit_requested_;
  quit_requested_ = false;
  return r;
}

void DebugUI::HandleKeyShortcut(SDL_Scancode scancode) {
  if (!initialized_ || !visible_) return;
  switch (scancode) {
    case SDL_SCANCODE_F5: {
      // Cycle presets: none -> all -> default (perf+logs) -> none.
      static constexpr uint64_t kPresets[] = {0, kPanelAll, kPanelDefault};
      panel_preset_ = (panel_preset_ + 1) % 3;
      panels_ = (panels_ & ~kPanelAll) | kPresets[panel_preset_];
      break;
    }
    case SDL_SCANCODE_F6:
      TogglePanel(kPanelSelector);
      break;
    default:
      break;
  }
}

}  // namespace G

#endif  // GAME_WITH_IMGUI
