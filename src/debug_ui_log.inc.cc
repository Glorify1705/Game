// Log console with level filtering, text search, and Lua eval.
// Included by debug_ui.cc (unity build). Not a standalone translation unit.
bool DebugUI::ShouldShowLogEntry(const LogEntry& entry) const {
  int level_idx = static_cast<int>(entry.level);
  if (level_idx < 0 || level_idx > 5) return false;
  if (!log_level_filter_[level_idx]) return false;
  return MatchesFilter(entry.text, log_text_filter_);
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
    enum { kClipBufSize = 64 * 1024 };
    char clip[kClipBufSize];
    size_t pos = 0;
    size_t count = log_entries_->size();
    for (size_t i = 0; i < count && pos < kClipBufSize - 1; ++i) {
      const LogEntry& entry = (*log_entries_)[i];
      if (!ShouldShowLogEntry(entry)) continue;
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
      auto mkdir_result = MakeDirs(dir.str());
      if (mkdir_result.is_error()) return;
      CmdBuffer path(dir.str(), "/log_", static_cast<uint64_t>(SDL_GetTicks()),
                     ".txt");
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
  const char* level_names[] = {"Fatal", "Error", "Warn",
                               "Info",  "Debug", "Trace"};
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
    size_t count = log_entries_->size();
    for (size_t i = 0; i < count; ++i) {
      const LogEntry& entry = (*log_entries_)[i];
      if (!ShouldShowLogEntry(entry)) continue;
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
  ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue |
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
        CopyToBuffer(eval_history_entries_[idx], kEvalInputSize, eval_input_);
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
