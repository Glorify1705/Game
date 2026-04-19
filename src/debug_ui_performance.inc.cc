// Performance panel with frame time graph and breakdown.
// Included by debug_ui.cc (unity build). Not a standalone translation unit.
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
