#include "debug_ui.h"

#ifdef GAME_WITH_IMGUI

#include <TextEditor.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <cctype>
#include <cstring>
#include <string_view>

#include "engine.h"
#include "libraries/sqlite3.h"
#include "lua.h"
#include "platform.h"
#include "sqlite_helpers.h"
#include "string_table.h"
#include "zone_stats.h"

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
  frame_times_ =
      allocator_->New<CircularBuffer<float>>(kFrameTimeHistory, allocator_);
  lua_memory_samples_ =
      allocator_->New<CircularBuffer<float>>(kFrameTimeHistory, allocator_);
  breakdown_history_ = allocator_->New<CircularBuffer<FrameBreakdown>>(
      kFrameTimeHistory, allocator_);
  repl_entries_ =
      allocator_->New<CircularBuffer<ReplEntry>>(kMaxReplEntries, allocator_);
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
  const char* text = "";
  if (ui->eval_history_pos_ >= 0) {
    text = ui->eval_history_entries_[ui->eval_history_pos_ % kEvalHistoryMax];
  }
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

bool DebugUI::NeedsImGuiFrame() const {
  return visible_ || mini_hud_visible_ || dropdown_repl_visible_ ||
         physics_debug_draw_;
}

void DebugUI::BeginFrame() {
  if (!initialized_ || !NeedsImGuiFrame()) return;
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void DebugUI::EndFrame() {
  if (!initialized_ || !NeedsImGuiFrame()) return;
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void DebugUI::Toggle() { visible_ = !visible_; }
void DebugUI::ToggleMiniHud() { mini_hud_visible_ = !mini_hud_visible_; }
void DebugUI::ToggleDropDownRepl() {
  dropdown_repl_visible_ = !dropdown_repl_visible_;
}

bool DebugUI::WantCaptureMouse() const {
  if (!initialized_ || !NeedsImGuiFrame()) return false;
  return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::WantCaptureKeyboard() const {
  if (!initialized_ || !NeedsImGuiFrame()) return false;
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
  CopyToBuffer(entry.text, sizeof(entry.text), message);
  log_entries_->Push(entry);
  if (log_auto_scroll_) log_scroll_to_bottom_ = true;
}

// Panel implementations (unity build includes).
#include "debug_ui_assets.inc.cc"
#include "debug_ui_docs.inc.cc"
#include "debug_ui_inspector.inc.cc"
#include "debug_ui_log.inc.cc"
#include "debug_ui_panels.inc.cc"
#include "debug_ui_performance.inc.cc"
#include "debug_ui_save.inc.cc"
#include "debug_ui_watch.inc.cc"
#include "debug_ui_zones.inc.cc"

// Window resize presets shared by menu and F7 shortcut.
struct WindowPreset {
  const char* label;
  int w, h;
};

constexpr WindowPreset kWindowPresets[] = {
    {"800x600", 800, 600},     {"1280x720", 1280, 720},
    {"1440x900", 1440, 900},   {"1920x1080", 1920, 1080},
    {"2560x1440", 2560, 1440},
};

constexpr int kWindowPresetCount =
    sizeof(kWindowPresets) / sizeof(kWindowPresets[0]);

void DebugUI::ResizeWindow(int w, int h) {
  if (window_ == nullptr) return;
  if (!resize_viewport_) {
    suppress_viewport_resize_ = true;
  }
  SDL_SetWindowSize(window_, w, h);
  if (resize_viewport_ && engine_ != nullptr) {
    engine_->batch_renderer.SetViewport(IVec2(w, h));
  }
  if (window_centered_) {
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
  }
}

bool DebugUI::ConsumeSuppressViewportResize() {
  bool r = suppress_viewport_resize_;
  suppress_viewport_resize_ = false;
  return r;
}

// Menu bar and dispatch.

void DebugUI::DrawMenuBar(const FrameContext& /*ctx*/) {
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
      PanelMenuItem("Network", kPanelNetwork);
      PanelMenuItem("Save Data", kPanelSave);
      PanelMenuItem("Assets", kPanelAssets);
      PanelMenuItem("API Docs", kPanelDocs);
      PanelMenuItem("Watch", kPanelWatch);
      PanelMenuItem("Hot Zones", kPanelZones);
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
#ifdef GAME_WITH_PROFILING
      {
        Profiler* p = GetProfiler();
        bool recording = p->recording();
        if (ImGui::MenuItem(recording ? "Stop Profiling (F11)"
                                      : "Start Profiling (F11)")) {
          p->ToggleRecording();
        }
      }
#endif
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) quit_requested_ = true;
      ImGui::EndMenu();
    }
    if (window_menu_requested_) {
      ImGui::OpenPopup("Window");
      window_menu_requested_ = false;
    }
    if (window_ != nullptr && ImGui::BeginMenu("Window")) {
      int w = 0, h = 0;
      SDL_GetWindowSize(window_, &w, &h);
      ImGui::Text("Current: %dx%d", w, h);
      ImGui::Separator();
      for (int i = 0; i < kWindowPresetCount; ++i) {
        bool current = (w == kWindowPresets[i].w && h == kWindowPresets[i].h);
        if (ImGui::MenuItem(kWindowPresets[i].label, nullptr, current)) {
          ResizeWindow(kWindowPresets[i].w, kWindowPresets[i].h);
        }
      }
      ImGui::Separator();
      ImGui::MenuItem("Resize viewport", nullptr, &resize_viewport_);
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

void DebugUI::DrawMiniHud(const FrameContext& ctx) {
  int win_w = 0;
  SDL_GetWindowSize(window_, &win_w, nullptr);
  float hud_x = static_cast<float>(win_w) - 155.0f;
  ImGui::SetNextWindowPos(ImVec2(hud_x, 5.0f));
  ImGui::SetNextWindowBgAlpha(0.5f);
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("##MiniHud", nullptr, flags)) {
    float last_ms = 0.0f;
    if (frame_times_ != nullptr && frame_times_->size() > 0) {
      last_ms = (*frame_times_)[frame_times_->size() - 1];
    }
    float fps = 0.0f;
    if (last_ms > 0.0f) {
      fps = 1000.0f / last_ms;
    }
    ImGui::Text("%.0f FPS", static_cast<double>(fps));
    ImGui::Text("%.1f ms", static_cast<double>(last_ms));
    ImGui::Text("%d draws", ctx.frame_stats.draw_calls);
    ImGui::Text("%.0f KB Lua", static_cast<double>(ctx.lua_memory_kb));
  }
  ImGui::End();
}

void DebugUI::DrawDropDownRepl() {
  if (!dropdown_editor_init_) {
    dropdown_editor_.SetLanguage(TextEditor::Language::Lua());
    dropdown_editor_.SetShowLineNumbersEnabled(false);
    dropdown_editor_.SetTabSize(2);
    dropdown_editor_.SetPalette(TextEditor::GetDarkPalette());
    dropdown_editor_init_ = true;
  }

  int win_w = 0, win_h = 0;
  SDL_GetWindowSize(window_, &win_w, &win_h);
  float repl_height = static_cast<float>(win_h) * 0.4f;

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(static_cast<float>(win_w), repl_height));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.1f, 0.92f));

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoCollapse;
  if (ImGui::Begin("##DropDownRepl", nullptr, flags)) {
    // Header.
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "REPL");
    ImGui::SameLine();
    const char* lang_label = "Lua";
    if (repl_lang_ == kFennel) lang_label = "Fennel";
    if (repl_lang_ == kSql) lang_label = "SQL";
    if (ImGui::SmallButton(lang_label)) {
      if (repl_lang_ == kLua) {
        repl_lang_ = kFennel;
      } else if (repl_lang_ == kFennel) {
        repl_lang_ = kSql;
      } else {
        repl_lang_ = kLua;
      }
      const TextEditor::Language* editor_lang = TextEditor::Language::Lua();
      if (repl_lang_ == kFennel) editor_lang = FennelLanguage();
      if (repl_lang_ == kSql) editor_lang = TextEditor::Language::Sql();
      dropdown_editor_.SetLanguage(editor_lang);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Run") || (ImGui::IsKeyDown(ImGuiMod_Ctrl) &&
                                      ImGui::IsKeyPressed(ImGuiKey_Enter))) {
      std::string code = dropdown_editor_.GetText();
      while (!code.empty() && (code.back() == '\n' || code.back() == '\r' ||
                               code.back() == ' ')) {
        code.pop_back();
      }
      if (!code.empty()) {
        EvalReplCode(code);
        dropdown_editor_.ClearText();
      }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
      while (!repl_entries_->empty()) repl_entries_->Pop();
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       "Ctrl+Enter to run | ` to close");

    ImGui::Separator();

    // Output area.
    float editor_height = ImGui::GetTextLineHeight() * 7;
    float output_height = ImGui::GetContentRegionAvail().y - editor_height -
                          ImGui::GetStyle().ItemSpacing.y;
    if (output_height < 40.0f) output_height = 40.0f;
    if (ImGui::BeginChild("##dd_output", ImVec2(0, output_height), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
      size_t count = repl_entries_->size();
      for (size_t i = 0; i < count; ++i) {
        const auto& entry = (*repl_entries_)[i];
        if (entry.is_input) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        } else if (entry.is_error) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        }
        ImGui::TextUnformatted(entry.text);
        if (entry.is_input || entry.is_error) ImGui::PopStyleColor();
      }
      if (repl_scroll_to_bottom_) {
        ImGui::SetScrollHereY(1.0f);
        repl_scroll_to_bottom_ = false;
      }
    }
    ImGui::EndChild();

    // Editor.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    dropdown_editor_.Render("##dd_editor",
                            ImVec2(0, ImGui::GetContentRegionAvail().y));
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void DebugUI::DrawPhysicsDebug() {
  if (!physics_debug_draw_ || !engine_->physics.debug_draw_enabled()) {
    // Clean up if debug draw was disabled while dragging.
    if (mouse_joint_ != nullptr) {
      engine_->physics.DestroyMouseJoint(mouse_joint_);
      mouse_joint_ = nullptr;
    }
    selected_body_ = nullptr;
    return;
  }

  IVec2 vp = engine_->batch_renderer.GetViewport();
  FVec2 viewport(vp.x, vp.y);
  engine_->physics.DrawDebug(&engine_->camera, viewport);

  // Convert Box2D position to screen pixels, accounting for whether the
  // game uses the camera system.
  float ppm = engine_->physics.GetPixelsPerMeter();
  bool use_camera = engine_->camera.IsFollowing() ||
                    engine_->camera.GetPosition() != FVec2::Zero();
  auto to_screen = [&](b2Vec2 p) -> ImVec2 {
    FVec2 world_px(p.x * ppm, p.y * ppm);
    if (use_camera) {
      FVec2 s = engine_->camera.ToScreen(world_px, viewport);
      return ImVec2(s.x, s.y);
    }
    return ImVec2(world_px.x, world_px.y);
  };
  auto to_world = [&](ImVec2 s) -> FVec2 {
    if (use_camera) {
      return engine_->camera.ToWorld(FVec(s.x, s.y), viewport);
    }
    return FVec(s.x, s.y);
  };

  // Velocity arrows.
  if (show_velocities_) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    for (const b2Body* body = engine_->physics.GetBodyList(); body != nullptr;
         body = body->GetNext()) {
      if (body->GetType() != b2_dynamicBody || !body->IsAwake()) continue;
      b2Vec2 pos = body->GetPosition();
      b2Vec2 vel = body->GetLinearVelocity();
      float speed = vel.Length();
      if (speed < 0.1f) continue;
      float arrow_len = speed * ppm * 0.3f;
      if (arrow_len > 100.0f) arrow_len = 100.0f;
      b2Vec2 dir(vel.x / speed, vel.y / speed);
      b2Vec2 tip = pos + (arrow_len / ppm) * dir;
      dl->AddLine(to_screen(pos), to_screen(tip), IM_COL32(0, 255, 255, 200),
                  2.0f);
    }
  }

  // Selected body highlight.
  if (selected_body_ != nullptr) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddCircle(to_screen(selected_body_->GetPosition()), 20.0f,
                  IM_COL32(255, 255, 0, 255), 0, 3.0f);
  }

  // Click-to-select and drag.
  if (!ImGui::GetIO().WantCaptureMouse) {
    ImVec2 mouse = ImGui::GetMousePos();
    FVec2 world_pos = to_world(mouse);
    if (ImGui::IsMouseClicked(0)) {
      b2Body* body = engine_->physics.QueryPoint(world_pos);
      selected_body_ = body;
      if (body != nullptr && body->GetType() == b2_dynamicBody) {
        mouse_joint_ = engine_->physics.CreateMouseJoint(body, world_pos);
      }
    }
    if (mouse_joint_ != nullptr && ImGui::IsMouseDown(0)) {
      engine_->physics.UpdateMouseJoint(mouse_joint_, world_pos);
    }
    if (mouse_joint_ != nullptr && ImGui::IsMouseReleased(0)) {
      engine_->physics.DestroyMouseJoint(mouse_joint_);
      mouse_joint_ = nullptr;
    }
  }
}

