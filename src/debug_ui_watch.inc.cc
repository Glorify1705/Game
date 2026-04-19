// Watch panel with live Lua expression evaluation and REPL.
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
      strncpy(watches_[watch_count_].path, watch_input_, kWatchPathSize - 1);
      watches_[watch_count_].path[kWatchPathSize - 1] = '\0';
      ++watch_count_;
      watch_input_[0] = '\0';
    }
  }
  ImGui::Separator();

  if (watch_count_ > 0) {
    bool has_error = engine_->lua.HasError();
    int remove_idx = -1;

    if (ImGui::BeginTable("##watches", 3,
                          ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Expr", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("##rm", ImGuiTableColumnFlags_WidthFixed,
                              20.0f);
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
  }

  ImGui::Separator();
  DrawRepl();

  ImGui::End();
}

void DebugUI::DrawRepl() {
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
  float footer =
      ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  if (ImGui::BeginChild("##repl_output", ImVec2(0, -footer), false,
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

  // Input.
  bool reclaim_focus = false;
  ImGuiInputTextFlags input_flags =
      ImGuiInputTextFlags_EnterReturnsTrue |
      ImGuiInputTextFlags_EscapeClearsAll |
      ImGuiInputTextFlags_CallbackHistory;
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##repl_eval", repl_input_, kEvalInputSize,
                       input_flags, ReplHistoryCallback, this)) {
    if (repl_input_[0] != '\0' && engine_ != nullptr) {
      // Echo input.
      ReplEntry input_entry;
      input_entry.is_input = true;
      input_entry.is_error = false;
      FixedStringBuffer<kMaxLogLineLength> echo(kTruncating);
      echo.Append("> ", repl_input_);
      strncpy(input_entry.text, echo.str(), sizeof(input_entry.text) - 1);
      input_entry.text[sizeof(input_entry.text) - 1] = '\0';
      repl_entries_->Push(input_entry);

      // Add to shared eval history (skip consecutive duplicates).
      if (eval_history_count_ == 0 ||
          std::string_view(eval_history_entries_[(eval_history_count_ - 1) %
                                                 kEvalHistoryMax]) !=
              repl_input_) {
        int idx = eval_history_count_ % kEvalHistoryMax;
        strncpy(eval_history_entries_[idx], repl_input_, kEvalInputSize - 1);
        eval_history_entries_[idx][kEvalInputSize - 1] = '\0';
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
      if (!result.view().empty()) {
        ReplEntry result_entry;
        result_entry.is_input = false;
        result_entry.is_error = !ok;
        FixedStringBuffer<kMaxLogLineLength> formatted(kTruncating);
        if (ok) formatted.Append("=> ");
        formatted.Append(result.view());
        strncpy(result_entry.text, formatted.str(),
                sizeof(result_entry.text) - 1);
        result_entry.text[sizeof(result_entry.text) - 1] = '\0';
        repl_entries_->Push(result_entry);
      }
      repl_input_[0] = '\0';
      repl_scroll_to_bottom_ = true;
    }
    reclaim_focus = true;
  }
  if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);
}
