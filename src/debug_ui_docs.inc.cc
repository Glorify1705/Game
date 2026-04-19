// API documentation browser with search.
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
