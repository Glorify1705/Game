// Tilemap debug panel.
// Included by debug_ui.cc (unity build). Not a standalone translation unit.

void DebugUI::DrawTilemapPanel() {
  ImGui::SetNextWindowPos(ImVec2(420, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(480, 550), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Tilemap", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  Tilemap* tilemap = Tilemap::debug_active_tilemap;
  if (tilemap == nullptr) {
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No tilemap active");
    ImGui::End();
    return;
  }

  // Tilemap info section.
  if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Tile size: %dx%d px", tilemap->tile_width(),
                tilemap->tile_height());
    ImGui::Text("Layers: %d", tilemap->layer_count());
    std::string_view tileset = tilemap->tileset();
    if (!tileset.empty()) {
      ImGui::Text("Tileset: %.*s", static_cast<int>(tileset.size()),
                  tileset.data());
    } else {
      ImGui::TextDisabled("Tileset: (none)");
    }
  }

  // Layer list section.
  if (ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginTable("layers", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn("Name");
      ImGui::TableSetupColumn("Size");
      ImGui::TableSetupColumn("Tiles");
      ImGui::TableSetupColumn("Vis");
      ImGui::TableSetupColumn("Col");
      ImGui::TableHeadersRow();

      for (int i = 0; i < tilemap->layer_count(); ++i) {
        TilemapLayer* layer = tilemap->layer(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", layer->name);
        ImGui::TableNextColumn();
        ImGui::Text("%dx%d", layer->width, layer->height);
        ImGui::TableNextColumn();
        // Count non-zero tiles.
        int tile_count = 0;
        int total = layer->width * layer->height;
        for (int t = 0; t < total; ++t) {
          if (layer->tiles[t] != 0) ++tile_count;
        }
        ImGui::Text("%d", tile_count);
        ImGui::TableNextColumn();
        ImGui::PushID(i * 2);
        ImGui::Checkbox("##vis", &layer->visible);
        ImGui::PopID();
        ImGui::TableNextColumn();
        ImGui::PushID(i * 2 + 1);
        ImGui::Checkbox("##col", &layer->collision);
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
  }

  // Tile inspector (hover info) section.
  if (ImGui::CollapsingHeader("Tile Inspector",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImVec2 mouse = ImGui::GetIO().MousePos;
    IVec2 vp = engine_->batch_renderer.GetViewport();
    FVec2 viewport(static_cast<float>(vp.x), static_cast<float>(vp.y));

    // Convert mouse screen position to world coordinates.
    FVec2 world_pos =
        engine_->camera.ToWorld(FVec2(mouse.x, mouse.y), viewport);
    int tx, ty;
    tilemap->WorldToTile(world_pos.x, world_pos.y, &tx, &ty);

    ImGui::Text("Mouse screen: (%.0f, %.0f)", static_cast<double>(mouse.x),
                static_cast<double>(mouse.y));
    ImGui::Text("World: (%.1f, %.1f)", static_cast<double>(world_pos.x),
                static_cast<double>(world_pos.y));
    ImGui::Text("Tile: (%d, %d)", tx, ty);
    ImGui::Separator();

    // Show tile IDs for each layer at the mouse position.
    for (int i = 0; i < tilemap->layer_count(); ++i) {
      const TilemapLayer* layer = tilemap->layer(i);
      int tile_id = 0;
      if (tx >= 0 && tx < layer->width && ty >= 0 && ty < layer->height) {
        tile_id = layer->tiles[ty * layer->width + tx];
      }
      ImGui::Text("  %s: tile %d", layer->name, tile_id);

      // Show a small tile preview if the tile is non-zero and we have a tileset.
      if (tile_id > 0) {
        std::string_view tileset_name = tilemap->tileset();
        if (!tileset_name.empty()) {
          Renderer* renderer = &engine_->renderer;
          GLuint tex = renderer->GetTextureByName(tileset_name);
          float sheet_w = 0, sheet_h = 0;
          DbAssets::Spritesheet* sheet =
              renderer->GetSpritesheet(tileset_name);
          if (sheet != nullptr) {
            sheet_w = static_cast<float>(sheet->width);
            sheet_h = static_cast<float>(sheet->height);
          } else {
            DbAssets::Image* img = renderer->GetImage(tileset_name);
            if (img != nullptr) {
              sheet_w = static_cast<float>(img->width);
              sheet_h = static_cast<float>(img->height);
            }
          }
          if (tex != 0 && sheet_w > 0 && sheet_h > 0) {
            float tw = static_cast<float>(tilemap->tile_width());
            float th = static_cast<float>(tilemap->tile_height());
            int tiles_per_row = static_cast<int>(sheet_w) / tilemap->tile_width();
            if (tiles_per_row > 0) {
              int tile_col = (tile_id - 1) % tiles_per_row;
              int tile_row = (tile_id - 1) / tiles_per_row;
              float u0 = (tile_col * tw) / sheet_w;
              float v0 = (tile_row * th) / sheet_h;
              float u1 = ((tile_col + 1) * tw) / sheet_w;
              float v1 = ((tile_row + 1) * th) / sheet_h;
              // Display at 2x scale for visibility.
              float display_size = 32.0f;
              ImGui::SameLine();
              ImGui::Image(
                  static_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
                  ImVec2(display_size, display_size), ImVec2(u0, v1),
                  ImVec2(u1, v0));
            }
          }
        }
      }
    }
  }

  // Grid overlay toggle.
  if (ImGui::CollapsingHeader("Grid Overlay")) {
    ImGui::Checkbox("Show grid", &tilemap_grid_visible_);
    if (tilemap_grid_visible_) {
      ImGui::TextDisabled("Grid lines drawn at tile boundaries");
    }
  }

  // Draw grid lines on the background draw list.
  if (tilemap_grid_visible_) {
    IVec2 vp = engine_->batch_renderer.GetViewport();
    FVec2 viewport(static_cast<float>(vp.x), static_cast<float>(vp.y));
    float tw = static_cast<float>(tilemap->tile_width());
    float th = static_cast<float>(tilemap->tile_height());

    // Find visible tile range from camera.
    FVec2 cam_pos = engine_->camera.GetPosition();
    float zoom = engine_->camera.GetZoom();
    float half_vw = (viewport.x / zoom) * 0.5f;
    float half_vh = (viewport.y / zoom) * 0.5f;
    float view_left = cam_pos.x - half_vw;
    float view_top = cam_pos.y - half_vh;
    float view_right = cam_pos.x + half_vw;
    float view_bottom = cam_pos.y + half_vh;

    int start_col = static_cast<int>(std::floor(view_left / tw));
    int end_col = static_cast<int>(std::ceil(view_right / tw));
    int start_row = static_cast<int>(std::floor(view_top / th));
    int end_row = static_cast<int>(std::ceil(view_bottom / th));

    // Clamp to reasonable range to avoid excessive lines.
    int max_lines = 200;
    if (end_col - start_col > max_lines) {
      end_col = start_col + max_lines;
    }
    if (end_row - start_row > max_lines) {
      end_row = start_row + max_lines;
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImU32 grid_color = IM_COL32(255, 255, 255, 40);

    // Vertical lines.
    for (int col = start_col; col <= end_col; ++col) {
      FVec2 world_top(col * tw, view_top);
      FVec2 world_bot(col * tw, view_bottom);
      FVec2 screen_top = engine_->camera.ToScreen(world_top, viewport);
      FVec2 screen_bot = engine_->camera.ToScreen(world_bot, viewport);
      dl->AddLine(ImVec2(screen_top.x, screen_top.y),
                  ImVec2(screen_bot.x, screen_bot.y), grid_color);
    }

    // Horizontal lines.
    for (int row = start_row; row <= end_row; ++row) {
      FVec2 world_left(view_left, row * th);
      FVec2 world_right(view_right, row * th);
      FVec2 screen_left = engine_->camera.ToScreen(world_left, viewport);
      FVec2 screen_right = engine_->camera.ToScreen(world_right, viewport);
      dl->AddLine(ImVec2(screen_left.x, screen_left.y),
                  ImVec2(screen_right.x, screen_right.y), grid_color);
    }
  }

  // Tileset viewer section.
  if (ImGui::CollapsingHeader("Tileset Viewer")) {
    std::string_view tileset_name = tilemap->tileset();
    if (tileset_name.empty()) {
      ImGui::TextDisabled("No tileset assigned");
    } else {
      Renderer* renderer = &engine_->renderer;
      GLuint tex = renderer->GetTextureByName(tileset_name);
      float sheet_w = 0, sheet_h = 0;
      DbAssets::Spritesheet* sheet = renderer->GetSpritesheet(tileset_name);
      if (sheet != nullptr) {
        sheet_w = static_cast<float>(sheet->width);
        sheet_h = static_cast<float>(sheet->height);
      } else {
        DbAssets::Image* img = renderer->GetImage(tileset_name);
        if (img != nullptr) {
          sheet_w = static_cast<float>(img->width);
          sheet_h = static_cast<float>(img->height);
        }
      }

      if (tex == 0 || sheet_w <= 0 || sheet_h <= 0) {
        ImGui::TextDisabled("Tileset texture not loaded");
      } else {
        float tw = static_cast<float>(tilemap->tile_width());
        float th = static_cast<float>(tilemap->tile_height());
        int tiles_per_row = static_cast<int>(sheet_w) / tilemap->tile_width();
        int tiles_per_col = static_cast<int>(sheet_h) / tilemap->tile_height();

        ImGui::Text("Sheet: %.0fx%.0f  Tiles: %dx%d",
                    static_cast<double>(sheet_w), static_cast<double>(sheet_h),
                    tiles_per_row, tiles_per_col);

        // Scale to fit available width.
        float avail_w = ImGui::GetContentRegionAvail().x;
        float scale = 1.0f;
        if (sheet_w > avail_w && avail_w > 0) {
          scale = avail_w / sheet_w;
        }
        float display_w = sheet_w * scale;
        float display_h = sheet_h * scale;

        if (ImGui::BeginChild("TilesetScroll", ImVec2(0, 300), true,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
          ImVec2 img_pos = ImGui::GetCursorScreenPos();
          // Display tileset image (V-flipped for OpenGL textures).
          ImGui::Image(
              static_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
              ImVec2(display_w, display_h), ImVec2(0, 1), ImVec2(1, 0));

          // Draw grid overlay on the tileset.
          ImDrawList* dl = ImGui::GetWindowDrawList();
          ImU32 line_color = IM_COL32(255, 255, 0, 80);
          float scaled_tw = tw * scale;
          float scaled_th = th * scale;

          // Vertical lines.
          for (int c = 0; c <= tiles_per_row; ++c) {
            float x = img_pos.x + c * scaled_tw;
            dl->AddLine(ImVec2(x, img_pos.y),
                        ImVec2(x, img_pos.y + display_h), line_color);
          }
          // Horizontal lines.
          for (int r = 0; r <= tiles_per_col; ++r) {
            float y = img_pos.y + r * scaled_th;
            dl->AddLine(ImVec2(img_pos.x, y),
                        ImVec2(img_pos.x + display_w, y), line_color);
          }

          // Hover: show tile ID tooltip.
          if (ImGui::IsItemHovered()) {
            ImVec2 mouse = ImGui::GetMousePos();
            float rel_x = mouse.x - img_pos.x;
            float rel_y = mouse.y - img_pos.y;
            int hover_col = static_cast<int>(rel_x / scaled_tw);
            int hover_row = static_cast<int>(rel_y / scaled_th);
            if (hover_col >= 0 && hover_col < tiles_per_row &&
                hover_row >= 0 && hover_row < tiles_per_col) {
              int tile_id = hover_row * tiles_per_row + hover_col + 1;
              ImGui::BeginTooltip();
              ImGui::Text("Tile ID: %d", tile_id);
              ImGui::Text("Grid: (%d, %d)", hover_col, hover_row);

              // Show a zoomed preview of the hovered tile.
              float u0 = (hover_col * tw) / sheet_w;
              float v0 = (hover_row * th) / sheet_h;
              float u1 = ((hover_col + 1) * tw) / sheet_w;
              float v1 = ((hover_row + 1) * th) / sheet_h;
              ImGui::Image(
                  static_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
                  ImVec2(64, 64), ImVec2(u0, v1), ImVec2(u1, v0));
              ImGui::EndTooltip();
            }
          }
        }
        ImGui::EndChild();
      }
    }
  }

  ImGui::End();
}
