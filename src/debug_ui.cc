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

// Case-insensitive substring search.
bool ContainsCI(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > haystack.size()) return false;
  for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
    bool match = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (tolower(static_cast<unsigned char>(haystack[i + j])) !=
          tolower(static_cast<unsigned char>(needle[j]))) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

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

// Routes ImGui allocations through the engine's SystemAllocator.
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

// Formats a byte count into a human-readable string (KB or MB).
void FormatBytes(SmallBuffer* buf, size_t bytes) {
  if (bytes >= 1024 * 1024) {
    buf->AppendF("%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else {
    buf->AppendF("%.1f KB", static_cast<double>(bytes) / 1024.0);
  }
}

// Returns a green/yellow/red color based on a 0-1 fill ratio.
ImVec4 RatioColor(float ratio) {
  if (ratio < 0.5f) return ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
  if (ratio < 0.8f) return ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
  return ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
}

// Draws a labeled progress bar with used/total formatted as bytes.
void DrawMemoryBar(const char* label, size_t used, size_t total) {
  float ratio = (total > 0)
                    ? static_cast<float>(used) / static_cast<float>(total)
                    : 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  SmallBuffer used_str, total_str;
  FormatBytes(&used_str, used);
  FormatBytes(&total_str, total);
  SmallBuffer overlay;
  overlay.Append(used_str.view(), " / ", total_str.view());
  ImGui::Text("%s", label);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, RatioColor(ratio));
  ImGui::ProgressBar(ratio, ImVec2(-1, 0), overlay.str());
  ImGui::PopStyleColor();
}

// Draws a PlotLines graph from a CircularBuffer with min/avg/max overlay.
void PlotCircularBuffer(const char* label, CircularBuffer<float>* buf,
                        const char* unit, ImVec2 size) {
  size_t count = buf->size();
  if (count == 0) return;
  enum { kMaxSamples = 300 };
  float values[kMaxSamples];
  float sum = 0.0f, min_val = 1e9f, max_val = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    float v = (*buf)[i];
    values[i] = v;
    sum += v;
    if (v < min_val) min_val = v;
    if (v > max_val) max_val = v;
  }
  char overlay[80];
  snprintf(overlay, sizeof(overlay), "%.1f %s (min %.1f  avg %.1f  max %.1f)",
           static_cast<double>(values[count - 1]), unit,
           static_cast<double>(min_val),
           static_cast<double>(sum / static_cast<float>(count)),
           static_cast<double>(max_val));
  ImGui::PlotLines(label, values, static_cast<int>(count),
                   /*values_offset=*/0, overlay, /*scale_min=*/0.0f,
                   /*scale_max=*/max_val * 1.5f, size);
}

// Converts a Lua stack index to an absolute index (Lua 5.1 compat).
int LuaAbsIndex(lua_State* L, int idx) {
  if (idx < 0 && idx > LUA_REGISTRYINDEX) return lua_gettop(L) + idx + 1;
  return idx;
}

// RAII guard that restores the Lua stack to its original depth on scope exit.
struct LuaStackGuard {
  lua_State* L;
  int top;
  LuaStackGuard(lua_State* state) : L(state), top(lua_gettop(state)) {}
  ~LuaStackGuard() { lua_settop(L, top); }
};

// Gets a string field from the table at idx. Returns empty view if missing.
std::string_view LuaGetString(lua_State* L, int idx, const char* field) {
  lua_getfield(L, idx, field);
  std::string_view result;
  if (lua_isstring(L, -1)) {
    const char* s = lua_tostring(L, -1);
    if (s != nullptr) result = s;
  }
  lua_pop(L, 1);
  return result;
}

// Sets a number field on the table at idx.
void LuaSetNumber(lua_State* L, int idx, const char* field, double val) {
  lua_pushnumber(L, val);
  lua_setfield(L, idx < 0 ? idx - 1 : idx, field);
}

// Pushes a global table onto the stack. Returns its absolute index, or 0.
int LuaPushGlobalTable(lua_State* L, const char* name) {
  lua_getglobal(L, name);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  return lua_gettop(L);
}

// Calls fn(key_cstr, key_sv) for each string-keyed entry in table at
// table_idx. The value is at the top of the Lua stack inside the callback.
// Return true from fn to stop iteration early.
template <typename F>
void LuaTableForEach(lua_State* L, int table_idx, F&& fn) {
  lua_pushnil(L);
  while (lua_next(L, table_idx) != 0) {
    if (lua_type(L, -2) == LUA_TSTRING) {
      const char* key_cstr = lua_tostring(L, -2);
      std::string_view key = key_cstr;
      if (fn(key_cstr, key)) {
        lua_pop(L, 2);
        return;
      }
    }
    lua_pop(L, 1);
  }
}

// Calls fn(i) for each element in the array at table_idx (1-indexed).
// The element is at the top of the Lua stack inside the callback.
template <typename F>
void LuaArrayForEach(lua_State* L, int table_idx, F&& fn) {
  int n = static_cast<int>(lua_objlen(L, table_idx));
  for (int i = 1; i <= n; ++i) {
    lua_rawgeti(L, table_idx, i);
    fn(i);
    lua_pop(L, 1);
  }
}

// Formats a Lua value at the given stack index into a short display string.
void FormatLuaValue(lua_State* L, int idx, StringBuffer* buf) {
  idx = LuaAbsIndex(L, idx);
  switch (lua_type(L, idx)) {
    case LUA_TNIL:
      buf->Append("nil");
      break;
    case LUA_TBOOLEAN:
      buf->Append(lua_toboolean(L, idx) ? "true" : "false");
      break;
    case LUA_TNUMBER:
      buf->AppendF("%g", lua_tonumber(L, idx));
      break;
    case LUA_TSTRING:
      buf->AppendF("\"%s\"", lua_tostring(L, idx));
      break;
    case LUA_TTABLE:
      buf->Append("{table}");
      break;
    case LUA_TFUNCTION:
      buf->AppendF("function: %p", lua_topointer(L, idx));
      break;
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA:
      if (lua_getmetatable(L, idx)) {
        lua_getfield(L, -1, "__name");
        if (lua_isstring(L, -1)) {
          buf->AppendF("%s: %p", lua_tostring(L, -1),
                       lua_topointer(L, idx));
        } else {
          buf->AppendF("userdata: %p", lua_topointer(L, idx));
        }
        lua_pop(L, 2);
      } else {
        buf->AppendF("userdata: %p", lua_topointer(L, idx));
      }
      break;
    case LUA_TTHREAD:
      buf->AppendF("thread: %p", lua_topointer(L, idx));
      break;
    default:
      buf->AppendF("(%s)", lua_typename(L, lua_type(L, idx)));
      break;
  }
}