void DebugUI::DrawAll(const FrameContext& ctx) {
  if (!initialized_ || engine_ == nullptr) return;
  // Independent overlays render without the full debug UI.
  if (mini_hud_visible_) DrawMiniHud(ctx);
  if (dropdown_repl_visible_) DrawDropDownRepl();
  DrawPhysicsDebug();
  if (!visible_) return;
  DrawMenuBar(ctx);
  if (PanelOpen(kPanelPerformance)) DrawPerformancePanel(ctx);
  if (PanelOpen(kPanelLogConsole)) DrawLogConsole();
  if (PanelOpen(kPanelEntityInspector)) DrawEntityInspector();
  if (PanelOpen(kPanelAudio)) DrawAudioPanel();
  if (PanelOpen(kPanelMemory)) DrawMemoryPanel(ctx.lua_memory_bytes);
  if (PanelOpen(kPanelRenderer)) DrawRendererPanel(ctx);
  if (PanelOpen(kPanelCamera)) DrawCameraPanel();
  if (PanelOpen(kPanelPhysics)) DrawPhysicsPanel();
  if (PanelOpen(kPanelNetwork)) DrawNetworkPanel();
  if (PanelOpen(kPanelSave)) DrawSavePanel();
  if (PanelOpen(kPanelAssets)) DrawAssetViewer();
  if (PanelOpen(kPanelDocs)) DrawDocsPanel();
  if (PanelOpen(kPanelWatch)) DrawWatchPanel();
  if (PanelOpen(kPanelZones)) DrawZonesPanel();
  if (PanelOpen(kPanelSelector)) DrawPanelSelector();
  if (zoom_texture_ != 0) DrawTextureZoom();
}

