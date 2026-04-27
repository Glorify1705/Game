// Save browser panel.
// Included by debug_ui.cc (unity build). Not a standalone translation unit.

void DebugUI::DrawSavePanel() {
  ImGui::SetNextWindowPos(ImVec2(420, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Save Data", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  Save* save = &engine_->save;
  if (!save->IsOpen()) {
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Save database not open");
    ImGui::End();
    return;
  }

  // Collect namespaces.
  struct NsCollector {
    const char* names[64];
    int count = 0;
  };
  NsCollector ns_list;
  (void)save->Namespaces(
      [](std::string_view name, void* ud) {
        auto* c = static_cast<NsCollector*>(ud);
        if (c->count < 64) c->names[c->count++] = name.data();
      },
      &ns_list);

  if (ns_list.count == 0) {
    ImGui::TextDisabled("No saved data");
    ImGui::End();
    return;
  }

  // Tab bar with one tab per namespace.
  if (ImGui::BeginTabBar("SaveNamespaces")) {
    for (int i = 0; i < ns_list.count; ++i) {
      if (ImGui::BeginTabItem(ns_list.names[i])) {
        std::string_view ns = ns_list.names[i];

        // List all keys in this namespace as a table.
        if (ImGui::BeginTable("kv", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
          ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed,
                                  150.0f);
          ImGui::TableSetupColumn("Value");
          ImGui::TableHeadersRow();

          (void)save->List(
              ns,
              [](std::string_view key, ByteSlice value, void* ud) {
                (void)ud;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(key.data(), key.data() + key.size());
                ImGui::TableNextColumn();
                // Show value as JSON string (raw blob content).
                if (value.size() == 0) {
                  ImGui::TextDisabled("(empty)");
                } else {
                  std::string_view json(
                      reinterpret_cast<const char*>(value.data()),
                      value.size());
                  ImGui::TextWrapped("%.*s", static_cast<int>(json.size()),
                                     json.data());
                }
              },
              nullptr);

          ImGui::EndTable();
        }

        // Clear namespace button.
        if (ImGui::Button("Clear Namespace")) {
          (void)save->Clear(ns);
        }

        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}
