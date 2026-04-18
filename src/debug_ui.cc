#include "debug_ui.h"

#ifdef GAME_WITH_IMGUI

#include <cstring>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include "libraries/sqlite3.h"
#include "lua.h"
#include "string_table.h"

namespace G {

namespace {

// Global pointer used by the LogSink callback to route messages to the
// DebugUI ring buffer. Only one DebugUI instance exists at a time.
DebugUI* g_debug_ui = nullptr;

// The original log sink installed before we intercept.
LogSink g_original_sink = nullptr;

// Log sink that forwards to both the original sink and the DebugUI buffer.
void DebugUILogSink(LogLevel level, const char* message) {
  if (g_original_sink) g_original_sink(level, message);
  if (g_debug_ui) g_debug_ui->LogMessage(level, message);
}

// Routes ImGui allocations through the engine's SystemAllocator. ImGui
// calls free() without a size, so we must use an allocator that doesn't
// need the size (SystemAllocator wraps malloc/free).
void* ImGuiAllocFunc(size_t size, void* /*user_data*/) {
  return SystemAllocator::Instance()->Alloc(size, /*align=*/16);
}

void ImGuiFreeFunc(void* ptr, void* /*user_data*/) {
  SystemAllocator::Instance()->Dealloc(ptr, /*sz=*/0);
}

// Returns an ImVec4 color for a given log level.
ImVec4 LogLevelColor(LogLevel level) {
  switch (level) {
    case LogLevel::kFatal:
      return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    case LogLevel::kError:
      return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    case LogLevel::kWarn:
      return ImVec4(1.0f, 0.9f, 0.3f, 1.0f);
    case LogLevel::kInfo:
      return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    case LogLevel::kDebug:
      return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    case LogLevel::kTrace:
      return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
  }
  return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

}  // namespace

void DebugUI::StartLogCapture(Allocator* allocator) {
  allocator_ = allocator;

  // Allocate the log entry ring buffer and intercept the engine log sink
  // immediately so that startup messages are not lost.
  log_entries_ =
      allocator_->New<CircularBuffer<LogEntry>>(kMaxLogEntries, allocator_);
  g_original_sink = GetLogSink();
  g_debug_ui = this;
  SetLogSink(DebugUILogSink);
}

void DebugUI::Init(SDL_Window* window, SDL_GLContext gl_context) {
  window_ = window;

  // Route ImGui's internal allocations through the engine allocator.
  ImGui::SetAllocatorFunctions(ImGuiAllocFunc, ImGuiFreeFunc, nullptr);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // Disable imgui.ini persistence — debug UI state is transient.
  io.IniFilename = nullptr;

  // Dark theme with semi-transparent backgrounds.
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.FrameRounding = 2.0f;
  style.Colors[ImGuiCol_WindowBg].w = 0.85f;

  ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init(/*glsl_version=*/"#version 150");

  // Allocate circular buffers for performance history.
  frame_times_ = allocator_->New<CircularBuffer<float>>(kFrameTimeHistory,
                                                        allocator_);
  lua_memory_samples_ = allocator_->New<CircularBuffer<float>>(
      kFrameTimeHistory, allocator_);

  initialized_ = true;
  LOG("Debug UI initialized (Dear ImGui ", IMGUI_VERSION, ")");
}

void DebugUI::Shutdown() {
  if (!initialized_) return;
  // Restore the original log sink before tearing down.
  SetLogSink(g_original_sink);
  g_debug_ui = nullptr;
  g_original_sink = nullptr;
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  initialized_ = false;
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

void DebugUI::DrawPerformancePanel(const FrameContext& ctx) {
  if (!initialized_ || !visible_) return;

  const auto& fs = ctx.frame_stats;
  float lua_memory_kb = ctx.lua_memory_kb;
  size_t cmd_buf_used = ctx.cmd_buf_used;
  size_t cmd_buf_capacity = ctx.cmd_buf_capacity;

  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380, 400), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Performance", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // FPS counter.
  const size_t count = frame_times_->size();
  if (count > 0) {
    float sum = 0.0f;
    float min_val = 1e9f;
    float max_val = 0.0f;
    for (size_t i = 0; i < count; ++i) {
      float v = (*frame_times_)[i];
      sum += v;
      if (v < min_val) min_val = v;
      if (v > max_val) max_val = v;
    }
    float avg = sum / static_cast<float>(count);
    float fps = (avg > 0.0f) ? 1000.0f / avg : 0.0f;

    ImGui::Text("FPS: %.1f", static_cast<double>(fps));
    ImGui::Separator();

    // Frame time graph. We need a contiguous array for PlotLines, so
    // copy from the circular buffer into a stack array.
    float values[kFrameTimeHistory];
    for (size_t i = 0; i < count; ++i) {
      values[i] = (*frame_times_)[i];
    }

    char overlay[64];
    snprintf(overlay, sizeof(overlay), "min %.1f  avg %.1f  max %.1f ms",
             static_cast<double>(min_val), static_cast<double>(avg),
             static_cast<double>(max_val));

    ImGui::PlotLines("Frame Time", values, static_cast<int>(count),
                     /*values_offset=*/0, overlay, /*scale_min=*/0.0f,
                     /*scale_max=*/max_val * 1.5f, ImVec2(0, 80));
  }

  ImGui::Separator();

  // Draw call breakdown.
  if (ImGui::CollapsingHeader("Draw Calls", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Draw calls: %d", fs.draw_calls);
    ImGui::Text("Vertices:   %d", fs.vertices);
    ImGui::Text("Commands:   %d", fs.commands);

    if (ImGui::TreeNode("Flush Reasons")) {
      ImGui::Text("Texture:   %d", fs.flush_texture);
      ImGui::Text("Transform: %d", fs.flush_transform);
      ImGui::Text("Shader:    %d", fs.flush_shader);
      ImGui::Text("Blend:     %d", fs.flush_blend);
      ImGui::Text("Canvas:    %d", fs.flush_canvas);
      ImGui::Text("Line end:  %d", fs.flush_line_end);
      ImGui::Text("Other:     %d", fs.flush_other);
      if (fs.flush_overflow > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           "Overflow:  %d", fs.flush_overflow);
      }
      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Redundant Skips")) {
      ImGui::Text("Texture:    %d", fs.redundant_texture);
      ImGui::Text("Transform:  %d", fs.redundant_transform);
      ImGui::Text("Shader:     %d", fs.redundant_shader);
      ImGui::Text("Blend:      %d", fs.redundant_blend);
      ImGui::Text("Line width: %d", fs.redundant_line_width);
      ImGui::Text("SDF outline:%d", fs.redundant_sdf_outline);
      ImGui::TreePop();
    }
  }

  ImGui::Separator();

  // Lua memory graph.
  {
    const size_t lua_count = lua_memory_samples_->size();
    if (lua_count > 0) {
      float lua_min = 1e9f;
      float lua_max = 0.0f;
      float lua_values[kFrameTimeHistory];
      for (size_t i = 0; i < lua_count; ++i) {
        float v = (*lua_memory_samples_)[i];
        lua_values[i] = v;
        if (v < lua_min) lua_min = v;
        if (v > lua_max) lua_max = v;
      }
      float lua_cur = lua_values[lua_count - 1];

      char lua_overlay[64];
      snprintf(lua_overlay, sizeof(lua_overlay), "%.1f KB (min %.1f  max %.1f)",
               static_cast<double>(lua_cur), static_cast<double>(lua_min),
               static_cast<double>(lua_max));

      ImGui::PlotLines("Lua Memory", lua_values, static_cast<int>(lua_count),
                       /*values_offset=*/0, lua_overlay, /*scale_min=*/0.0f,
                       /*scale_max=*/lua_max * 1.5f, ImVec2(0, 60));
    } else {
      ImGui::Text("Lua memory: %.1f KB", static_cast<double>(lua_memory_kb));
    }
  }

  ImGui::Separator();

  // Command buffer fill from the batch renderer.
  float used_mb =
      static_cast<float>(cmd_buf_used) / (1024.0f * 1024.0f);
  float total_mb =
      static_cast<float>(cmd_buf_capacity) / (1024.0f * 1024.0f);
  float fill_ratio = (cmd_buf_capacity > 0)
                         ? static_cast<float>(cmd_buf_used) /
                               static_cast<float>(cmd_buf_capacity)
                         : 0.0f;
  if (fill_ratio > 1.0f) fill_ratio = 1.0f;

  // Color the progress bar based on fill level.
  ImVec4 bar_color;
  if (fill_ratio < 0.5f) {
    bar_color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);  // Green.
  } else if (fill_ratio < 0.8f) {
    bar_color = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);  // Yellow.
  } else {
    bar_color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);  // Red.
  }

  char cmd_buf_label[64];
  snprintf(cmd_buf_label, sizeof(cmd_buf_label), "%.2f / %.0f MB",
           static_cast<double>(used_mb), static_cast<double>(total_mb));

  ImGui::Text("Command Buffer");
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_color);
  ImGui::ProgressBar(fill_ratio, ImVec2(-1, 0), cmd_buf_label);
  ImGui::PopStyleColor();

  ImGui::Separator();

  // Window size controls.
  if (window_ != nullptr &&
      ImGui::CollapsingHeader("Window", ImGuiTreeNodeFlags_DefaultOpen)) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);

    // Preset buttons.
    struct Preset {
      const char* label;
      int w, h;
    };
    const Preset presets[] = {
        {"800x600", 800, 600},     {"1280x720", 1280, 720},
        {"1440x900", 1440, 900},   {"1920x1080", 1920, 1080},
        {"2560x1440", 2560, 1440},
    };
    // Helper lambda: resize the SDL window and update the engine viewport.
    auto ResizeWindow = [&](int new_w, int new_h) {
      SDL_SetWindowSize(window_, new_w, new_h);
      if (batch_renderer_ != nullptr) {
        batch_renderer_->SetViewport(IVec2(new_w, new_h));
      }
    };

    for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); ++i) {
      if (i > 0) ImGui::SameLine();
      if (ImGui::Button(presets[i].label)) {
        ResizeWindow(presets[i].w, presets[i].h);
      }
    }

    // Custom size inputs.
    static int custom_w = 0, custom_h = 0;
    if (custom_w == 0) {
      custom_w = w;
      custom_h = h;
    }
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("##cw", &custom_w, 0);
    ImGui::SameLine();
    ImGui::Text("x");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("##ch", &custom_h, 0);
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
      if (custom_w > 0 && custom_h > 0) {
        ResizeWindow(custom_w, custom_h);
      }
    }

    ImGui::Text("Current: %dx%d", w, h);
  }

  ImGui::End();
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

