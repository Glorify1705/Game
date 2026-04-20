// Audio, Memory, Renderer, Camera, and Physics panels.
// Included by debug_ui.cc (unity build). Not a standalone translation unit.
void DebugUI::DrawAudioPanel() {
  Sound* sound = &engine_->sound;
  ImGui::SetNextWindowPos(ImVec2(620, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 350), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Audio", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  float global_gain = sound->global_gain();
  if (ImGui::SliderFloat("Global Volume", &global_gain, 0.0f, 1.0f,
                          "%.2f")) {
    sound->SetGlobalGain(global_gain);
  }
  ImGui::Separator();

  size_t used = sound->stream_count();
  size_t total = sound->max_streams();
  ImGui::Text("Stream Slots: %zu / %zu", used, total);
  float slot_ratio = 0.0f;
  if (total > 0) {
    slot_ratio = static_cast<float>(used) / static_cast<float>(total);
  }
  ImGui::ProgressBar(slot_ratio, ImVec2(-1, 0));
  ImGui::Separator();

  if (used > 0 &&
      ImGui::BeginTable("Streams", 7,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollY,
                        ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableSetupColumn("Vol", ImGuiTableColumnFlags_WidthFixed, 40);
    ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, 40);
    ImGui::TableSetupColumn("Pan", ImGuiTableColumnFlags_WidthFixed, 40);
    ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed, 35);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableHeadersRow();

    Sound::StreamDebugInfo infos[128];
    sound->GetStreamDebugInfo(infos, 128);
    for (size_t i = 0; i < used; ++i) {
      const auto& info = infos[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      std::string_view name = StringByHandle(info.handle);
      ImGui::TextUnformatted(name.data(), name.data() + name.size());
      ImGui::TableNextColumn();
      if (info.playing) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Playing");
      } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Stopped");
      }
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", static_cast<double>(info.gain));
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", static_cast<double>(info.pitch));
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", static_cast<double>(info.pan));
      ImGui::TableNextColumn();
      ImGui::Text("%s", info.loop ? "Yes" : "No");
      ImGui::TableNextColumn();
      ImGui::Text("%s", info.managed ? "Managed" : "Auto");
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void DebugUI::DrawMemoryPanel(size_t lua_memory_bytes) {
  ImGui::SetNextWindowPos(ImVec2(620, 390), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380, 280), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Memory", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  if (engine_arena_ != nullptr &&
      ImGui::CollapsingHeader("Engine Arena",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawMemoryBar("Used / Total", engine_arena_->used_memory(),
                  engine_arena_->total_memory());
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Frame Allocator",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawMemoryBar("Used / Total", engine_->frame_allocator.used_memory(),
                  engine_->frame_allocator.total_memory());
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Lua Heap", ImGuiTreeNodeFlags_DefaultOpen)) {
    SmallBuffer lua_str;
    FormatBytes(&lua_str, lua_memory_bytes);
    ImGui::Text("Lua memory: %s", lua_str.str());
    size_t lua_count = lua_memory_samples_->size();
    if (lua_count > 0) {
      enum { kMax = 300 };
      float lua_values[kMax];
      float lua_max = 0.0f;
      for (size_t i = 0; i < lua_count; ++i) {
        float v = (*lua_memory_samples_)[i];
        lua_values[i] = v;
        if (v > lua_max) lua_max = v;
      }
      ImGui::PlotLines("##lua_mem_sparkline", lua_values,
                        static_cast<int>(lua_count), /*values_offset=*/0,
                        /*overlay_text=*/nullptr, /*scale_min=*/0.0f,
                        /*scale_max=*/lua_max * 1.5f, ImVec2(0, 40));
    }
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("String Table")) {
    auto st_stats = StringTable::Instance().stats();
    ImGui::Text("Interned strings: %d / %d", st_stats.strings_used,
                st_stats.total_strings);
    ImGui::Text("Buffer used: %d / %d bytes", st_stats.space_used,
                st_stats.total_space);
    float st_ratio = static_cast<float>(st_stats.space_used) /
                     static_cast<float>(st_stats.total_space);
    ImGui::ProgressBar(st_ratio, ImVec2(-1, 0));
  }
  ImGui::End();
}

void DebugUI::DrawRendererPanel(const FrameContext& ctx) {
  const auto& fs = ctx.frame_stats;
  ImGui::SetNextWindowPos(ImVec2(10, 440), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 450), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Renderer", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  if (ImGui::CollapsingHeader("Batch Stats",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Draw calls: %d", fs.draw_calls);
    ImGui::Text("Vertices:   %d", fs.vertices);
    ImGui::Text("Commands:   %d", fs.commands);
    ImGui::Separator();
    ImGui::Text("Flush Reasons:");
    if (ImGui::BeginTable("FlushReasons", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Reason");
      ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableHeadersRow();
      struct FlushEntry {
        const char* name;
        int count;
      };
      FlushEntry entries[] = {
          {"Texture", fs.flush_texture},
          {"Transform", fs.flush_transform},
          {"Shader", fs.flush_shader},
          {"Blend Mode", fs.flush_blend},
          {"Canvas", fs.flush_canvas},
          {"Line End", fs.flush_line_end},
          {"Overflow", fs.flush_overflow},
          {"Other", fs.flush_other},
      };
      for (const auto& e : entries) {
        if (e.count == 0) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (e.count == fs.flush_overflow && fs.flush_overflow > 0) {
          ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", e.name);
        } else {
          ImGui::Text("%s", e.name);
        }
        ImGui::TableNextColumn();
        ImGui::Text("%d", e.count);
      }
      ImGui::EndTable();
    }
    ImGui::Separator();
    int total_redundant = fs.redundant_texture + fs.redundant_transform +
                          fs.redundant_shader + fs.redundant_blend +
                          fs.redundant_line_width + fs.redundant_sdf_outline;
    ImGui::Text("Redundant Skips: %d", total_redundant);
    if (total_redundant > 0 && ImGui::TreeNode("##redundant_detail")) {
      ImGui::Text("Texture:     %d", fs.redundant_texture);
      ImGui::Text("Transform:   %d", fs.redundant_transform);
      ImGui::Text("Shader:      %d", fs.redundant_shader);
      ImGui::Text("Blend:       %d", fs.redundant_blend);
      ImGui::Text("Line Width:  %d", fs.redundant_line_width);
      ImGui::Text("SDF Outline: %d", fs.redundant_sdf_outline);
      ImGui::TreePop();
    }
    ImGui::Separator();
    DrawMemoryBar("Command Buffer", ctx.cmd_buf_used, ctx.cmd_buf_capacity);
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Textures")) {
    ImGui::Text("Texture units: %zu",
                engine_->batch_renderer.GetTextureCount());
  }

  if (ImGui::CollapsingHeader("Loaded Images")) {
    Slice<DbAssets::Image> images = engine_->renderer.GetImages();
    ImGui::Text("Count: %zu", images.size());
    if (images.size() > 0 &&
        ImGui::BeginTable("Images", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 150))) {
      ImGui::TableSetupColumn("Name");
      ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableHeadersRow();
      for (size_t i = 0; i < images.size(); ++i) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(images[i].name.data(),
                               images[i].name.data() + images[i].name.size());
        ImGui::TableNextColumn();
        ImGui::Text("%zu", images[i].width);
        ImGui::TableNextColumn();
        ImGui::Text("%zu", images[i].height);
      }
      ImGui::EndTable();
    }
  }

  if (ImGui::CollapsingHeader("Shader Programs")) {
    engine_->shaders.programs().ForEach(
        [](std::string_view name, const GLuint& handle) {
          ImGui::Text("%-30.*s  (GL %u)", static_cast<int>(name.size()),
                      name.data(), handle);
        });
  }

  if (ImGui::CollapsingHeader("Current State")) {
    ImGui::Text("Blend mode: %s",
                BlendModeName(engine_->batch_renderer.GetCurrentBlendMode()));
    std::string_view shader_name =
        StringByHandle(engine_->batch_renderer.GetCurrentShader());
    ImGui::Text("Shader: %.*s", static_cast<int>(shader_name.size()),
                shader_name.data());
    IVec2 vp = engine_->batch_renderer.viewport();
    ImGui::Text("Viewport: %dx%d", vp.x, vp.y);
  }
  ImGui::End();
}

