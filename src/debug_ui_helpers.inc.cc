// Debug UI shared helpers. Included by debug_ui.cc (unity build).
// This file is not a standalone translation unit.

// Copies src into a fixed-size char buffer with guaranteed null termination.
void CopyToBuffer(char* dst, size_t dst_size, std::string_view src) {
  size_t len = src.size() < dst_size - 1 ? src.size() : dst_size - 1;
  memcpy(dst, src.data(), len);
  dst[len] = '\0';
}

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

// Returns true if name contains the filter substring (or filter is empty).
bool MatchesFilter(std::string_view name, const char* filter) {
  if (filter[0] == '\0') return true;
  return name.find(filter) != std::string_view::npos;
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
  SmallBuffer overlay;
  overlay.AppendF("%.1f %s (min %.1f  avg %.1f  max %.1f)",
                  static_cast<double>(values[count - 1]), unit,
                  static_cast<double>(min_val),
                  static_cast<double>(sum / static_cast<float>(count)),
                  static_cast<double>(max_val));
  ImGui::PlotLines(label, values, static_cast<int>(count),
                   /*values_offset=*/0, overlay.str(), /*scale_min=*/0.0f,
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
        lua_getfield(L, -1, "__tostring");
        if (lua_isfunction(L, -1)) {
          lua_pushvalue(L, idx);
          if (lua_pcall(L, 1, 1, 0) == 0 && lua_isstring(L, -1)) {
            buf->Append(lua_tostring(L, -1));
            lua_pop(L, 2);
            break;
          }
          lua_pop(L, 1);
        } else {
          lua_pop(L, 1);
        }
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

// Lua registry reference for the cached fennel compiler table.
int g_fennel_ref = LUA_NOREF;

// Compiles a Fennel string to Lua source using fennel.compileString.
// Caches the fennel module reference after first lookup.
// Returns true with compiled Lua in output, false with error in output.
bool CompileFennel(lua_State* L, std::string_view code, StringBuffer* output) {
  int top = lua_gettop(L);

  // Use cached reference if available.
  if (g_fennel_ref != LUA_NOREF) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_fennel_ref);
  } else {
    // Find the fennel module: try _fennel global, package.loaded, require.
    lua_getglobal(L, "_fennel");
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_getfield(L, LUA_GLOBALSINDEX, "package");
      if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "loaded");
        if (lua_istable(L, -1)) {
          lua_getfield(L, -1, "fennel");
          lua_replace(L, -3);
          lua_pop(L, 1);
        } else {
          lua_pop(L, 1);
        }
      }
    }
    // Last resort: try require("fennel"). This is slow (loads the compiler
    // from scratch) but only happens once since we cache the result.
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_getglobal(L, "require");
      if (lua_isfunction(L, -1)) {
        lua_pushstring(L, "fennel");
        if (lua_pcall(L, 1, 1, 0) != 0) {
          lua_pop(L, 1);
          lua_pushnil(L);
        }
      }
    }
    if (!lua_istable(L, -1)) {
      lua_settop(L, top);
      output->Append("Fennel compiler not loaded");
      return false;
    }
    // Cache the reference for future calls.
    lua_pushvalue(L, -1);
    g_fennel_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  // Call fennel.compileString(code).
  lua_getfield(L, -1, "compileString");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    output->Append("fennel.compileString not found");
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