void DebugUI::DrawLogConsole() {
  if (!initialized_ || !visible_) return;

  ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Log Console", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // Toolbar: level filters, text filter, clear, copy, auto-scroll.
  if (ImGui::Button("Clear")) {
    while (!log_entries_->empty()) log_entries_->Pop();
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy")) {
    // Build a string of all visible (filtered) log lines.
    FixedStringBuffer<kMaxLogLineLength> line(kTruncating);
    bool has_filter = log_text_filter_[0] != '\0';
    // Estimate: use a dynamic SDL clipboard set. Build into a temporary
    // buffer on the stack, capped at a reasonable size.
    enum { kClipBufSize = 64 * 1024 };
    char clip[kClipBufSize];
    size_t pos = 0;
    size_t count = log_entries_->size();
    for (size_t i = 0; i < count && pos < kClipBufSize - 1; ++i) {
      const LogEntry& entry = (*log_entries_)[i];
      int level_idx = static_cast<int>(entry.level);
      if (level_idx < 0 || level_idx > 5) continue;
      if (!log_level_filter_[level_idx]) continue;
      if (has_filter && strstr(entry.text, log_text_filter_) == nullptr) {
        continue;
      }
      size_t len = strlen(entry.text);
      if (pos + len + 1 >= kClipBufSize) break;
      memcpy(clip + pos, entry.text, len);
      pos += len;
      clip[pos++] = '\n';
    }
    clip[pos] = '\0';
    SDL_SetClipboardText(clip);
  }
  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &log_auto_scroll_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##filter", "Filter...", log_text_filter_,
                           sizeof(log_text_filter_));

  // Level filter toggles.
  const char* level_names[] = {"Fatal", "Error", "Warn", "Info", "Debug",
                               "Trace"};
  for (int i = 0; i < 6; ++i) {
    if (i > 0) ImGui::SameLine();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        LogLevelColor(static_cast<LogLevel>(i)));
    ImGui::Checkbox(level_names[i], &log_level_filter_[i]);
    ImGui::PopStyleColor();
  }

  ImGui::Separator();

  // Scrollable log region — leave room for the eval input at the bottom.
  float footer_height =
      ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  if (ImGui::BeginChild("LogScrollRegion", ImVec2(0, -footer_height), false,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    bool has_text_filter = log_text_filter_[0] != '\0';
    size_t count = log_entries_->size();
    for (size_t i = 0; i < count; ++i) {
      const LogEntry& entry = (*log_entries_)[i];
      int level_idx = static_cast<int>(entry.level);
      if (level_idx < 0 || level_idx > 5) continue;
      if (!log_level_filter_[level_idx]) continue;
      if (has_text_filter && strstr(entry.text, log_text_filter_) == nullptr) {
        continue;
      }
      ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(entry.level));
      ImGui::TextUnformatted(entry.text);
      ImGui::PopStyleColor();
    }
    if (log_scroll_to_bottom_) {
      ImGui::SetScrollHereY(1.0f);
      log_scroll_to_bottom_ = false;
    }
  }
  ImGui::EndChild();

  // Lua eval input.
  ImGui::Separator();
  bool reclaim_focus = false;
  ImGuiInputTextFlags input_flags =
      ImGuiInputTextFlags_EnterReturnsTrue |
      ImGuiInputTextFlags_EscapeClearsAll |
      ImGuiInputTextFlags_CallbackHistory;

  // Simple history ring for eval input.
  struct EvalHistory {
    enum { kMax = 64 };
    char entries[kMax][kEvalInputSize] = {};
    int count = 0;
    int pos = -1;

    void Add(const char* str) {
      // Don't add duplicates of the most recent entry.
      if (count > 0) {
        int last = (count - 1) % kMax;
        if (strcmp(entries[last], str) == 0) {
          pos = -1;
          return;
        }
      }
      int idx = count % kMax;
      snprintf(entries[idx], kEvalInputSize, "%s", str);
      count++;
      pos = -1;
    }

    static int Callback(ImGuiInputTextCallbackData* data) {
      auto* history = static_cast<EvalHistory*>(data->UserData);
      if (history->count == 0) return 0;
      if (data->EventKey == ImGuiKey_UpArrow) {
        if (history->pos == -1) {
          history->pos = history->count - 1;
        } else if (history->pos > 0 &&
                   history->pos > history->count - (int)kMax) {
          history->pos--;
        }
      } else if (data->EventKey == ImGuiKey_DownArrow) {
        if (history->pos != -1) {
          history->pos++;
          if (history->pos >= history->count) history->pos = -1;
        }
      }
      const char* text =
          (history->pos >= 0) ? history->entries[history->pos % kMax] : "";
      data->DeleteChars(0, data->BufTextLen);
      data->InsertChars(0, text);
      return 0;
    }
  };

  static EvalHistory history;

  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##eval", eval_input_, kEvalInputSize, input_flags,
                       EvalHistory::Callback, &history)) {
    if (eval_input_[0] != '\0') {
      // Log the input.
      FixedStringBuffer<kMaxLogLineLength> echo(kTruncating);
      echo.Append("> ", eval_input_);
      LogMessage(LogLevel::kInfo, echo.str());

      history.Add(eval_input_);

      // Evaluate.
      if (lua_ != nullptr) {
        FixedStringBuffer<kMaxLogLineLength> result(kTruncating);
        bool ok =
            lua_->EvalString(eval_input_, &result);
        if (result.view().size() > 0) {
          LogMessage(ok ? LogLevel::kInfo : LogLevel::kError, result.str());
        }
      } else {
        LogMessage(LogLevel::kWarn, "No Lua VM available");
      }
      eval_input_[0] = '\0';
    }
    reclaim_focus = true;
  }

  // Auto-focus the input field.
  ImGui::SetItemDefaultFocus();
  if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);

  ImGui::End();
}

