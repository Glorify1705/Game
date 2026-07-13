// Hot zones profiler panel showing live timing statistics.
// Included by debug_ui.cc (unity build). Not a standalone translation unit.

void DebugUI::DrawZonesPanel() {
  ImGui::SetNextWindowPos(ImVec2(820, 440), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Hot Zones", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  ZoneStats* zs = GetZoneStats();
  if (ImGui::Button("Reset")) zs->Reset();
  ImGui::SameLine();
  ImGui::Text("%d zones", zs->zone_count());

  if (zs->zone_count() == 0) {
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       "No zones recorded yet.");
    ImGui::End();
    return;
  }

  // Sort zones by average time descending.
  int indices[ZoneStats::kMaxZones];
  int count = zs->zone_count();
  for (int i = 0; i < count; ++i) indices[i] = i;
  for (int i = 0; i < count - 1; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (zs->zone(indices[j]).stats.avg() >
          zs->zone(indices[i]).stats.avg()) {
        int tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
      }
    }
  }

  if (ImGui::BeginTable("##zones", 7,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Sortable,
                        ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableSetupColumn("p50", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableSetupColumn("p90", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableSetupColumn("p99", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableHeadersRow();

    for (int i = 0; i < count; ++i) {
      const auto& zone = zs->zone(indices[i]);
      const auto& s = zone.stats;
      double avg = s.avg();

      // Color code: green < 1ms, yellow 1-5ms, red > 5ms.
      ImVec4 color(0.2f, 0.8f, 0.2f, 1.0f);
      if (avg > 5.0) {
        color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
      } else if (avg > 1.0) {
        color = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(color, "%.*s",
                         static_cast<int>(zone.name.size()),
                         zone.name.data());
      ImGui::TableNextColumn();
      ImGui::Text("%.0f", s.samples());
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", avg);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", s.Percentile(50));
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", s.Percentile(90));
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", s.Percentile(99));
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", s.max());
    }
    ImGui::EndTable();
  }

  ImGui::End();
}
