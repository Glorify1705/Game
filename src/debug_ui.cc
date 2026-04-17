#include "debug_ui.h"

#ifdef GAME_WITH_IMGUI

#include <cstring>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include "lua.h"

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

  // Allocate the circular buffer for frame time history.
  frame_times_ = allocator_->New<CircularBuffer<float>>(kFrameTimeHistory,
                                                        allocator_);

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

void DebugUI::DrawPerformancePanel(const FrameStats& fs,
                                   float lua_memory_kb,
                                   size_t cmd_buf_used,
                                   size_t cmd_buf_capacity) {
  if (!initialized_ || !visible_) return;

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

  // Lua memory.
  ImGui::Text("Lua memory: %.1f KB", static_cast<double>(lua_memory_kb));

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

  // Toolbar: level filters, text filter, clear, auto-scroll.
  if (ImGui::Button("Clear")) {
    while (!log_entries_->empty()) log_entries_->Pop();
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
        bool open = ImGui::TreeNode(key_buf);
        if (open) {
          DrawLuaValue(L, depth + 1, idx, child_key);
          ImGui::TreePop();
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

}  // namespace G

#endif  // GAME_WITH_IMGUI