void DebugUI::DrawAudioPanel() {
  if (!initialized_ || !visible_ || sound_ == nullptr) return;

  ImGui::SetNextWindowPos(ImVec2(620, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 350), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Audio", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // Global volume slider.
  float global_gain = sound_->global_gain();
  if (ImGui::SliderFloat("Global Volume", &global_gain, 0.0f, 1.0f,
                          "%.2f")) {
    sound_->SetGlobalGain(global_gain);
  }

  ImGui::Separator();

  // Stream slot usage.
  size_t used = sound_->stream_count();
  size_t total = sound_->max_streams();
  ImGui::Text("Stream Slots: %zu / %zu", used, total);
  float slot_ratio = (total > 0)
                         ? static_cast<float>(used) / static_cast<float>(total)
                         : 0.0f;
  ImGui::ProgressBar(slot_ratio, ImVec2(-1, 0));

  ImGui::Separator();

  // Active streams table.
  if (used > 0 &&
      ImGui::BeginTable("Streams", 7,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollY,
                        ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableSetupColumn("Vol", ImGuiTableColumnFlags_WidthFixed, 40);
    ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, 40);
    ImGui::TableSetupColumn("Pan", ImGuiTableColumnFlags_WidthFixed, 40);
    ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed, 35);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableHeadersRow();

    Sound::StreamDebugInfo infos[128];
    sound_->GetStreamDebugInfo(infos, 128);
    for (size_t i = 0; i < used; ++i) {
      const auto& info = infos[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      std::string_view name = StringByHandle(info.handle);
      ImGui::TextUnformatted(name.data(), name.data() + name.size());

      ImGui::TableNextColumn();
      if (info.playing) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Playing");
      } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Stopped");
      }

      ImGui::TableNextColumn();
      ImGui::Text("%.2f", static_cast<double>(info.gain));

      ImGui::TableNextColumn();
      ImGui::Text("%.2f", static_cast<double>(info.pitch));

      ImGui::TableNextColumn();
      ImGui::Text("%.2f", static_cast<double>(info.pan));

      ImGui::TableNextColumn();
      ImGui::Text("%s", info.loop ? "Yes" : "No");

      ImGui::TableNextColumn();
      ImGui::Text("%s", info.managed ? "Managed" : "Auto");
    }
    ImGui::EndTable();
  }

  ImGui::End();
}

