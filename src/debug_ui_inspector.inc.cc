// Entity inspector showing _Game state and user globals.
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