void DebugUI::DrawPanelSelector() {
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
    PanelMenuItem("Hot Zones", kPanelZones);
    PanelMenuItem("Save Data", kPanelSave);
  }
  ImGui::End();
  if (!open) TogglePanel(kPanelSelector);
}

void DebugUI::DrawTextureZoom() {
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
      if (zoom_tex_w_ > 0 && zoom_tex_h_ > 0) {
        zoom_level_ = fmin(avail_w / zoom_tex_w_, avail_h / zoom_tex_h_);
      } else {
        zoom_level_ = 1.0f;
      }
    }
    if (ImGui::BeginChild("ZoomRegion", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
      float w = zoom_tex_w_ * zoom_level_;
      float h = zoom_tex_h_ * zoom_level_;
      ImVec2 img_pos = ImGui::GetCursorScreenPos();
      ImGui::Image(
          static_cast<ImTextureID>(static_cast<uintptr_t>(zoom_texture_)),
          ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
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
          ImGui::ColorButton("##pixel", color, ImGuiColorEditFlags_AlphaPreview,
                             ImVec2(32, 32));
          ImGui::SameLine();
          ImGui::Text("(%d, %d)\n#%02X%02X%02X%02X\nRGBA: %d %d %d %d", px, py,
                      zoom_pixels_[offset], zoom_pixels_[offset + 1],
                      zoom_pixels_[offset + 2], zoom_pixels_[offset + 3],
                      zoom_pixels_[offset], zoom_pixels_[offset + 1],
                      zoom_pixels_[offset + 2], zoom_pixels_[offset + 3]);
          ImGui::EndTooltip();
        }
      }
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
    case SDL_SCANCODE_F7:
      window_menu_requested_ = true;
      break;
    default:
      break;
  }
}

}  // namespace G

#endif  // GAME_WITH_IMGUI