namespace {

// Formats a byte count into a human-readable string (KB or MB).
void FormatBytes(char* buf, size_t buf_size, size_t bytes) {
  if (bytes >= 1024 * 1024) {
    snprintf(buf, buf_size, "%.1f MB",
             static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else {
    snprintf(buf, buf_size, "%.1f KB",
             static_cast<double>(bytes) / 1024.0);
  }
}

// Draws a labeled memory bar with used/total.
void DrawMemoryBar(const char* label, size_t used, size_t total) {
  float ratio = (total > 0)
                    ? static_cast<float>(used) / static_cast<float>(total)
                    : 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;

  ImVec4 bar_color;
  if (ratio < 0.5f) {
    bar_color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
  } else if (ratio < 0.8f) {
    bar_color = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
  } else {
    bar_color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
  }

  char used_str[32], total_str[32], overlay[80];
  FormatBytes(used_str, sizeof(used_str), used);
  FormatBytes(total_str, sizeof(total_str), total);
  snprintf(overlay, sizeof(overlay), "%s / %s", used_str, total_str);

  ImGui::Text("%s", label);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_color);
  ImGui::ProgressBar(ratio, ImVec2(-1, 0), overlay);
  ImGui::PopStyleColor();
}

}  // namespace

void DebugUI::DrawMemoryPanel(size_t lua_memory_bytes) {
  if (!initialized_ || !visible_) return;

  ImGui::SetNextWindowPos(ImVec2(620, 370), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380, 280), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Memory", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // Engine arena allocator.
  if (engine_arena_ != nullptr &&
      ImGui::CollapsingHeader("Engine Arena",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawMemoryBar("Used / Total", engine_arena_->used_memory(),
                  engine_arena_->total_memory());
    ImGui::Text("%.1f%% used",
                static_cast<double>(engine_arena_->used_memory()) /
                    static_cast<double>(engine_arena_->total_memory()) * 100.0);
  }

  ImGui::Separator();

  // Frame allocator (reset each frame).
  if (frame_arena_ != nullptr &&
      ImGui::CollapsingHeader("Frame Allocator",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawMemoryBar("Used / Total", frame_arena_->used_memory(),
                  frame_arena_->total_memory());
    ImGui::Text("%.1f%% used (resets each frame)",
                static_cast<double>(frame_arena_->used_memory()) /
                    static_cast<double>(frame_arena_->total_memory()) * 100.0);
  }

  ImGui::Separator();

  // Lua heap.
  if (ImGui::CollapsingHeader("Lua Heap", ImGuiTreeNodeFlags_DefaultOpen)) {
    char lua_str[32];
    FormatBytes(lua_str, sizeof(lua_str), lua_memory_bytes);
    ImGui::Text("Lua memory: %s", lua_str);

    // Sparkline from the existing lua_memory_samples_ buffer.
    const size_t lua_count = lua_memory_samples_->size();
    if (lua_count > 0) {
      float lua_values[kFrameTimeHistory];
      float lua_max = 0.0f;
      for (size_t i = 0; i < lua_count; ++i) {
        float v = (*lua_memory_samples_)[i];
        lua_values[i] = v;
        if (v > lua_max) lua_max = v;
      }
      ImGui::PlotLines("##lua_mem_sparkline", lua_values,
                        static_cast<int>(lua_count), /*values_offset=*/0,
                        /*overlay_text=*/nullptr, /*scale_min=*/0.0f,
                        /*scale_max=*/lua_max * 1.5f, ImVec2(0, 40));
    }
  }

  ImGui::Separator();

  // String table stats.
  if (ImGui::CollapsingHeader("String Table")) {
    auto st_stats = StringTable::Instance().stats();
    ImGui::Text("Interned strings: %d / %d", st_stats.strings_used,
                st_stats.total_strings);
    ImGui::Text("Buffer used: %d / %d bytes", st_stats.space_used,
                st_stats.total_space);
    float st_ratio = static_cast<float>(st_stats.space_used) /
                     static_cast<float>(st_stats.total_space);
    ImGui::ProgressBar(st_ratio, ImVec2(-1, 0));
  }

  ImGui::End();
}

namespace {

// Returns a display name for a blend mode enum value.
const char* BlendModeName(BlendMode mode) {
  switch (mode) {
    case BLEND_ALPHA:
      return "Alpha";
    case BLEND_ADD:
      return "Additive";
    case BLEND_MULTIPLY:
      return "Multiply";
    case BLEND_REPLACE:
      return "Replace";
    case BLEND_PREMULTIPLIED:
      return "Premultiplied";
  }
  return "Unknown";
}

}  // namespace

void DebugUI::DrawRendererPanel(const FrameContext& ctx) {
  if (!initialized_ || !visible_) return;

  const auto& fs = ctx.frame_stats;
  size_t cmd_buf_used = ctx.cmd_buf_used;
  size_t cmd_buf_capacity = ctx.cmd_buf_capacity;

  ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 450), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Renderer", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // Batch stats (detailed).
  if (ImGui::CollapsingHeader("Batch Stats",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Draw calls: %d", fs.draw_calls);
    ImGui::Text("Vertices:   %d", fs.vertices);
    ImGui::Text("Commands:   %d", fs.commands);

    ImGui::Separator();
    ImGui::Text("Flush Reasons:");
    if (ImGui::BeginTable("FlushReasons", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Reason");
      ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableHeadersRow();

      struct FlushEntry {
        const char* name;
        int count;
      };
      FlushEntry entries[] = {
          {"Texture", fs.flush_texture},
          {"Transform", fs.flush_transform},
          {"Shader", fs.flush_shader},
          {"Blend Mode", fs.flush_blend},
          {"Canvas", fs.flush_canvas},
          {"Line End", fs.flush_line_end},
          {"Overflow", fs.flush_overflow},
          {"Other", fs.flush_other},
      };
      for (const auto& e : entries) {
        if (e.count == 0) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (e.count == fs.flush_overflow && fs.flush_overflow > 0) {
          ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", e.name);
        } else {
          ImGui::Text("%s", e.name);
        }
        ImGui::TableNextColumn();
        ImGui::Text("%d", e.count);
      }
      ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("Redundant Skips:");
    int total_redundant = fs.redundant_texture + fs.redundant_transform +
                          fs.redundant_shader + fs.redundant_blend +
                          fs.redundant_line_width + fs.redundant_sdf_outline;
    ImGui::Text("Total: %d", total_redundant);
    if (total_redundant > 0 && ImGui::TreeNode("##redundant_detail")) {
      ImGui::Text("Texture:     %d", fs.redundant_texture);
      ImGui::Text("Transform:   %d", fs.redundant_transform);
      ImGui::Text("Shader:      %d", fs.redundant_shader);
      ImGui::Text("Blend:       %d", fs.redundant_blend);
      ImGui::Text("Line Width:  %d", fs.redundant_line_width);
      ImGui::Text("SDF Outline: %d", fs.redundant_sdf_outline);
      ImGui::TreePop();
    }

    // Command buffer fill.
    ImGui::Separator();
    float used_mb =
        static_cast<float>(cmd_buf_used) / (1024.0f * 1024.0f);
    float total_mb =
        static_cast<float>(cmd_buf_capacity) / (1024.0f * 1024.0f);
    float fill_ratio = (cmd_buf_capacity > 0)
                           ? static_cast<float>(cmd_buf_used) /
                                 static_cast<float>(cmd_buf_capacity)
                           : 0.0f;
    if (fill_ratio > 1.0f) fill_ratio = 1.0f;
    char cmd_label[64];
    snprintf(cmd_label, sizeof(cmd_label), "%.2f / %.0f MB",
             static_cast<double>(used_mb), static_cast<double>(total_mb));
    ImGui::Text("Command Buffer");
    ImVec4 bar_color;
    if (fill_ratio < 0.5f) {
      bar_color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
    } else if (fill_ratio < 0.8f) {
      bar_color = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
    } else {
      bar_color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_color);
    ImGui::ProgressBar(fill_ratio, ImVec2(-1, 0), cmd_label);
    ImGui::PopStyleColor();
  }

  ImGui::Separator();

  // Loaded textures.
  if (batch_renderer_ != nullptr &&
      ImGui::CollapsingHeader("Textures")) {
    ImGui::Text("Texture units: %zu", batch_renderer_->GetTextureCount());
  }

  // Loaded images.
  if (renderer_ != nullptr && ImGui::CollapsingHeader("Loaded Images")) {
    Slice<DbAssets::Image> images = renderer_->GetImages();
    ImGui::Text("Count: %zu", images.size());
    if (images.size() > 0 &&
        ImGui::BeginTable("Images", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 150))) {
      ImGui::TableSetupColumn("Name");
      ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableHeadersRow();
      for (size_t i = 0; i < images.size(); ++i) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(images[i].name.data(),
                               images[i].name.data() + images[i].name.size());
        ImGui::TableNextColumn();
        ImGui::Text("%zu", images[i].width);
        ImGui::TableNextColumn();
        ImGui::Text("%zu", images[i].height);
      }
      ImGui::EndTable();
    }
  }

  // Active shaders.
  if (shaders_ != nullptr && ImGui::CollapsingHeader("Shader Programs")) {
    shaders_->programs().ForEach(
        [](std::string_view name, const GLuint& handle) {
          ImGui::Text("%-30.*s  (GL %u)", static_cast<int>(name.size()),
                      name.data(), handle);
        });
  }

  // Current state readout.
  if (batch_renderer_ != nullptr &&
      ImGui::CollapsingHeader("Current State")) {
    ImGui::Text("Blend mode: %s",
                BlendModeName(batch_renderer_->GetCurrentBlendMode()));
    std::string_view shader_name =
        StringByHandle(batch_renderer_->GetCurrentShader());
    ImGui::Text("Shader: %.*s", static_cast<int>(shader_name.size()),
                shader_name.data());
    IVec2 vp = batch_renderer_->viewport();
    ImGui::Text("Viewport: %dx%d", vp.x, vp.y);
  }

  ImGui::End();
}

void DebugUI::DrawCameraPanel() {
  if (!initialized_ || !visible_ || camera_ == nullptr) return;

  ImGui::SetNextWindowPos(ImVec2(440, 420), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(340, 350), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Camera", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // Position.
  FVec2 pos = camera_->GetPosition();
  ImGui::Text("Position: (%.1f, %.1f)", static_cast<double>(pos.x),
              static_cast<double>(pos.y));

  // Zoom.
  float zoom = camera_->GetZoom();
  ImGui::Text("Zoom: %.3f", static_cast<double>(zoom));

  // Rotation.
  float rotation = camera_->GetRotation();
  ImGui::Text("Rotation: %.3f rad (%.1f deg)",
              static_cast<double>(rotation),
              static_cast<double>(rotation * 180.0f /
                                  static_cast<float>(M_PI)));

  ImGui::Separator();

  // Follow state.
  if (ImGui::CollapsingHeader("Follow", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (camera_->IsFollowing()) {
      FVec2 target = camera_->GetFollowTarget();
      ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Following");
      ImGui::Text("Target: (%.1f, %.1f)", static_cast<double>(target.x),
                  static_cast<double>(target.y));
      FVec2 lerp = camera_->GetLerp();
      ImGui::Text("Lerp: (%.3f, %.3f)", static_cast<double>(lerp.x),
                  static_cast<double>(lerp.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Not following");
    }
  }

  // Deadzone.
  if (ImGui::CollapsingHeader("Deadzone", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (camera_->HasDeadzone()) {
      FVec2 dz = camera_->GetDeadzone();
      ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Active");
      ImGui::Text("Half-size: (%.3f, %.3f)", static_cast<double>(dz.x),
                  static_cast<double>(dz.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Disabled");
    }
  }

  // Bounds.
  if (ImGui::CollapsingHeader("Bounds")) {
    if (camera_->HasBounds()) {
      FVec2 start = camera_->GetBoundsStart();
      FVec2 size = camera_->GetBoundsSize();
      ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Active");
      ImGui::Text("Origin: (%.1f, %.1f)", static_cast<double>(start.x),
                  static_cast<double>(start.y));
      ImGui::Text("Size:   (%.1f, %.1f)", static_cast<double>(size.x),
                  static_cast<double>(size.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No bounds set");
    }
  }

  // Shake.
  if (ImGui::CollapsingHeader("Shake")) {
    float intensity = camera_->GetShakeIntensity();
    float timer = camera_->GetShakeTimer();
    FVec2 offset = camera_->GetShakeOffset();
    if (timer > 0.0f) {
      ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Shaking");
      ImGui::Text("Intensity: %.2f", static_cast<double>(intensity));
      ImGui::Text("Remaining: %.2f s", static_cast<double>(timer));
      ImGui::Text("Offset:    (%.1f, %.1f)", static_cast<double>(offset.x),
                  static_cast<double>(offset.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No active shake");
    }
  }

  ImGui::End();
}

namespace {

// Formats a Lua value at the given stack index into a short display string.
void FormatLuaValue(lua_State* L, int idx, char* buf, size_t buf_size) {
  if (idx < 0 && idx > LUA_REGISTRYINDEX) idx = lua_gettop(L) + idx + 1;
  switch (lua_type(L, idx)) {
    case LUA_TNIL:
      snprintf(buf, buf_size, "nil");
      break;
    case LUA_TBOOLEAN:
      snprintf(buf, buf_size, "%s",
               lua_toboolean(L, idx) ? "true" : "false");
      break;
    case LUA_TNUMBER:
      snprintf(buf, buf_size, "%g", lua_tonumber(L, idx));
      break;
    case LUA_TSTRING: {
      const char* s = lua_tostring(L, idx);
      snprintf(buf, buf_size, "\"%s\"", s);
      break;
    }
    case LUA_TTABLE:
      snprintf(buf, buf_size, "{table}");
      break;
    case LUA_TFUNCTION:
      snprintf(buf, buf_size, "function: %p", lua_topointer(L, idx));
      break;
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA: {
      // Try to get the metatable name for a friendlier label.
      if (lua_getmetatable(L, idx)) {
        lua_getfield(L, -1, "__name");
        if (lua_isstring(L, -1)) {
          snprintf(buf, buf_size, "%s: %p", lua_tostring(L, -1),
                   lua_topointer(L, idx));
        } else {
          snprintf(buf, buf_size, "userdata: %p", lua_topointer(L, idx));
        }
        lua_pop(L, 2);
      } else {
        snprintf(buf, buf_size, "userdata: %p", lua_topointer(L, idx));
      }
      break;
    }
    case LUA_TTHREAD:
      snprintf(buf, buf_size, "thread: %p", lua_topointer(L, idx));
      break;
    default:
      snprintf(buf, buf_size, "(%s)", lua_typename(L, lua_type(L, idx)));
      break;
  }
}

// Formats a Lua key at the given stack index for display.
void FormatLuaKey(lua_State* L, int idx, char* buf, size_t buf_size) {
  if (idx < 0 && idx > LUA_REGISTRYINDEX) idx = lua_gettop(L) + idx + 1;
  switch (lua_type(L, idx)) {
    case LUA_TSTRING:
      snprintf(buf, buf_size, "%s", lua_tostring(L, idx));
      break;
    case LUA_TNUMBER:
      snprintf(buf, buf_size, "[%g]", lua_tonumber(L, idx));
      break;
    default:
      snprintf(buf, buf_size, "[%s]",
               lua_typename(L, lua_type(L, idx)));
      break;
  }
}

// Recursively draws a Lua value as ImGui tree nodes. The value must be at
// the top of the Lua stack. table_ref is the stack index of the parent
// table (for write-back); key_idx is the stack index of the key.
// Checks if a Lua table at idx looks like a color ({r, g, b} or {r, g, b, a}).
// If so, fills out the color array and returns the component count (3 or 4).
// Returns 0 if not a color table.
int CheckColorTable(lua_State* L, int idx, float* color) {
  lua_getfield(L, idx, "r");
  lua_getfield(L, idx, "g");
  lua_getfield(L, idx, "b");
  bool is_color = lua_isnumber(L, -3) && lua_isnumber(L, -2) &&
                  lua_isnumber(L, -1);
  if (!is_color) {
    lua_pop(L, 3);
    return 0;
  }
  color[0] = static_cast<float>(lua_tonumber(L, -3));
  color[1] = static_cast<float>(lua_tonumber(L, -2));
  color[2] = static_cast<float>(lua_tonumber(L, -1));
  lua_pop(L, 3);

  lua_getfield(L, idx, "a");
  bool has_alpha = lua_isnumber(L, -1);
  if (has_alpha) {
    color[3] = static_cast<float>(lua_tonumber(L, -1));
  } else {
    color[3] = 1.0f;
  }
  lua_pop(L, 1);
  return has_alpha ? 4 : 3;
}

void DrawLuaValue(lua_State* L, int depth, int table_ref, int key_idx) {
  if (depth > 10) {
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "...");
    return;
  }

  int idx = lua_gettop(L);
  int type = lua_type(L, idx);

  if (type == LUA_TTABLE) {
    // Render each table entry as a child tree node.
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
      // Stack: ... table ... key value
      int child_key = lua_gettop(L) - 1;
      int child_val = lua_gettop(L);

      char key_buf[128];
      FormatLuaKey(L, child_key, key_buf, sizeof(key_buf));

      if (lua_type(L, child_val) == LUA_TTABLE) {
        // Check if this table is a color ({r, g, b} or {r, g, b, a}).
        float color[4];
        int color_components = CheckColorTable(L, child_val, color);
        if (color_components > 0) {
          ImGui::PushID(child_key);
          bool changed = false;
          if (color_components == 4) {
            changed = ImGui::ColorEdit4(key_buf, color);
          } else {
            changed = ImGui::ColorEdit3(key_buf, color);
          }
          if (changed) {
            lua_pushvalue(L, child_key);
            lua_pushvalue(L, child_val);
            lua_pushnumber(L, static_cast<double>(color[0]));
            lua_setfield(L, -2, "r");
            lua_pushnumber(L, static_cast<double>(color[1]));
            lua_setfield(L, -2, "g");
            lua_pushnumber(L, static_cast<double>(color[2]));
            lua_setfield(L, -2, "b");
            if (color_components == 4) {
              lua_pushnumber(L, static_cast<double>(color[3]));
              lua_setfield(L, -2, "a");
            }
            lua_pop(L, 2);
          }
          ImGui::PopID();
        } else {
          bool open = ImGui::TreeNode(key_buf);
          if (open) {
            DrawLuaValue(L, depth + 1, idx, child_key);
            ImGui::TreePop();
          }
        }
      } else {
        ImGui::PushID(child_key);
        // Leaf value — editable inline.
        char val_buf[256];
        FormatLuaValue(L, child_val, val_buf, sizeof(val_buf));

        if (lua_type(L, child_val) == LUA_TNUMBER) {
          double v = lua_tonumber(L, child_val);
          float fv = static_cast<float>(v);
          ImGui::SetNextItemWidth(120);
          if (ImGui::DragFloat(key_buf, &fv, 0.1f)) {
            // Write back: table[key] = new value.
            lua_pushvalue(L, child_key);
            lua_pushnumber(L, static_cast<double>(fv));
            lua_settable(L, idx);
          }
        } else if (lua_type(L, child_val) == LUA_TBOOLEAN) {
          bool v = lua_toboolean(L, child_val) != 0;
          if (ImGui::Checkbox(key_buf, &v)) {
            lua_pushvalue(L, child_key);
            lua_pushboolean(L, v ? 1 : 0);
            lua_settable(L, idx);
          }
        } else {
          ImGui::Text("%s: %s", key_buf, val_buf);
        }
        ImGui::PopID();
      }
      // Pop value, keep key for lua_next.
      lua_pop(L, 1);
    }
  } else {
    char val_buf[256];
    FormatLuaValue(L, idx, val_buf, sizeof(val_buf));
    ImGui::Text("%s", val_buf);
  }
}

}  // namespace

void DebugUI::DrawEntityInspector() {
  if (!initialized_ || !visible_ || lua_ == nullptr) return;

  ImGui::SetNextWindowPos(ImVec2(400, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Entity Inspector", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  lua_State* L = lua_->state();
  int top = lua_gettop(L);

  // Filter input.
  static char filter[128] = {};
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##inspector_filter", "Filter keys...", filter,
                           sizeof(filter));
  ImGui::Separator();

  // Walk the G global table.
  lua_getglobal(L, "G");
  if (!lua_istable(L, -1)) {
    ImGui::Text("G is not a table");
    lua_settop(L, top);
    ImGui::End();
    return;
  }

  int g_idx = lua_gettop(L);
  bool has_filter = filter[0] != '\0';

  lua_pushnil(L);
  while (lua_next(L, g_idx) != 0) {
    // Stack: G key value
    char key_buf[128];
    FormatLuaKey(L, -2, key_buf, sizeof(key_buf));

    if (has_filter && strstr(key_buf, filter) == nullptr) {
      lua_pop(L, 1);
      continue;
    }

    int val_type = lua_type(L, -1);
    if (val_type == LUA_TTABLE) {
      bool open = ImGui::TreeNode(key_buf);
      if (open) {
        DrawLuaValue(L, 1, g_idx, lua_gettop(L) - 1);
        ImGui::TreePop();
      }
    } else {
      char val_buf[256];
      FormatLuaValue(L, -1, val_buf, sizeof(val_buf));
      ImGui::Text("G.%s = %s", key_buf, val_buf);
    }
    lua_pop(L, 1);
  }

  // Also show _Game table (the game module).
  lua_getglobal(L, "_Game");
  if (lua_istable(L, -1)) {
    if (!has_filter || strstr("_Game", filter) != nullptr) {
      if (ImGui::TreeNode("_Game")) {
        DrawLuaValue(L, 1, 0, 0);
        ImGui::TreePop();
      }
    }
  }

  lua_settop(L, top);
  ImGui::End();
}

void DebugUI::DrawPhysicsPanel() {
  if (!initialized_ || !visible_ || physics_ == nullptr) return;

  ImGui::SetNextWindowPos(ImVec2(820, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Physics", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // World stats.
  ImGui::Text("Bodies: %d  Joints: %d  Contacts: %d",
              physics_->GetBodyCount(), physics_->GetJointCount(),
              physics_->GetContactCount());
  ImGui::Text("Pixels/meter: %.1f",
              static_cast<double>(physics_->GetPixelsPerMeter()));

  ImGui::Separator();

  // Gravity sliders.
  if (ImGui::CollapsingHeader("World Settings",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    FVec2 gravity = physics_->GetWorldGravity();
    bool changed = false;
    ImGui::SetNextItemWidth(150);
    if (ImGui::DragFloat("Gravity X", &gravity.x, 1.0f)) changed = true;
    ImGui::SetNextItemWidth(150);
    if (ImGui::DragFloat("Gravity Y", &gravity.y, 1.0f)) changed = true;
    if (changed) physics_->SetWorldGravity(gravity);

    int vel_iter = physics_->GetVelocityIterations();
    int pos_iter = physics_->GetPositionIterations();
    bool iter_changed = false;
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Velocity Iterations", &vel_iter, 1)) {
      if (vel_iter < 1) vel_iter = 1;
      iter_changed = true;
    }
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Position Iterations", &pos_iter, 1)) {
      if (pos_iter < 1) pos_iter = 1;
      iter_changed = true;
    }
    if (iter_changed) physics_->SetIterations(vel_iter, pos_iter);
  }

  ImGui::Separator();

  // Body list.
  if (ImGui::CollapsingHeader("Bodies")) {
    if (ImGui::BeginTable("BodyTable", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0, 250))) {
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("Position");
      ImGui::TableSetupColumn("Velocity");
      ImGui::TableSetupColumn("Angle", ImGuiTableColumnFlags_WidthFixed, 50);
      ImGui::TableSetupColumn("Mass", ImGuiTableColumnFlags_WidthFixed, 55);
      ImGui::TableHeadersRow();

      float ppm = physics_->GetPixelsPerMeter();
      for (const b2Body* body = physics_->GetBodyList(); body != nullptr;
           body = body->GetNext()) {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        const char* type_str = "?";
        switch (body->GetType()) {
          case b2_dynamicBody:
            type_str = "Dynamic";
            break;
          case b2_staticBody:
            type_str = "Static";
            break;
          case b2_kinematicBody:
            type_str = "Kinematic";
            break;
        }
        ImGui::Text("%s", type_str);

        ImGui::TableNextColumn();
        b2Vec2 pos = body->GetPosition();
        ImGui::Text("%.0f, %.0f",
                    static_cast<double>(pos.x * ppm),
                    static_cast<double>(pos.y * ppm));

        ImGui::TableNextColumn();
        b2Vec2 vel = body->GetLinearVelocity();
        ImGui::Text("%.1f, %.1f",
                    static_cast<double>(vel.x * ppm),
                    static_cast<double>(vel.y * ppm));

        ImGui::TableNextColumn();
        ImGui::Text("%.1f", static_cast<double>(body->GetAngle()));

        ImGui::TableNextColumn();
        ImGui::Text("%.1f", static_cast<double>(body->GetMass()));
      }
      ImGui::EndTable();
    }
  }

  ImGui::End();
}

void DebugUI::DrawMenuBar(const FrameContext& ctx) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("Panels")) {
      ImGui::MenuItem("Performance", nullptr, &show_performance_);
      ImGui::MenuItem("Log Console", nullptr, &show_log_console_);
      ImGui::MenuItem("Entity Inspector", nullptr, &show_entity_inspector_);
      ImGui::MenuItem("Audio", nullptr, &show_audio_);
      ImGui::MenuItem("Memory", nullptr, &show_memory_);
      ImGui::MenuItem("Renderer", nullptr, &show_renderer_);
      ImGui::MenuItem("Camera", nullptr, &show_camera_);
      ImGui::MenuItem("Physics", nullptr, &show_physics_);
      ImGui::MenuItem("Assets", nullptr, &show_assets_);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Actions")) {
      if (ImGui::MenuItem("Screenshot (F12)")) {
        screenshot_requested_ = true;
      }
      if (ImGui::MenuItem("Hot Reload")) {
        hot_reload_requested_ = true;
      }
      if (lua_ != nullptr && ImGui::MenuItem("Run GC")) {
        lua_->RunGc();
      }
      ImGui::EndMenu();
    }
    // Time controls inline in the menu bar.
    ImGui::Separator();
    if (lua_ != nullptr) {
      float ts = lua_->TimeScale();
      bool paused = (ts == 0.0f);
      if (ImGui::SmallButton(paused ? "Play" : "Pause")) {
        if (paused) {
          lua_->SetTimeScale(1.0f);
        } else {
          lua_->SetTimeScale(0.0f);
        }
        ts = lua_->TimeScale();
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(100);
      if (ImGui::SliderFloat("##timescale", &ts, 0.0f, 4.0f, "%.2fx")) {
        lua_->SetTimeScale(ts);
      }
    }
    // FPS readout on the right side.
    {
      char fps_text[32];
      float fps = (ctx.lua_memory_kb > 0 && frame_times_ != nullptr &&
                   frame_times_->size() > 0)
                      ? 1000.0f / ((*frame_times_)[frame_times_->size() - 1])
                      : 0.0f;
      snprintf(fps_text, sizeof(fps_text), "%.0f FPS", static_cast<double>(fps));
      float text_width = ImGui::CalcTextSize(fps_text).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - text_width - 10);
      ImGui::Text("%s", fps_text);
    }
    ImGui::EndMainMenuBar();
  }
}

void DebugUI::DrawAll(const FrameContext& ctx) {
  if (!initialized_ || !visible_) return;
  DrawMenuBar(ctx);
  if (show_performance_) DrawPerformancePanel(ctx);
  if (show_log_console_) DrawLogConsole();
  if (show_entity_inspector_) DrawEntityInspector();
  if (show_audio_) DrawAudioPanel();
  if (show_memory_) DrawMemoryPanel(ctx.lua_memory_bytes);
  if (show_renderer_) DrawRendererPanel(ctx);
  if (show_camera_) DrawCameraPanel();
  if (show_physics_) DrawPhysicsPanel();
  if (show_assets_) DrawAssetViewer();
}

bool DebugUI::ConsumeScreenshotRequest() {
  bool r = screenshot_requested_;
  screenshot_requested_ = false;
  return r;
}

void DebugUI::DrawAssetViewer() {
  if (!initialized_ || !visible_) return;

  ImGui::SetNextWindowPos(ImVec2(400, 300), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Assets", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  static char filter[128] = {};
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##asset_filter", "Filter by name...", filter,
                           sizeof(filter));
  ImGui::Separator();

  bool has_filter = filter[0] != '\0';

  if (ImGui::BeginTabBar("AssetTabs")) {
    // Images tab.
    if (renderer_ != nullptr && ImGui::BeginTabItem("Images")) {
      auto images = renderer_->GetImages();
      for (size_t i = 0; i < images.size(); ++i) {
        const auto& img = images[i];
        if (has_filter && strstr(img.name.data(), filter) == nullptr) continue;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode("##img", "%.*s (%zux%zu)",
                            static_cast<int>(img.name.size()), img.name.data(),
                            img.width, img.height)) {
          GLuint tex = renderer_->GetTextureByName(img.name);
          if (tex != 0) {
            float max_w = ImGui::GetContentRegionAvail().x;
            float scale = (static_cast<float>(img.width) > max_w)
                              ? max_w / static_cast<float>(img.width)
                              : 1.0f;
            ImGui::Image(
                static_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
                ImVec2(static_cast<float>(img.width) * scale,
                       static_cast<float>(img.height) * scale),
                ImVec2(0, 1), ImVec2(1, 0));
          }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::EndTabItem();
    }

    // Sprites tab.
    if (renderer_ != nullptr && ImGui::BeginTabItem("Sprites")) {
      auto sprites = renderer_->GetSprites();
      for (size_t i = 0; i < sprites.size(); ++i) {
        const auto& spr = sprites[i];
        if (has_filter && strstr(spr.name.data(), filter) == nullptr) continue;
        ImGui::PushID(static_cast<int>(i));
        auto* sheet = renderer_->GetSpritesheet(spr.spritesheet);
        if (sheet != nullptr) {
          if (ImGui::TreeNode("##spr", "%.*s (%zux%zu)",
                              static_cast<int>(spr.name.size()),
                              spr.name.data(), spr.width, spr.height)) {
            GLuint tex = renderer_->GetTextureByName(sheet->name);
            if (tex != 0 && sheet->width > 0 && sheet->height > 0) {
              float u0 = static_cast<float>(spr.x) /
                         static_cast<float>(sheet->width);
              float v0 = static_cast<float>(spr.y) /
                         static_cast<float>(sheet->height);
              float u1 = static_cast<float>(spr.x + spr.width) /
                         static_cast<float>(sheet->width);
              float v1 = static_cast<float>(spr.y + spr.height) /
                         static_cast<float>(sheet->height);
              float display_w = static_cast<float>(spr.width);
              float display_h = static_cast<float>(spr.height);
              // Scale up small sprites for visibility.
              if (display_w < 64) {
                float s = 64.0f / display_w;
                display_w *= s;
                display_h *= s;
              }
              ImGui::Image(
                  static_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
                  ImVec2(display_w, display_h),
                  ImVec2(u0, v1), ImVec2(u1, v0));
            }
            ImGui::Text("Sheet: %.*s  Pos: %zu,%zu",
                        static_cast<int>(spr.spritesheet.size()),
                        spr.spritesheet.data(), spr.x, spr.y);
            ImGui::TreePop();
          }
        }
        ImGui::PopID();
      }
      ImGui::EndTabItem();
    }

    // Audio tab.
    if (sound_ != nullptr && ImGui::BeginTabItem("Audio")) {
      sqlite3_stmt* stmt = nullptr;
      const char* sql =
          "SELECT name, channels, samplerate, samples, length(contents) "
          "FROM audios ORDER BY name";
      if (db_ != nullptr &&
          sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (ImGui::BeginTable("AudioTable", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0, 0))) {
          ImGui::TableSetupColumn("Name");
          ImGui::TableSetupColumn("Ch", ImGuiTableColumnFlags_WidthFixed, 25);
          ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthFixed, 50);
          ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 65);
          ImGui::TableSetupColumn("##play", ImGuiTableColumnFlags_WidthFixed,
                                  40);
          ImGui::TableHeadersRow();

          while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 0));
            if (name == nullptr) continue;
            if (has_filter && strstr(name, filter) == nullptr) continue;
            int channels = sqlite3_column_int(stmt, 1);
            int samplerate = sqlite3_column_int(stmt, 2);
            int size_bytes = sqlite3_column_int(stmt, 4);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::Text("%d", channels);
            ImGui::TableNextColumn();
            ImGui::Text("%d", samplerate);
            ImGui::TableNextColumn();
            char sz[32];
            FormatBytes(sz, sizeof(sz), static_cast<size_t>(size_bytes));
            ImGui::TextUnformatted(sz);
            ImGui::TableNextColumn();
            ImGui::PushID(name);
            {
              static Sound::Source preview_source = 0;
              static bool has_preview = false;
              static char preview_name[256] = {};
              bool is_playing =
                  has_preview && strcmp(preview_name, name) == 0;
              if (ImGui::SmallButton(is_playing ? "Stop" : "Play")) {
                if (is_playing) {
                  (void)sound_->Stop(preview_source);
                  has_preview = false;
                  preview_name[0] = '\0';
                } else {
                  if (has_preview) {
                    (void)sound_->Stop(preview_source);
                  }
                  auto result =
                      sound_->AddEffect(name, Sound::Ownership::kAutoFree);
                  if (!result.is_error()) {
                    preview_source = result.value();
                    snprintf(preview_name, sizeof(preview_name), "%s", name);
                    has_preview = true;
                    (void)sound_->StartChannel(preview_source);
                  }
                }
              }
            }
            ImGui::PopID();
          }
          ImGui::EndTable();
        }
        sqlite3_finalize(stmt);
      }
      ImGui::EndTabItem();
    }

    // Scripts tab.
    if (db_ != nullptr && ImGui::BeginTabItem("Scripts")) {
      sqlite3_stmt* stmt = nullptr;
      const char* sql =
          "SELECT name, length(contents) FROM scripts ORDER BY name";
      if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
          const char* name = reinterpret_cast<const char*>(
              sqlite3_column_text(stmt, 0));
          int size = sqlite3_column_int(stmt, 1);
          if (name == nullptr) continue;
          if (has_filter && strstr(name, filter) == nullptr) continue;
          char sz[32];
          FormatBytes(sz, sizeof(sz), static_cast<size_t>(size));
          ImGui::Text("%s  (%s)", name, sz);
        }
        sqlite3_finalize(stmt);
      }
      ImGui::EndTabItem();
    }

    // Shaders tab.
    if (db_ != nullptr && ImGui::BeginTabItem("Shaders")) {
      sqlite3_stmt* stmt = nullptr;
      const char* sql =
          "SELECT name, shader_type, length(contents) FROM shaders "
          "ORDER BY name";
      if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
          const char* name = reinterpret_cast<const char*>(
              sqlite3_column_text(stmt, 0));
          const char* type = reinterpret_cast<const char*>(
              sqlite3_column_text(stmt, 1));
          int size = sqlite3_column_int(stmt, 2);
          if (name == nullptr) continue;
          if (has_filter && strstr(name, filter) == nullptr) continue;
          char sz[32];
          FormatBytes(sz, sizeof(sz), static_cast<size_t>(size));
          ImGui::Text("%s  [%s]  (%s)", name, type ? type : "?", sz);
        }
        sqlite3_finalize(stmt);
      }
      ImGui::EndTabItem();
    }

    // Fonts tab.
    if (db_ != nullptr && ImGui::BeginTabItem("Fonts")) {
      sqlite3_stmt* stmt = nullptr;
      const char* sql =
          "SELECT name, length(contents) FROM fonts ORDER BY name";
      if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
          const char* name = reinterpret_cast<const char*>(
              sqlite3_column_text(stmt, 0));
          int size = sqlite3_column_int(stmt, 1);
          if (name == nullptr) continue;
          if (has_filter && strstr(name, filter) == nullptr) continue;
          char sz[32];
          FormatBytes(sz, sizeof(sz), static_cast<size_t>(size));
          ImGui::Text("%s  (%s)", name, sz);
        }
        sqlite3_finalize(stmt);
      }
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::End();
}

bool DebugUI::ConsumeHotReloadRequest() {
  bool r = hot_reload_requested_;
  hot_reload_requested_ = false;
  return r;
}

}  // namespace G

#endif  // GAME_WITH_IMGUI