// Formats a Lua key at the given stack index for display.
void FormatLuaKey(lua_State* L, int idx, StringBuffer* buf) {
  idx = LuaAbsIndex(L, idx);
  switch (lua_type(L, idx)) {
    case LUA_TSTRING:
      buf->Append(lua_tostring(L, idx));
      break;
    case LUA_TNUMBER:
      buf->AppendF("[%g]", lua_tonumber(L, idx));
      break;
    default:
      buf->AppendF("[%s]", lua_typename(L, lua_type(L, idx)));
      break;
  }
}

// Builds "name(arg1, arg2, ...)" from a _Docs function table at func_idx.
void FormatLuaSignature(lua_State* L, int func_idx, const char* name,
                        StringBuffer* buf) {
  buf->Append(name, "(");
  lua_getfield(L, func_idx, "args");
  if (lua_istable(L, -1)) {
    int args_idx = lua_gettop(L);
    int nargs = static_cast<int>(lua_objlen(L, args_idx));
    for (int i = 1; i <= nargs; ++i) {
      lua_rawgeti(L, args_idx, i);
      if (lua_istable(L, -1)) {
        auto aname = LuaGetString(L, -1, "name");
        if (!aname.empty()) {
          if (i > 1) buf->Append(", ");
          buf->Append(aname);
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  buf->Append(")");
}

// Checks if a Lua table looks like a color ({r, g, b} or {r, g, b, a}).
// Returns component count (3 or 4) or 0 if not a color.
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
  color[3] = has_alpha ? static_cast<float>(lua_tonumber(L, -1)) : 1.0f;
  lua_pop(L, 1);
  return has_alpha ? 4 : 3;
}

// Compiles a Fennel string to Lua source using _fennel.compileString.
// Returns true with compiled Lua in output, false with error in output.
bool CompileFennel(lua_State* L, std::string_view code, StringBuffer* output) {
  int top = lua_gettop(L);
  lua_getglobal(L, "_fennel");
  if (!lua_istable(L, -1)) {
    lua_settop(L, top);
    output->Append("Fennel compiler not loaded");
    return false;
  }
  lua_getfield(L, -1, "compileString");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    output->Append("_fennel.compileString not found");
    return false;
  }
  lua_pushlstring(L, code.data(), code.size());
  if (lua_pcall(L, 1, 1, 0) != 0) {
    if (lua_isstring(L, -1)) output->Append(lua_tostring(L, -1));
    lua_settop(L, top);
    return false;
  }
  if (lua_isstring(L, -1)) {
    output->Append(lua_tostring(L, -1));
  }
  lua_settop(L, top);
  return true;
}

// Checks if a Lua table looks like a vec2 ({x, y} with both numbers).
// Returns true and fills xy[2] if so.
bool CheckVec2Table(lua_State* L, int idx, float* xy) {
  lua_getfield(L, idx, "x");
  lua_getfield(L, idx, "y");
  bool is_vec2 = lua_isnumber(L, -2) && lua_isnumber(L, -1);
  if (!is_vec2) {
    lua_pop(L, 2);
    return false;
  }
  xy[0] = static_cast<float>(lua_tonumber(L, -2));
  xy[1] = static_cast<float>(lua_tonumber(L, -1));
  lua_pop(L, 2);
  return true;
}

// Recursively draws a Lua value as ImGui tree nodes.
void DrawLuaValue(lua_State* L, int depth, int table_ref, int key_idx) {
  if (depth > 10) {
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "...");
    return;
  }
  int idx = lua_gettop(L);
  int type = lua_type(L, idx);
  if (type == LUA_TTABLE) {
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
      int child_key = lua_gettop(L) - 1;
      int child_val = lua_gettop(L);
      SmallBuffer key_buf;
      FormatLuaKey(L, child_key, &key_buf);
      if (lua_type(L, child_val) == LUA_TTABLE) {
        float xy[2];
        float color[4];
        if (CheckVec2Table(L, child_val, xy)) {
          ImGui::PushID(child_key);
          ImGui::SetNextItemWidth(180);
          if (ImGui::DragFloat2(key_buf.str(), xy, 0.1f)) {
            LuaSetNumber(L, child_val, "x", xy[0]);
            LuaSetNumber(L, child_val, "y", xy[1]);
          }
          ImGui::PopID();
        } else if (int cc = CheckColorTable(L, child_val, color); cc > 0) {
          ImGui::PushID(child_key);
          bool changed = (cc == 4) ? ImGui::ColorEdit4(key_buf.str(), color)
                                   : ImGui::ColorEdit3(key_buf.str(), color);
          if (changed) {
            LuaSetNumber(L, child_val, "r", color[0]);
            LuaSetNumber(L, child_val, "g", color[1]);
            LuaSetNumber(L, child_val, "b", color[2]);
            if (cc == 4) LuaSetNumber(L, child_val, "a", color[3]);
          }
          ImGui::PopID();
        } else {
          if (ImGui::TreeNode(key_buf.str())) {
            DrawLuaValue(L, depth + 1, idx, child_key);
            ImGui::TreePop();
          }
        }
      } else {
        ImGui::PushID(child_key);
        if (lua_type(L, child_val) == LUA_TNUMBER) {
          float fv = static_cast<float>(lua_tonumber(L, child_val));
          ImGui::SetNextItemWidth(120);
          if (ImGui::DragFloat(key_buf.str(), &fv, 0.1f)) {
            lua_pushvalue(L, child_key);
            lua_pushnumber(L, static_cast<double>(fv));
            lua_settable(L, idx);
          }
        } else if (lua_type(L, child_val) == LUA_TBOOLEAN) {
          bool v = lua_toboolean(L, child_val) != 0;
          if (ImGui::Checkbox(key_buf.str(), &v)) {
            lua_pushvalue(L, child_key);
            lua_pushboolean(L, v ? 1 : 0);
            lua_settable(L, idx);
          }
        } else {
          Str val_buf;
          FormatLuaValue(L, child_val, &val_buf);
          ImGui::Text("%s: %s", key_buf.str(), val_buf.str());
        }
        ImGui::PopID();
      }
      lua_pop(L, 1);
    }
  } else {
    Str val_buf;
    FormatLuaValue(L, idx, &val_buf);
    ImGui::Text("%s", val_buf.str());
  }
}

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

