// Asset viewer with image, sprite, audio, script, shader, and font tabs.
// Included by debug_ui.cc (unity build). Not a standalone translation unit.
void DebugUI::DrawAssetImagesTab() {
  Renderer* renderer = &engine_->renderer;
  auto images = renderer->GetImages();
  for (size_t i = 0; i < images.size(); ++i) {
    const auto& img = images[i];
    if (!MatchesFilter(img.name.data(), asset_filter_)) continue;
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::TreeNode("##img", "%.*s (%zux%zu)",
                        static_cast<int>(img.name.size()), img.name.data(),
                        img.width, img.height)) {
      GLuint tex = renderer->GetTextureByName(img.name);
      if (tex != 0) {
        float max_w = ImGui::GetContentRegionAvail().x;
        float scale = 1.0f;
        if (static_cast<float>(img.width) > max_w) {
          scale = max_w / static_cast<float>(img.width);
        }
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
                     ImVec2(static_cast<float>(img.width) * scale,
                            static_cast<float>(img.height) * scale));
        if (ImGui::SmallButton("Zoom")) {
          zoom_texture_ = tex;
          zoom_tex_w_ = static_cast<float>(img.width);
          zoom_tex_h_ = static_cast<float>(img.height);
          zoom_level_ = 1.0f;
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
}

void DebugUI::DrawAssetSpritesTab() {
  Renderer* renderer = &engine_->renderer;
  auto sprites = renderer->GetSprites();
  for (size_t i = 0; i < sprites.size(); ++i) {
    const auto& spr = sprites[i];
    if (!MatchesFilter(spr.name.data(), asset_filter_)) continue;
    ImGui::PushID(static_cast<int>(i));
    auto* sheet = renderer->GetSpritesheet(spr.spritesheet);
    if (sheet != nullptr) {
      if (ImGui::TreeNode("##spr", "%.*s (%zux%zu)",
                          static_cast<int>(spr.name.size()), spr.name.data(),
                          spr.width, spr.height)) {
        GLuint tex = renderer->GetTextureByName(sheet->name);
        if (tex != 0 && sheet->width > 0 && sheet->height > 0) {
          float u0 =
              static_cast<float>(spr.x) / static_cast<float>(sheet->width);
          float v0 =
              static_cast<float>(spr.y) / static_cast<float>(sheet->height);
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
          ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
                       ImVec2(display_w, display_h), ImVec2(u0, v0),
                       ImVec2(u1, v1));
        }
        ImGui::Text("Sheet: %.*s  Pos: %zu,%zu",
                    static_cast<int>(spr.spritesheet.size()),
                    spr.spritesheet.data(), spr.x, spr.y);
        ImGui::TreePop();
      }
    }
    ImGui::PopID();
  }
}

void DebugUI::DrawAssetAudioTab() {
  Sound* sound = &engine_->sound;
  sqlite3* db = engine_->db;
  if (db == nullptr) return;
  SqlStmt stmt(db,
               "SELECT name, channels, samplerate, samples, length(contents) "
               "FROM audios ORDER BY name");
  if (!stmt.ok()) return;
  if (ImGui::BeginTable("AudioTable", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY,
                        ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Ch", ImGuiTableColumnFlags_WidthFixed, 25);
    ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthFixed, 50);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 65);
    ImGui::TableSetupColumn("##play", ImGuiTableColumnFlags_WidthFixed, 40);
    ImGui::TableHeadersRow();
    while (MUST(stmt.Step())) {
      auto name = stmt.ColumnText(0);
      if (name.empty()) continue;
      if (!MatchesFilter(name, asset_filter_)) continue;
      int channels = stmt.ColumnInt(1);
      int samplerate = stmt.ColumnInt(2);
      int size_bytes = stmt.ColumnInt(4);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(name.data());
      ImGui::TableNextColumn();
      ImGui::Text("%d", channels);
      ImGui::TableNextColumn();
      ImGui::Text("%d", samplerate);
      ImGui::TableNextColumn();
      SmallBuffer sz;
      FormatBytes(&sz, static_cast<size_t>(size_bytes));
      ImGui::TextUnformatted(sz.str());
      ImGui::TableNextColumn();
      ImGui::PushID(name.data());
      bool is_playing = has_preview_ && preview_name_.view() == name;
      if (ImGui::SmallButton(is_playing ? "Stop" : "Play")) {
        if (is_playing) {
          auto stop = sound->Stop(preview_source_);
          if (!stop.is_error()) has_preview_ = false;
          preview_name_.Clear();
        } else {
          if (has_preview_) {
            auto stop = sound->Stop(preview_source_);
            if (!stop.is_error()) has_preview_ = false;
          }
          auto result =
              sound->AddEffect(name.data(), Sound::Ownership::kAutoFree);
          if (!result.is_error()) {
            preview_source_ = result.value();
            preview_name_.Clear();
            preview_name_.Append(name);
            has_preview_ = true;
            auto start = sound->StartChannel(preview_source_);
            if (start.is_error()) has_preview_ = false;
          }
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void DebugUI::DrawAssetDbTab(const char* label, const char* sql) {
  sqlite3* db = engine_->db;
  if (db == nullptr || !ImGui::BeginTabItem(label)) return;
  SqlStmt stmt(db, sql);
  if (!stmt.ok()) {
    ImGui::EndTabItem();
    return;
  }
  while (MUST(stmt.Step())) {
    auto name = stmt.ColumnText(0);
    int size = stmt.ColumnInt(1);
    if (name.empty()) continue;
    if (!MatchesFilter(name, asset_filter_)) continue;
    SmallBuffer sz;
    FormatBytes(&sz, static_cast<size_t>(size));
    ImGui::Text("%s  (%s)", name.data(), sz.str());
  }
  ImGui::EndTabItem();
}

// Shared helper for the script/shader editor tabs. Loads content from DB,
// displays in a TextEditor, and optionally saves + triggers hot reload.
namespace {

void DrawCodeEditorTab(Engine* engine, const char* table_name,
                       const TextEditor::Language* lang, const char* filter,
                       DebugUI::CodeEditorState* state) {
  TextEditor* editor = &state->editor;
  PathBuffer* loaded_name = &state->loaded_name;
  bool* read_only = &state->read_only;
  sqlite3* db = engine->db;
  if (db == nullptr) return;

  // Left: script list.
  float list_width = 180.0f;
  if (ImGui::BeginChild("##list", ImVec2(list_width, 0), true)) {
    SmallBuffer sql;
    sql.AppendF("SELECT name FROM %s ORDER BY name", table_name);
    SqlStmt stmt(db, sql.str());
    if (stmt.ok()) {
      while (MUST(stmt.Step())) {
        auto name = stmt.ColumnText(0);
        if (name.empty()) continue;
        if (!MatchesFilter(name, filter)) continue;
        bool selected = (loaded_name->view() == name);
        if (ImGui::Selectable(name.data(), selected)) {
          if (!selected) {
            SmallBuffer load_sql;
            load_sql.AppendF("SELECT contents FROM %s WHERE name = ?",
                             table_name);
            SqlStmt load_stmt(db, load_sql.str());
            if (load_stmt.ok()) {
              load_stmt.BindText(1, name);
              if (MUST(load_stmt.Step())) {
                auto contents = load_stmt.ColumnText(0);
                if (!contents.empty()) {
                  editor->SetText(std::string(contents));
                  if (lang != nullptr) {
                    editor->SetLanguage(lang);
                  } else {
                    if (name.size() > 4 &&
                        name.substr(name.size() - 4) == ".fnl") {
                      editor->SetLanguage(FennelLanguage());
                    } else {
                      editor->SetLanguage(TextEditor::Language::Lua());
                    }
                  }
                  loaded_name->Clear();
                  loaded_name->Append(name);
                  *read_only = true;
                  editor->SetReadOnlyEnabled(true);
                }
              }
            }
          }
        }
      }
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();

  // Right: editor.
  if (ImGui::BeginChild("##editor", ImVec2(0, 0))) {
    if (loaded_name->view().empty()) {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                         "Select a file from the list.");
    } else {
      ImGui::Text("%s", loaded_name->str());
      ImGui::SameLine();
      if (ImGui::SmallButton(*read_only ? "Edit" : "Read-only")) {
        *read_only = !*read_only;
        editor->SetReadOnlyEnabled(*read_only);
      }
      if (!*read_only) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Save & Reload")) {
          std::string text = editor->GetText();
          SmallBuffer save_sql;
          save_sql.AppendF("UPDATE %s SET contents = ? WHERE name = ?",
                           table_name);
          SqlStmt save_stmt(db, save_sql.str());
          if (save_stmt.ok()) {
            save_stmt.BindText(1, text);
            save_stmt.BindText(2, loaded_name->view());
            auto step = save_stmt.Step();
            if (!step.is_error()) {
              engine->lua.RequestHotload();
            }
          }
        }
      }
      editor->Render("##code_editor",
                     ImVec2(0, ImGui::GetContentRegionAvail().y));
    }
  }
  ImGui::EndChild();
}

}  // namespace

void DebugUI::DrawAssetScriptsTab() {
  DrawCodeEditorTab(engine_, "scripts", /*lang=*/nullptr, asset_filter_,
                    &script_editor_);
}

void DebugUI::DrawAssetShadersTab() {
  DrawCodeEditorTab(engine_, "shaders", TextEditor::Language::Glsl(),
                    asset_filter_, &shader_editor_);
}

void DebugUI::DrawAssetSqlTab() {
  sqlite3* db = engine_->db;
  if (db == nullptr) return;

  ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                              ImGuiInputTextFlags_CtrlEnterForNewLine;
  bool run = false;
  ImGui::SetNextItemWidth(-60);
  if (ImGui::InputTextMultiline("##sql_input", sql_input_, kSqlInputSize,
                                ImVec2(-60, ImGui::GetTextLineHeight() * 3),
                                flags)) {
    run = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Run") || run) {
    sql_results_.has_error = false;
    sql_results_.col_count = 0;
    sql_results_.row_count = 0;
    sql_results_.error[0] = '\0';

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql_input_, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      sql_results_.has_error = true;
      CopyToBuffer(sql_results_.error, sizeof(sql_results_.error),
                   sqlite3_errmsg(db));
    } else {
      sql_results_.col_count = sqlite3_column_count(stmt);
      if (sql_results_.col_count > SqlResults::kMaxCols) {
        sql_results_.col_count = SqlResults::kMaxCols;
      }
      for (int c = 0; c < sql_results_.col_count; ++c) {
        const char* name = sqlite3_column_name(stmt, c);
        CopyToBuffer(sql_results_.col_names[c], SqlResults::kCellSize,
                     name ? name : "?");
      }
      int row = 0;
      while (sqlite3_step(stmt) == SQLITE_ROW && row < SqlResults::kMaxRows) {
        for (int c = 0; c < sql_results_.col_count; ++c) {
          const char* val =
              reinterpret_cast<const char*>(sqlite3_column_text(stmt, c));
          CopyToBuffer(sql_results_.cells[row][c], SqlResults::kCellSize,
                       val ? val : "NULL");
        }
        ++row;
      }
      sql_results_.row_count = row;
      sqlite3_finalize(stmt);
    }
  }

  // Display results.
  if (sql_results_.has_error) {
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                       sql_results_.error);
  } else if (sql_results_.col_count > 0) {
    ImGui::Text("%d rows", sql_results_.row_count);
    if (ImGui::BeginTable("##sql_results", sql_results_.col_count,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0, 0))) {
      for (int c = 0; c < sql_results_.col_count; ++c) {
        ImGui::TableSetupColumn(sql_results_.col_names[c]);
      }
      ImGui::TableHeadersRow();
      for (int r = 0; r < sql_results_.row_count; ++r) {
        ImGui::TableNextRow();
        for (int c = 0; c < sql_results_.col_count; ++c) {
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(sql_results_.cells[r][c]);
        }
      }
      ImGui::EndTable();
    }
  }
}

void DebugUI::DrawAssetViewer() {
  ImGui::SetNextWindowPos(ImVec2(400, 300), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Assets", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##asset_filter", "Filter by name...", asset_filter_,
                           sizeof(asset_filter_));
  ImGui::Separator();

  if (ImGui::BeginTabBar("AssetTabs")) {
    if (ImGui::BeginTabItem("Images")) {
      DrawAssetImagesTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Sprites")) {
      DrawAssetSpritesTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Audio")) {
      DrawAssetAudioTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Scripts")) {
      DrawAssetScriptsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Shaders")) {
      DrawAssetShadersTab();
      ImGui::EndTabItem();
    }
    DrawAssetDbTab("Fonts",
                   "SELECT name, length(contents) FROM fonts ORDER BY name");
    if (ImGui::BeginTabItem("SQL")) {
      DrawAssetSqlTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}
