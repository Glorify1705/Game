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
}

void DebugUI::DrawAssetAudioTab() {
  Sound* sound = &engine_->sound;
  sqlite3* db = engine_->db;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT name, channels, samplerate, samples, length(contents) "
      "FROM audios ORDER BY name";
  if (db == nullptr ||
      sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return;
  }
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
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char* name = reinterpret_cast<const char*>(
          sqlite3_column_text(stmt, 0));
      if (name == nullptr) continue;
      if (!MatchesFilter(name, asset_filter_)) continue;
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
              sound->AddEffect(name, Sound::Ownership::kAutoFree);
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
  sqlite3_finalize(stmt);
}

void DebugUI::DrawAssetDbTab(const char* label, const char* sql) {
  sqlite3* db = engine_->db;
  if (db == nullptr || !ImGui::BeginTabItem(label)) return;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char* name = reinterpret_cast<const char*>(
          sqlite3_column_text(stmt, 0));
      int size = sqlite3_column_int(stmt, 1);
      if (name == nullptr) continue;
      if (!MatchesFilter(name, asset_filter_)) continue;
      SmallBuffer sz;
      FormatBytes(&sz, static_cast<size_t>(size));
      ImGui::Text("%s  (%s)", name, sz.str());
    }
    sqlite3_finalize(stmt);
  }
  ImGui::EndTabItem();
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
    DrawAssetDbTab("Scripts",
                   "SELECT name, length(contents) FROM scripts ORDER BY name");
    DrawAssetDbTab("Shaders",
                   "SELECT name, length(contents) FROM shaders ORDER BY name");
    DrawAssetDbTab("Fonts",
                   "SELECT name, length(contents) FROM fonts ORDER BY name");
    ImGui::EndTabBar();
  }
  ImGui::End();
}