void DebugUI::StartLogCapture(Allocator* allocator) {
  allocator_ = allocator;
  log_entries_ =
      allocator_->New<CircularBuffer<LogEntry>>(kMaxLogEntries, allocator_);
  g_original_sink = GetLogSink();
  g_debug_ui = this;
  SetLogSink(DebugUILogSink);
}

void DebugUI::Init(SDL_Window* window, SDL_GLContext gl_context) {
  window_ = window;
  ImGui::SetAllocatorFunctions(ImGuiAllocFunc, ImGuiFreeFunc, nullptr);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
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
  // Shares eval_history_entries_ with the Log Console.
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

void DebugUI::DrawPerformancePanel(const FrameContext& ctx) {
  ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380, 400), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Performance", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // FPS and frame time graph.
  if (frame_times_->size() > 0) {
    float last_ms = (*frame_times_)[frame_times_->size() - 1];
    float fps = (last_ms > 0.0f) ? 1000.0f / last_ms : 0.0f;
    ImGui::Text("FPS: %.1f", static_cast<double>(fps));
    ImGui::Separator();
    PlotCircularBuffer("Frame Time", frame_times_, "ms", ImVec2(0, 80));
  }

  ImGui::Separator();

  // Draw call breakdown.
  const auto& fs = ctx.frame_stats;
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
  PlotCircularBuffer("Lua Memory", lua_memory_samples_, "KB", ImVec2(0, 60));

  // Frame time breakdown.
  if (ImGui::CollapsingHeader("Frame Breakdown",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    const auto& bd = ctx.breakdown;
    float total = bd.update_ms + bd.draw_ms + bd.render_ms + bd.debug_ui_ms;

    struct Category {
      const char* name;
      float ms;
      ImU32 color;
    };
    Category cats[] = {
        {"Update", bd.update_ms, IM_COL32(74, 144, 217, 255)},
        {"Draw", bd.draw_ms, IM_COL32(80, 200, 120, 255)},
        {"Render", bd.render_ms, IM_COL32(232, 150, 58, 255)},
        {"DebugUI", bd.debug_ui_ms, IM_COL32(155, 89, 182, 255)},
    };

    // Stacked bar.
    if (total > 0.0f) {
      float bar_width = ImGui::GetContentRegionAvail().x;
      float bar_height = 20.0f;
      ImVec2 cursor = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      float x = cursor.x;
      for (auto& cat : cats) {
        float w = (cat.ms / total) * bar_width;
        if (w < 1.0f) w = 1.0f;
        dl->AddRectFilled(ImVec2(x, cursor.y),
                          ImVec2(x + w, cursor.y + bar_height), cat.color);
        x += w;
      }
      ImGui::Dummy(ImVec2(bar_width, bar_height));

      // Legend.
      for (size_t i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        ImGui::ColorButton(cats[i].name,
                           ImColor(cats[i].color).Value,
                           ImGuiColorEditFlags_NoTooltip |
                               ImGuiColorEditFlags_NoPicker,
                           ImVec2(10, 10));
        ImGui::SameLine();
        ImGui::Text("%s %.1f", cats[i].name,
                    static_cast<double>(cats[i].ms));
      }
    }

    // Stacked history chart.
    size_t count = breakdown_history_->size();
    if (count > 0) {
      float chart_width = ImGui::GetContentRegionAvail().x;
      float chart_height = 80.0f;
      ImVec2 cursor = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // Find max total for Y scale.
      float max_total = 0.0f;
      for (size_t i = 0; i < count; ++i) {
        const auto& s = (*breakdown_history_)[i];
        float t = s.update_ms + s.draw_ms + s.render_ms + s.debug_ui_ms;
        if (t > max_total) max_total = t;
      }
      if (max_total < 1.0f) max_total = 1.0f;
      max_total *= 1.2f;

      // Background.
      dl->AddRectFilled(cursor,
                        ImVec2(cursor.x + chart_width,
                               cursor.y + chart_height),
                        IM_COL32(30, 30, 30, 200));

      float col_width = chart_width / static_cast<float>(count);
      for (size_t i = 0; i < count; ++i) {
        const auto& s = (*breakdown_history_)[i];
        float vals[] = {s.update_ms, s.draw_ms, s.render_ms, s.debug_ui_ms};
        ImU32 cols[] = {cats[0].color, cats[1].color, cats[2].color,
                        cats[3].color};
        float x = cursor.x + static_cast<float>(i) * col_width;
        float y_bottom = cursor.y + chart_height;
        for (int j = 0; j < 4; ++j) {
          float h = (vals[j] / max_total) * chart_height;
          if (h < 0.5f) continue;
          dl->AddRectFilled(ImVec2(x, y_bottom - h),
                            ImVec2(x + col_width, y_bottom), cols[j]);
          y_bottom -= h;
        }
      }
      ImGui::Dummy(ImVec2(chart_width, chart_height));
    }
  }

  ImGui::Separator();

  // Command buffer fill.
  DrawMemoryBar("Command Buffer", ctx.cmd_buf_used, ctx.cmd_buf_capacity);

  ImGui::End();
}

void DebugUI::DrawLogConsole() {
  ImGui::SetNextWindowPos(ImVec2(10, 440), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Log Console", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // Toolbar.
  if (ImGui::Button("Clear")) {
    while (!log_entries_->empty()) log_entries_->Pop();
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy")) {
    bool has_filter = log_text_filter_[0] != '\0';
    enum { kClipBufSize = 64 * 1024 };
    char clip[kClipBufSize];
    size_t pos = 0;
    size_t count = log_entries_->size();
    for (size_t i = 0; i < count && pos < kClipBufSize - 1; ++i) {
      const LogEntry& entry = (*log_entries_)[i];
      int level_idx = static_cast<int>(entry.level);
      if (level_idx < 0 || level_idx > 5) continue;
      if (!log_level_filter_[level_idx]) continue;
      if (has_filter && std::string_view(entry.text).find(log_text_filter_) ==
              std::string_view::npos) {
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
  if (ImGui::Button("Save")) {
    const char* write_dir = PHYSFS_getWriteDir();
    if (write_dir != nullptr) {
      CmdBuffer dir(write_dir, "logs");
      (void)MakeDirs(dir.str());
      CmdBuffer path(dir.str(), "/log_",
                     static_cast<uint64_t>(SDL_GetTicks()), ".txt");
      FILE* f = fopen(path.str(), "w");
      if (f != nullptr) {
        size_t count = log_entries_->size();
        for (size_t i = 0; i < count; ++i) {
          fprintf(f, "%s\n", (*log_entries_)[i].text);
        }
        fclose(f);
        LogMessage(LogLevel::kInfo, path.str());
      }
    }
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
    ImGui::PushStyleColor(ImGuiCol_Text,
                          LogLevelColor(static_cast<LogLevel>(i)));
    ImGui::Checkbox(level_names[i], &log_level_filter_[i]);
    ImGui::PopStyleColor();
  }

  ImGui::Separator();

  // Scrollable log region.
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
      if (has_text_filter &&
          std::string_view(entry.text).find(log_text_filter_) ==
              std::string_view::npos) {
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
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##eval", eval_input_, kEvalInputSize, input_flags,
                       EvalHistoryCallback, this)) {
    if (eval_input_[0] != '\0' && engine_ != nullptr) {
      FixedStringBuffer<kMaxLogLineLength> echo(kTruncating);
      echo.Append("> ", eval_input_);
      LogMessage(LogLevel::kInfo, echo.str());
      // Add to history (skip consecutive duplicates).
      if (eval_history_count_ == 0 ||
          std::string_view(eval_history_entries_[(eval_history_count_ - 1) %
                                                 kEvalHistoryMax]) !=
              eval_input_) {
        int idx = eval_history_count_ % kEvalHistoryMax;
        snprintf(eval_history_entries_[idx], kEvalInputSize, "%s",
                 eval_input_);
        eval_history_count_++;
      }
      eval_history_pos_ = -1;
      FixedStringBuffer<kMaxLogLineLength> result(kTruncating);
      bool ok = engine_->lua.EvalString(eval_input_, &result);
      if (result.view().size() > 0) {
        LogMessage(ok ? LogLevel::kInfo : LogLevel::kError, result.str());
      }
      eval_input_[0] = '\0';
    }
    reclaim_focus = true;
  }
  ImGui::SetItemDefaultFocus();
  if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);
  ImGui::End();
}

void DebugUI::DrawAudioPanel() {
  Sound* sound = &engine_->sound;
  ImGui::SetNextWindowPos(ImVec2(620, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 350), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Audio", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  float global_gain = sound->global_gain();
  if (ImGui::SliderFloat("Global Volume", &global_gain, 0.0f, 1.0f,
                          "%.2f")) {
    sound->SetGlobalGain(global_gain);
  }
  ImGui::Separator();

  size_t used = sound->stream_count();
  size_t total = sound->max_streams();
  ImGui::Text("Stream Slots: %zu / %zu", used, total);
  float slot_ratio = (total > 0)
                         ? static_cast<float>(used) / static_cast<float>(total)
                         : 0.0f;
  ImGui::ProgressBar(slot_ratio, ImVec2(-1, 0));
  ImGui::Separator();

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
    sound->GetStreamDebugInfo(infos, 128);
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

void DebugUI::DrawMemoryPanel(size_t lua_memory_bytes) {
  ImGui::SetNextWindowPos(ImVec2(620, 390), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380, 280), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Memory", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  if (engine_arena_ != nullptr &&
      ImGui::CollapsingHeader("Engine Arena",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawMemoryBar("Used / Total", engine_arena_->used_memory(),
                  engine_arena_->total_memory());
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Frame Allocator",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawMemoryBar("Used / Total", engine_->frame_allocator.used_memory(),
                  engine_->frame_allocator.total_memory());
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Lua Heap", ImGuiTreeNodeFlags_DefaultOpen)) {
    SmallBuffer lua_str;
    FormatBytes(&lua_str, lua_memory_bytes);
    ImGui::Text("Lua memory: %s", lua_str.str());
    size_t lua_count = lua_memory_samples_->size();
    if (lua_count > 0) {
      enum { kMax = 300 };
      float lua_values[kMax];
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

void DebugUI::DrawRendererPanel(const FrameContext& ctx) {
  const auto& fs = ctx.frame_stats;
  ImGui::SetNextWindowPos(ImVec2(10, 440), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 450), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Renderer", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

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
    int total_redundant = fs.redundant_texture + fs.redundant_transform +
                          fs.redundant_shader + fs.redundant_blend +
                          fs.redundant_line_width + fs.redundant_sdf_outline;
    ImGui::Text("Redundant Skips: %d", total_redundant);
    if (total_redundant > 0 && ImGui::TreeNode("##redundant_detail")) {
      ImGui::Text("Texture:     %d", fs.redundant_texture);
      ImGui::Text("Transform:   %d", fs.redundant_transform);
      ImGui::Text("Shader:      %d", fs.redundant_shader);
      ImGui::Text("Blend:       %d", fs.redundant_blend);
      ImGui::Text("Line Width:  %d", fs.redundant_line_width);
      ImGui::Text("SDF Outline: %d", fs.redundant_sdf_outline);
      ImGui::TreePop();
    }
    ImGui::Separator();
    DrawMemoryBar("Command Buffer", ctx.cmd_buf_used, ctx.cmd_buf_capacity);
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Textures")) {
    ImGui::Text("Texture units: %zu",
                engine_->batch_renderer.GetTextureCount());
  }

  if (ImGui::CollapsingHeader("Loaded Images")) {
    Slice<DbAssets::Image> images = engine_->renderer.GetImages();
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

  if (ImGui::CollapsingHeader("Shader Programs")) {
    engine_->shaders.programs().ForEach(
        [](std::string_view name, const GLuint& handle) {
          ImGui::Text("%-30.*s  (GL %u)", static_cast<int>(name.size()),
                      name.data(), handle);
        });
  }

  if (ImGui::CollapsingHeader("Current State")) {
    ImGui::Text("Blend mode: %s",
                BlendModeName(engine_->batch_renderer.GetCurrentBlendMode()));
    std::string_view shader_name =
        StringByHandle(engine_->batch_renderer.GetCurrentShader());
    ImGui::Text("Shader: %.*s", static_cast<int>(shader_name.size()),
                shader_name.data());
    IVec2 vp = engine_->batch_renderer.viewport();
    ImGui::Text("Viewport: %dx%d", vp.x, vp.y);
  }
  ImGui::End();
}

void DebugUI::DrawCameraPanel() {
  Camera* camera = &engine_->camera;
  ImGui::SetNextWindowPos(ImVec2(440, 440), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(340, 350), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Camera", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  FVec2 pos = camera->GetPosition();
  ImGui::Text("Position: (%.1f, %.1f)", static_cast<double>(pos.x),
              static_cast<double>(pos.y));
  float zoom = camera->GetZoom();
  ImGui::Text("Zoom: %.3f", static_cast<double>(zoom));
  float rotation = camera->GetRotation();
  ImGui::Text("Rotation: %.3f rad (%.1f deg)",
              static_cast<double>(rotation),
              static_cast<double>(rotation * 180.0f /
                                  static_cast<float>(M_PI)));
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Follow", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (camera->IsFollowing()) {
      FVec2 target = camera->GetFollowTarget();
      ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Following");
      ImGui::Text("Target: (%.1f, %.1f)", static_cast<double>(target.x),
                  static_cast<double>(target.y));
      FVec2 lerp = camera->GetLerp();
      ImGui::Text("Lerp: (%.3f, %.3f)", static_cast<double>(lerp.x),
                  static_cast<double>(lerp.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Not following");
    }
  }

  if (ImGui::CollapsingHeader("Deadzone", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (camera->HasDeadzone()) {
      FVec2 dz = camera->GetDeadzone();
      ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Active");
      ImGui::Text("Half-size: (%.3f, %.3f)", static_cast<double>(dz.x),
                  static_cast<double>(dz.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Disabled");
    }
  }

  if (ImGui::CollapsingHeader("Bounds")) {
    if (camera->HasBounds()) {
      FVec2 start = camera->GetBoundsStart();
      FVec2 size = camera->GetBoundsSize();
      ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Active");
      ImGui::Text("Origin: (%.1f, %.1f)", static_cast<double>(start.x),
                  static_cast<double>(start.y));
      ImGui::Text("Size:   (%.1f, %.1f)", static_cast<double>(size.x),
                  static_cast<double>(size.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No bounds set");
    }
  }

  if (ImGui::CollapsingHeader("Shake")) {
    float intensity = camera->GetShakeIntensity();
    float timer = camera->GetShakeTimer();
    FVec2 offset = camera->GetShakeOffset();
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

  ImGui::Separator();

  // Drag-to-pan camera override.
  if (camera_override_) {
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Override active");
    if (ImGui::Button("Release")) {
      camera->SetPosition(camera_saved_x_, camera_saved_y_);
      camera_override_ = false;
    }
    ImGui::SameLine();
    ImGui::Text("(drag game viewport to pan)");
    // Check for mouse drag on the game viewport (not over any ImGui window).
    if (!ImGui::GetIO().WantCaptureMouse && ImGui::IsMouseDragging(0)) {
      ImVec2 delta = ImGui::GetMouseDragDelta(0);
      ImGui::ResetMouseDragDelta(0);
      FVec2 p = camera->GetPosition();
      camera->SetPosition(p.x - delta.x, p.y - delta.y);
    }
  } else {
    if (ImGui::Button("Pan Override")) {
      FVec2 p = camera->GetPosition();
      camera_saved_x_ = p.x;
      camera_saved_y_ = p.y;
      camera_override_ = true;
    }
  }

  ImGui::End();
}

void DebugUI::DrawEntityInspector() {
  lua_State* L = engine_->lua.state();
  LuaStackGuard guard(L);

  ImGui::SetNextWindowPos(ImVec2(400, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Entity Inspector", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##inspector_filter", "Filter keys...",
                           inspector_filter_, sizeof(inspector_filter_));
  ImGui::Separator();

  std::string_view filter = inspector_filter_;
  bool has_filter = !filter.empty();

  // Show _Game first — this is the primary game state table.
  int game_idx = LuaPushGlobalTable(L, "_Game");
  if (game_idx != 0) {
    if (!has_filter ||
        std::string_view("_Game").find(filter) != std::string_view::npos) {
      if (ImGui::TreeNodeEx("_Game", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawLuaValue(L, 1, 0, 0);
        ImGui::TreePop();
      }
    }
    lua_pop(L, 1);
  }

  // Show user globals (skip internal tables and standard Lua modules).
  lua_pushvalue(L, LUA_GLOBALSINDEX);
  int globals_idx = lua_gettop(L);
  bool has_user_globals = false;
  LuaTableForEach(L, globals_idx, [&](const char* key_cstr,
                                      std::string_view key) {
    if (key == "G" || key == "_Game" || key == "_G" || key == "_Docs" ||
        key == "_VERSION" || key == "string" || key == "table" ||
        key == "math" || key == "io" || key == "os" ||
        key == "coroutine" || key == "debug" || key == "package" ||
        key == "arg" || lua_type(L, -1) == LUA_TFUNCTION) {
      return false;
    }
    if (has_filter && key.find(filter) == std::string_view::npos) {
      return false;
    }
    if (!has_user_globals) {
      has_user_globals = true;
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Globals");
    }
    if (lua_type(L, -1) == LUA_TTABLE) {
      if (ImGui::TreeNode(key_cstr)) {
        DrawLuaValue(L, 1, globals_idx, lua_gettop(L) - 1);
        ImGui::TreePop();
      }
    } else {
      Str val_buf;
      FormatLuaValue(L, -1, &val_buf);
      ImGui::Text("%s = %s", key_cstr, val_buf.str());
    }
    return false;
  });

  ImGui::End();
}

void DebugUI::DrawPhysicsPanel() {
  Physics* physics = &engine_->physics;
  ImGui::SetNextWindowPos(ImVec2(820, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Physics", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  ImGui::Text("Bodies: %d  Joints: %d  Contacts: %d",
              physics->GetBodyCount(), physics->GetJointCount(),
              physics->GetContactCount());
  ImGui::Text("Pixels/meter: %.1f",
              static_cast<double>(physics->GetPixelsPerMeter()));
  ImGui::Separator();

  if (ImGui::CollapsingHeader("World Settings",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    FVec2 gravity = physics->GetWorldGravity();
    bool changed = false;
    ImGui::SetNextItemWidth(150);
    if (ImGui::DragFloat("Gravity X", &gravity.x, 1.0f)) changed = true;
    ImGui::SetNextItemWidth(150);
    if (ImGui::DragFloat("Gravity Y", &gravity.y, 1.0f)) changed = true;
    if (changed) physics->SetWorldGravity(gravity);

    int vel_iter = physics->GetVelocityIterations();
    int pos_iter = physics->GetPositionIterations();
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
    if (iter_changed) physics->SetIterations(vel_iter, pos_iter);
  }
  ImGui::Separator();

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
      float ppm = physics->GetPixelsPerMeter();
      for (const b2Body* body = physics->GetBodyList(); body != nullptr;
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
        ImGui::Text("%.0f, %.0f", static_cast<double>(pos.x * ppm),
                    static_cast<double>(pos.y * ppm));
        ImGui::TableNextColumn();
        b2Vec2 vel = body->GetLinearVelocity();
        ImGui::Text("%.1f, %.1f", static_cast<double>(vel.x * ppm),
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
      char fps_text[32];
      snprintf(fps_text, sizeof(fps_text), "%.0f FPS",
               static_cast<double>(fps));
      float text_width = ImGui::CalcTextSize(fps_text).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - text_width - 10);
      ImGui::Text("%s", fps_text);
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
    if (ImGui::Begin("Texture Zoom", &open, ImGuiWindowFlags_NoFocusOnAppearing)) {
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
          // Image is flipped vertically (UV 0,1 to 1,0).
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
                               ImGuiColorEditFlags_AlphaPreview, ImVec2(32, 32));
            ImGui::SameLine();
            ImGui::Text("(%d, %d)\n#%02X%02X%02X%02X\nRGBA: %d %d %d %d",
                        px, py, zoom_pixels_[offset], zoom_pixels_[offset + 1],
                        zoom_pixels_[offset + 2], zoom_pixels_[offset + 3],
                        zoom_pixels_[offset], zoom_pixels_[offset + 1],
                        zoom_pixels_[offset + 2], zoom_pixels_[offset + 3]);
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

void DebugUI::DrawDocsPanel() {
  lua_State* L = engine_->lua.state();
  LuaStackGuard guard(L);

  ImGui::SetNextWindowPos(ImVec2(420, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(480, 550), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("API Docs", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##docs_filter", "Search functions...",
                           docs_filter_, sizeof(docs_filter_));
  ImGui::Separator();

  int docs_idx = LuaPushGlobalTable(L, "_Docs");
  if (docs_idx == 0) {
    ImGui::Text("_Docs table not found");
    ImGui::End();
    return;
  }
  std::string_view filter = docs_filter_;
  bool has_filter = !filter.empty();

  // Checks whether any function in a library matches the filter.
  auto LibraryMatchesFilter = [&](int lib_idx,
                                  std::string_view lib_name) -> bool {
    if (ContainsCI(lib_name, filter)) return true;
    bool found = false;
    LuaTableForEach(L, lib_idx, [&](const char*, std::string_view fname) {
      if (ContainsCI(fname, filter)) { found = true; return true; }
      auto ds = LuaGetString(L, -1, "docstring");
      if (!ds.empty() && ContainsCI(ds, filter)) { found = true; return true; }
      return false;
    });
    return found;
  };

  // Iterate libraries (e.g. graphics, physics, sound).
  LuaTableForEach(L, docs_idx, [&](const char* lib_cstr,
                                   std::string_view lib_name) {
    if (!lua_istable(L, -1)) return false;
    if (has_filter && !LibraryMatchesFilter(lua_gettop(L), lib_name)) {
      return false;
    }
    int lib_idx = lua_gettop(L);

    ImGuiTreeNodeFlags lib_flags = has_filter ? ImGuiTreeNodeFlags_DefaultOpen
                                             : ImGuiTreeNodeFlags_None;
    SmallBuffer lib_label;
    lib_label.Append("G.", lib_cstr);
    if (!ImGui::TreeNodeEx(lib_label.str(), lib_flags)) return false;

    // Iterate functions in the library.
    LuaTableForEach(L, lib_idx, [&](const char* func_cstr,
                                    std::string_view func_name) {
      if (!lua_istable(L, -1)) return false;
      int func_idx = lua_gettop(L);

      // Filter individual functions.
      if (has_filter && !ContainsCI(lib_name, filter) &&
          !ContainsCI(func_name, filter)) {
        auto ds = LuaGetString(L, func_idx, "docstring");
        if (ds.empty() || !ContainsCI(ds, filter)) return false;
      }

      Str sig;
      FormatLuaSignature(L, func_idx, func_cstr, &sig);
      if (!ImGui::TreeNode(func_cstr, "%s", sig.str())) return false;

      // Docstring.
      auto docstring = LuaGetString(L, func_idx, "docstring");
      if (!docstring.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(docstring.size()),
                           docstring.data());
      }

      // Arguments detail.
      lua_getfield(L, func_idx, "args");
      if (lua_istable(L, -1)) {
        int args_idx = lua_gettop(L);
        if (lua_objlen(L, args_idx) > 0) {
          ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Args:");
          LuaArrayForEach(L, args_idx, [&](int) {
            if (!lua_istable(L, -1)) return;
            auto aname = LuaGetString(L, -1, "name");
            auto adoc = LuaGetString(L, -1, "docstring");
            if (aname.empty()) return;
            if (!adoc.empty()) {
              ImGui::BulletText("%.*s - %.*s",
                                static_cast<int>(aname.size()), aname.data(),
                                static_cast<int>(adoc.size()), adoc.data());
            } else {
              ImGui::BulletText("%.*s",
                                static_cast<int>(aname.size()), aname.data());
            }
          });
        }
      }
      lua_pop(L, 1);

      // Return values.
      lua_getfield(L, func_idx, "returns");
      if (lua_istable(L, -1)) {
        int ret_idx = lua_gettop(L);
        if (lua_objlen(L, ret_idx) > 0) {
          ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "Returns:");
          LuaArrayForEach(L, ret_idx, [&](int) {
            if (lua_isstring(L, -1)) {
              ImGui::BulletText("%s", lua_tostring(L, -1));
            }
          });
        }
      }
      lua_pop(L, 1);

      ImGui::TreePop();
      return false;
    });
    ImGui::TreePop();
    return false;
  });

  ImGui::End();
}

void DebugUI::DrawWatchPanel() {
  ImGui::SetNextWindowPos(ImVec2(820, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380, 350), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Watch", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  // Input to add a new watch expression.
  ImGui::SetNextItemWidth(-60);
  bool add = ImGui::InputTextWithHint(
      "##watch_add", "#_Game.enemies", watch_input_, kWatchPathSize,
      ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if (ImGui::Button("Add") || add) {
    if (watch_input_[0] != '\0' && watch_count_ < (int)kMaxWatches) {
      snprintf(watches_[watch_count_].path, kWatchPathSize, "%s",
               watch_input_);
      ++watch_count_;
      watch_input_[0] = '\0';
    }
  }
  ImGui::Separator();

  if (watch_count_ == 0) {
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       "No watches. Type a Lua expression above.");
    ImGui::End();
    return;
  }

  bool has_error = engine_->lua.HasError();
  int remove_idx = -1;

  if (ImGui::BeginTable("##watches", 3,
                        ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Expr", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##rm", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < watch_count_; ++i) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(watches_[i].path);

      ImGui::TableNextColumn();
      if (has_error) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "error");
      } else {
        Str result;
        if (engine_->lua.EvalString(watches_[i].path, &result)) {
          ImGui::TextUnformatted(result.str());
        } else {
          ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                             result.str());
        }
      }

      ImGui::TableNextColumn();
      ImGui::PushID(i);
      if (ImGui::SmallButton("X")) remove_idx = i;
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  // Remove entry by shifting.
  if (remove_idx >= 0) {
    for (int i = remove_idx; i < watch_count_ - 1; ++i) {
      watches_[i] = watches_[i + 1];
    }
    --watch_count_;
    watches_[watch_count_].path[0] = '\0';
  }

  ImGui::Separator();

  // REPL section.
  ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "REPL");
  ImGui::SameLine();
  if (ImGui::SmallButton(repl_lang_ == kFennel ? "Fennel" : "Lua")) {
    repl_lang_ = (repl_lang_ == kLua) ? kFennel : kLua;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear")) {
    while (!repl_entries_->empty()) repl_entries_->Pop();
  }

  // Scrollable output.
  float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  if (ImGui::BeginChild("##repl_output", ImVec2(0, -footer), false,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    size_t count = repl_entries_->size();
    for (size_t i = 0; i < count; ++i) {
      const auto& entry = (*repl_entries_)[i];
      if (entry.is_input) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextUnformatted(entry.text);
        ImGui::PopStyleColor();
      } else if (entry.is_error) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextUnformatted(entry.text);
        ImGui::PopStyleColor();
      } else {
        ImGui::TextUnformatted(entry.text);
      }
    }
    if (repl_scroll_to_bottom_) {
      ImGui::SetScrollHereY(1.0f);
      repl_scroll_to_bottom_ = false;
    }
  }
  ImGui::EndChild();

  // REPL input.
  bool reclaim_focus = false;
  ImGuiInputTextFlags input_flags =
      ImGuiInputTextFlags_EnterReturnsTrue |
      ImGuiInputTextFlags_EscapeClearsAll |
      ImGuiInputTextFlags_CallbackHistory;
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##repl_eval", repl_input_, kEvalInputSize,
                       input_flags, ReplHistoryCallback, this)) {
    if (repl_input_[0] != '\0' && engine_ != nullptr) {
      // Add input echo.
      ReplEntry input_entry;
      input_entry.is_input = true;
      input_entry.is_error = false;
      snprintf(input_entry.text, sizeof(input_entry.text), "> %s",
               repl_input_);
      repl_entries_->Push(input_entry);

      // Add to shared eval history (skip consecutive duplicates).
      if (eval_history_count_ == 0 ||
          std::string_view(eval_history_entries_[(eval_history_count_ - 1) %
                                                 kEvalHistoryMax]) !=
              repl_input_) {
        int idx = eval_history_count_ % kEvalHistoryMax;
        snprintf(eval_history_entries_[idx], kEvalInputSize, "%s",
                 repl_input_);
        eval_history_count_++;
      }
      eval_history_pos_ = -1;

      // Evaluate (compile Fennel to Lua first if in Fennel mode).
      FixedStringBuffer<kMaxLogLineLength> result(kTruncating);
      bool ok = false;
      if (repl_lang_ == kFennel) {
        FixedStringBuffer<kMaxLogLineLength> compiled(kTruncating);
        if (CompileFennel(engine_->lua.state(), repl_input_, &compiled)) {
          ok = engine_->lua.EvalString(compiled.view(), &result);
        } else {
          result.Append(compiled.view());
        }
      } else {
        ok = engine_->lua.EvalString(repl_input_, &result);
      }
      if (result.view().size() > 0) {
        ReplEntry result_entry;
        result_entry.is_input = false;
        result_entry.is_error = !ok;
        snprintf(result_entry.text, sizeof(result_entry.text), "%s%s",
                 ok ? "=> " : "", result.str());
        repl_entries_->Push(result_entry);
      }
      repl_input_[0] = '\0';
      repl_scroll_to_bottom_ = true;
    }
    reclaim_focus = true;
  }
  if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);

  ImGui::End();
}

void DebugUI::DrawAssetViewer() {
  ImGui::SetNextWindowPos(ImVec2(400, 300), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Assets", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##asset_filter", "Filter by name...",
                           asset_filter_, sizeof(asset_filter_));
  ImGui::Separator();

  bool has_filter = asset_filter_[0] != '\0';
  Renderer* renderer = &engine_->renderer;
  Sound* sound = &engine_->sound;
  sqlite3* db = engine_->db;

  if (ImGui::BeginTabBar("AssetTabs")) {
    // Images tab.
    if (ImGui::BeginTabItem("Images")) {
      auto images = renderer->GetImages();
      for (size_t i = 0; i < images.size(); ++i) {
        const auto& img = images[i];
        if (has_filter && std::string_view(img.name.data()).find(asset_filter_) ==
                    std::string_view::npos) {
          continue;
        }
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode("##img", "%.*s (%zux%zu)",
                            static_cast<int>(img.name.size()), img.name.data(),
                            img.width, img.height)) {
          GLuint tex = renderer->GetTextureByName(img.name);
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
            if (ImGui::SmallButton("Zoom")) {
              zoom_texture_ = tex;
              zoom_tex_w_ = static_cast<float>(img.width);
              zoom_tex_h_ = static_cast<float>(img.height);
              zoom_level_ = 1.0f;
              // Read back pixel data for color sampling.
              if (zoom_pixels_ != nullptr) {
                allocator_->Dealloc(zoom_pixels_, zoom_pixels_size_);
              }
              zoom_pixels_size_ = img.width * img.height * 4;
              zoom_pixels_ = static_cast<uint8_t*>(
                  allocator_->Alloc(zoom_pixels_size_, /*align=*/1));
              glBindTexture(GL_TEXTURE_2D, tex);
              glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                            zoom_pixels_);
              glBindTexture(GL_TEXTURE_2D, 0);
            }
          }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::EndTabItem();
    }

    // Sprites tab.
    if (ImGui::BeginTabItem("Sprites")) {
      auto sprites = renderer->GetSprites();
      for (size_t i = 0; i < sprites.size(); ++i) {
        const auto& spr = sprites[i];
        if (has_filter && std::string_view(spr.name.data()).find(asset_filter_) ==
                    std::string_view::npos) {
          continue;
        }
        ImGui::PushID(static_cast<int>(i));
        auto* sheet = renderer->GetSpritesheet(spr.spritesheet);
        if (sheet != nullptr) {
          if (ImGui::TreeNode("##spr", "%.*s (%zux%zu)",
                              static_cast<int>(spr.name.size()),
                              spr.name.data(), spr.width, spr.height)) {
            GLuint tex = renderer->GetTextureByName(sheet->name);
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
              if (display_w < 64) {
                float s = 64.0f / display_w;
                display_w *= s;
                display_h *= s;
              }
              ImGui::Image(
                  static_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
                  ImVec2(display_w, display_h), ImVec2(u0, v1),
                  ImVec2(u1, v0));
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
    if (ImGui::BeginTabItem("Audio")) {
      sqlite3_stmt* stmt = nullptr;
      const char* sql =
          "SELECT name, channels, samplerate, samples, length(contents) "
          "FROM audios ORDER BY name";
      if (db != nullptr &&
          sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
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
            if (has_filter && std::string_view(name).find(asset_filter_) ==
                    std::string_view::npos) continue;
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
            SmallBuffer sz;
            FormatBytes(&sz, static_cast<size_t>(size_bytes));
            ImGui::TextUnformatted(sz.str());
            ImGui::TableNextColumn();
            ImGui::PushID(name);
            bool is_playing =
                has_preview_ && preview_name_.view() == name;
            if (ImGui::SmallButton(is_playing ? "Stop" : "Play")) {
              if (is_playing) {
                (void)sound->Stop(preview_source_);
                has_preview_ = false;
                preview_name_.Clear();
              } else {
                if (has_preview_) (void)sound->Stop(preview_source_);
                auto result =
                    sound->AddEffect(name, Sound::Ownership::kAutoFree);
                if (!result.is_error()) {
                  preview_source_ = result.value();
                  preview_name_.Clear();
                  preview_name_.Append(name);
                  has_preview_ = true;
                  (void)sound->StartChannel(preview_source_);
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
    if (db != nullptr && ImGui::BeginTabItem("Scripts")) {
      sqlite3_stmt* stmt = nullptr;
      const char* sql =
          "SELECT name, length(contents) FROM scripts ORDER BY name";
      if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
          const char* name = reinterpret_cast<const char*>(
              sqlite3_column_text(stmt, 0));
          int size = sqlite3_column_int(stmt, 1);
          if (name == nullptr) continue;
          if (has_filter && std::string_view(name).find(asset_filter_) ==
                    std::string_view::npos) continue;
          SmallBuffer sz;
          FormatBytes(&sz, static_cast<size_t>(size));
          ImGui::Text("%s  (%s)", name, sz.str());
        }
        sqlite3_finalize(stmt);
      }
      ImGui::EndTabItem();
    }

    // Shaders tab.
    if (db != nullptr && ImGui::BeginTabItem("Shaders")) {
      sqlite3_stmt* stmt = nullptr;
      const char* sql =
          "SELECT name, shader_type, length(contents) FROM shaders "
          "ORDER BY name";
      if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
          const char* name = reinterpret_cast<const char*>(
              sqlite3_column_text(stmt, 0));
          const char* type = reinterpret_cast<const char*>(
              sqlite3_column_text(stmt, 1));
          int size = sqlite3_column_int(stmt, 2);
          if (name == nullptr) continue;
          if (has_filter && std::string_view(name).find(asset_filter_) ==
                    std::string_view::npos) continue;
          SmallBuffer sz;
          FormatBytes(&sz, static_cast<size_t>(size));
          ImGui::Text("%s  [%s]  (%s)", name, type ? type : "?", sz.str());
        }
        sqlite3_finalize(stmt);
      }
      ImGui::EndTabItem();
    }

    // Fonts tab.
    if (db != nullptr && ImGui::BeginTabItem("Fonts")) {
      sqlite3_stmt* stmt = nullptr;
      const char* sql =
          "SELECT name, length(contents) FROM fonts ORDER BY name";
      if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
          const char* name = reinterpret_cast<const char*>(
              sqlite3_column_text(stmt, 0));
          int size = sqlite3_column_int(stmt, 1);
          if (name == nullptr) continue;
          if (has_filter && std::string_view(name).find(asset_filter_) ==
                    std::string_view::npos) continue;
          SmallBuffer sz;
          FormatBytes(&sz, static_cast<size_t>(size));
          ImGui::Text("%s  (%s)", name, sz.str());
        }
        sqlite3_finalize(stmt);
      }
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }
  ImGui::End();
}

}  // namespace G

#endif  // GAME_WITH_IMGUI