void DebugUI::DrawCameraPanel() {
  Camera* camera = &engine_->camera;
  ImGui::SetNextWindowPos(ImVec2(440, 440), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(340, 350), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Camera", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  FVec2 pos = camera->GetPosition();
  ImGui::Text("Position: (%.1f, %.1f)", static_cast<double>(pos.x),
              static_cast<double>(pos.y));
  float zoom = camera->GetZoom();
  ImGui::Text("Zoom: %.3f", static_cast<double>(zoom));
  float rotation = camera->GetRotation();
  ImGui::Text("Rotation: %.3f rad (%.1f deg)",
              static_cast<double>(rotation),
              static_cast<double>(rotation * 180.0f /
                                  static_cast<float>(M_PI)));
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Follow", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (camera->IsFollowing()) {
      FVec2 target = camera->GetFollowTarget();
      ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Following");
      ImGui::Text("Target: (%.1f, %.1f)", static_cast<double>(target.x),
                  static_cast<double>(target.y));
      FVec2 lerp = camera->GetLerp();
      ImGui::Text("Lerp: (%.3f, %.3f)", static_cast<double>(lerp.x),
                  static_cast<double>(lerp.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Not following");
    }
  }

  if (ImGui::CollapsingHeader("Deadzone", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (camera->HasDeadzone()) {
      FVec2 dz = camera->GetDeadzone();
      ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Active");
      ImGui::Text("Half-size: (%.3f, %.3f)", static_cast<double>(dz.x),
                  static_cast<double>(dz.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Disabled");
    }
  }

  if (ImGui::CollapsingHeader("Bounds")) {
    if (camera->HasBounds()) {
      FVec2 start = camera->GetBoundsStart();
      FVec2 size = camera->GetBoundsSize();
      ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Active");
      ImGui::Text("Origin: (%.1f, %.1f)", static_cast<double>(start.x),
                  static_cast<double>(start.y));
      ImGui::Text("Size:   (%.1f, %.1f)", static_cast<double>(size.x),
                  static_cast<double>(size.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No bounds set");
    }
  }

  if (ImGui::CollapsingHeader("Shake")) {
    float intensity = camera->GetShakeIntensity();
    float timer = camera->GetShakeTimer();
    FVec2 offset = camera->GetShakeOffset();
    if (timer > 0.0f) {
      ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Shaking");
      ImGui::Text("Intensity: %.2f", static_cast<double>(intensity));
      ImGui::Text("Remaining: %.2f s", static_cast<double>(timer));
      ImGui::Text("Offset:    (%.1f, %.1f)", static_cast<double>(offset.x),
                  static_cast<double>(offset.y));
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No active shake");
    }
  }

  ImGui::Separator();

  // Drag-to-pan camera override.
  if (camera_override_) {
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Override active");
    if (ImGui::Button("Release")) {
      camera->SetPosition(camera_saved_x_, camera_saved_y_);
      camera_override_ = false;
    }
    ImGui::SameLine();
    ImGui::Text("(drag game viewport to pan)");
    // Check for mouse drag on the game viewport (not over any ImGui window).
    if (!ImGui::GetIO().WantCaptureMouse && ImGui::IsMouseDragging(0)) {
      ImVec2 delta = ImGui::GetMouseDragDelta(0);
      ImGui::ResetMouseDragDelta(0);
      FVec2 p = camera->GetPosition();
      camera->SetPosition(p.x - delta.x, p.y - delta.y);
    }
  } else {
    if (ImGui::Button("Pan Override")) {
      FVec2 p = camera->GetPosition();
      camera_saved_x_ = p.x;
      camera_saved_y_ = p.y;
      camera_override_ = true;
    }
  }

  ImGui::End();
}

void DebugUI::DrawPhysicsPanel() {
  Physics* physics = &engine_->physics;
  ImGui::SetNextWindowPos(ImVec2(820, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Physics", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  ImGui::Text("Bodies: %d  Joints: %d  Contacts: %d",
              physics->GetBodyCount(), physics->GetJointCount(),
              physics->GetContactCount());
  ImGui::Text("Pixels/meter: %.1f",
              static_cast<double>(physics->GetPixelsPerMeter()));
  ImGui::Separator();

  if (ImGui::CollapsingHeader("World Settings",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    FVec2 gravity = physics->GetWorldGravity();
    bool changed = false;
    ImGui::SetNextItemWidth(150);
    if (ImGui::DragFloat("Gravity X", &gravity.x, 1.0f)) changed = true;
    ImGui::SetNextItemWidth(150);
    if (ImGui::DragFloat("Gravity Y", &gravity.y, 1.0f)) changed = true;
    if (changed) physics->SetWorldGravity(gravity);

    int vel_iter = physics->GetVelocityIterations();
    int pos_iter = physics->GetPositionIterations();
    bool iter_changed = false;
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Velocity Iterations", &vel_iter, 1)) {
      if (vel_iter < 1) vel_iter = 1;
      iter_changed = true;
    }
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Position Iterations", &pos_iter, 1)) {
      if (pos_iter < 1) pos_iter = 1;
      iter_changed = true;
    }
    if (iter_changed) physics->SetIterations(vel_iter, pos_iter);
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Debug Draw",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Checkbox("Enable", &physics_debug_draw_)) {
      if (physics_debug_draw_) {
        physics->EnableDebugDraw(physics_debug_flags_);
      } else {
        physics->DisableDebugDraw();
      }
    }
    if (physics_debug_draw_) {
      bool shapes = (physics_debug_flags_ & b2Draw::e_shapeBit) != 0;
      bool joints = (physics_debug_flags_ & b2Draw::e_jointBit) != 0;
      bool aabbs = (physics_debug_flags_ & b2Draw::e_aabbBit) != 0;
      bool pairs = (physics_debug_flags_ & b2Draw::e_pairBit) != 0;
      bool com = (physics_debug_flags_ & b2Draw::e_centerOfMassBit) != 0;
      bool flags_changed = false;
      if (ImGui::Checkbox("Shapes", &shapes)) flags_changed = true;
      ImGui::SameLine();
      if (ImGui::Checkbox("Joints", &joints)) flags_changed = true;
      ImGui::SameLine();
      if (ImGui::Checkbox("AABBs", &aabbs)) flags_changed = true;
      if (ImGui::Checkbox("Pairs", &pairs)) flags_changed = true;
      ImGui::SameLine();
      if (ImGui::Checkbox("Center of Mass", &com)) flags_changed = true;
      if (flags_changed) {
        physics_debug_flags_ = 0;
        if (shapes) physics_debug_flags_ |= b2Draw::e_shapeBit;
        if (joints) physics_debug_flags_ |= b2Draw::e_jointBit;
        if (aabbs) physics_debug_flags_ |= b2Draw::e_aabbBit;
        if (pairs) physics_debug_flags_ |= b2Draw::e_pairBit;
        if (com) physics_debug_flags_ |= b2Draw::e_centerOfMassBit;
        physics->EnableDebugDraw(physics_debug_flags_);
      }
      ImGui::Checkbox("Velocities", &show_velocities_);
    }
  }
  ImGui::Separator();

  // Selected body details.
  if (selected_body_ != nullptr) {
    if (ImGui::CollapsingHeader("Selected Body",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      float ppm = physics->GetPixelsPerMeter();
      b2Vec2 pos = selected_body_->GetPosition();
      b2Vec2 vel = selected_body_->GetLinearVelocity();
      const char* type_str = "?";
      if (selected_body_->GetType() == b2_dynamicBody) type_str = "Dynamic";
      if (selected_body_->GetType() == b2_staticBody) type_str = "Static";
      if (selected_body_->GetType() == b2_kinematicBody) type_str = "Kinematic";
      ImGui::Text("Type: %s", type_str);
      ImGui::Text("Position: (%.1f, %.1f)",
                  static_cast<double>(pos.x * ppm),
                  static_cast<double>(pos.y * ppm));
      ImGui::Text("Velocity: (%.1f, %.1f)  Speed: %.1f",
                  static_cast<double>(vel.x * ppm),
                  static_cast<double>(vel.y * ppm),
                  static_cast<double>(vel.Length() * ppm));
      ImGui::Text("Angle: %.2f rad  Mass: %.2f",
                  static_cast<double>(selected_body_->GetAngle()),
                  static_cast<double>(selected_body_->GetMass()));
      ImGui::Text("Awake: %s  Fixed Rotation: %s",
                  selected_body_->IsAwake() ? "Yes" : "No",
                  selected_body_->IsFixedRotation() ? "Yes" : "No");
      int fixture_count = 0;
      for (const b2Fixture* f = selected_body_->GetFixtureList();
           f != nullptr; f = f->GetNext()) {
        ++fixture_count;
      }
      ImGui::Text("Fixtures: %d", fixture_count);
      if (ImGui::SmallButton("Deselect")) selected_body_ = nullptr;
    }
    ImGui::Separator();
  }

  if (ImGui::CollapsingHeader("Bodies")) {
    if (ImGui::BeginTable("BodyTable", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0, 250))) {
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("Position");
      ImGui::TableSetupColumn("Velocity");
      ImGui::TableSetupColumn("Angle", ImGuiTableColumnFlags_WidthFixed, 50);
      ImGui::TableSetupColumn("Mass", ImGuiTableColumnFlags_WidthFixed, 55);
      ImGui::TableHeadersRow();
      float ppm = physics->GetPixelsPerMeter();
      for (const b2Body* body = physics->GetBodyList(); body != nullptr;
           body = body->GetNext()) {
        bool is_selected = (body == selected_body_);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const char* type_str = "?";
        switch (body->GetType()) {
          case b2_dynamicBody:
            type_str = "Dynamic";
            break;
          case b2_staticBody:
            type_str = "Static";
            break;
          case b2_kinematicBody:
            type_str = "Kinematic";
            break;
        }
        ImGui::PushID(body);
        if (ImGui::Selectable(type_str, is_selected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
          selected_body_ = is_selected ? nullptr
                                       : const_cast<b2Body*>(body);
        }
        ImGui::PopID();
        ImGui::TableNextColumn();
        b2Vec2 pos = body->GetPosition();
        ImGui::Text("%.0f, %.0f", static_cast<double>(pos.x * ppm),
                    static_cast<double>(pos.y * ppm));
        ImGui::TableNextColumn();
        b2Vec2 vel = body->GetLinearVelocity();
        ImGui::Text("%.1f, %.1f", static_cast<double>(vel.x * ppm),
                    static_cast<double>(vel.y * ppm));
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", static_cast<double>(body->GetAngle()));
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", static_cast<double>(body->GetMass()));
      }
      ImGui::EndTable();
    }
  }
  ImGui::End();
}

void DebugUI::DrawNetworkPanel() {
  Network* net = &engine_->network;
  ImGui::SetNextWindowPos(ImVec2(620, 400), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Network", nullptr,
                    ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }

  if (!net->IsActive()) {
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "No network host active");
    ImGui::End();
    return;
  }

  ENetHost* host = net->host();
  if (host == nullptr) {
    ImGui::End();
    return;
  }

  // Connection summary.
  size_t connected = net->PeerCount();
  ImGui::Text("Connected Peers: %zu / %zu", connected, host->peerCount);
  ImGui::Text("Channels: %zu", host->channelLimit);
  ImGui::Separator();

  // Bandwidth stats.
  ImGui::Text("Total Sent: %.1f KB",
              host->totalSentData / 1024.0);
  ImGui::Text("Total Received: %.1f KB",
              host->totalReceivedData / 1024.0);
  ImGui::Text("Packets Sent: %u", host->totalSentPackets);
  ImGui::Text("Packets Received: %u", host->totalReceivedPackets);
  ImGui::Separator();

  // Per-peer table.
  if (connected > 0 &&
      ImGui::BeginTable("Peers", 6,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("RTT (ms)");
    ImGui::TableSetupColumn("Loss %");
    ImGui::TableSetupColumn("Sent KB");
    ImGui::TableSetupColumn("Recv KB");
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < host->peerCount; ++i) {
      ENetPeer* peer = &host->peers[i];
      if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%zu", i);
      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1), "Connected");
      ImGui::TableNextColumn();
      ImGui::Text("%u", peer->roundTripTime);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", peer->packetLoss / 65536.0 * 100.0);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f",
                  static_cast<double>(peer->outgoingDataTotal) / 1024.0);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f",
                  static_cast<double>(peer->incomingDataTotal) / 1024.0);
    }
    ImGui::EndTable();
  }

  // Events this frame.
  size_t events = net->event_count();
  if (events > 0) {
    ImGui::Separator();
    ImGui::Text("Events this frame: %zu", events);
  }

  ImGui::End();
}
