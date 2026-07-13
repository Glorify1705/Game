#include "physics_debug_draw.h"

#include <imgui.h>

namespace G {

ImVec2 PhysicsDebugDraw::ToScreen(const b2Vec2& v) const {
  // Convert Box2D meters to world pixels.
  FVec2 world_px(v.x * ppm_, v.y * ppm_);
  // Apply camera transform if the camera is actively used (has non-default
  // position or is following a target). Games that don't use camera.attach()
  // draw in raw screen coordinates and don't need the transform.
  if (camera_ != nullptr &&
      (camera_->IsFollowing() || camera_->GetPosition() != FVec2::Zero())) {
    FVec2 screen = camera_->ToScreen(world_px, viewport_);
    return ImVec2(screen.x, screen.y);
  }
  return ImVec2(world_px.x, world_px.y);
}

ImU32 PhysicsDebugDraw::ToImCol(const b2Color& c) const {
  return IM_COL32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255),
                  static_cast<int>(c.b * 255), static_cast<int>(c.a * 255));
}

void PhysicsDebugDraw::DrawPolygon(const b2Vec2* vertices, int32 vertex_count,
                                   const b2Color& color) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  ImU32 col = ToImCol(color);
  for (int32 i = 0; i < vertex_count; ++i) {
    int32 next = (i + 1) % vertex_count;
    dl->AddLine(ToScreen(vertices[i]), ToScreen(vertices[next]), col, 1.0f);
  }
}

void PhysicsDebugDraw::DrawSolidPolygon(const b2Vec2* vertices,
                                        int32 vertex_count,
                                        const b2Color& color) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  ImU32 col = ToImCol(color);
  for (int32 i = 0; i < vertex_count; ++i) {
    int32 next = (i + 1) % vertex_count;
    dl->AddLine(ToScreen(vertices[i]), ToScreen(vertices[next]), col, 2.0f);
  }
}

void PhysicsDebugDraw::DrawCircle(const b2Vec2& center, float radius,
                                  const b2Color& color) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  dl->AddCircle(ToScreen(center), radius * ppm_, ToImCol(color),
                /*num_segments=*/24, 1.0f);
}

void PhysicsDebugDraw::DrawSolidCircle(const b2Vec2& center, float radius,
                                       const b2Vec2& axis,
                                       const b2Color& color) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  ImVec2 screen_center = ToScreen(center);
  float px_radius = radius * ppm_;
  ImU32 col = ToImCol(color);
  dl->AddCircle(screen_center, px_radius, col, /*num_segments=*/24, 2.0f);
  // Draw axis line from center to edge.
  b2Vec2 edge_world = center + radius * axis;
  dl->AddLine(screen_center, ToScreen(edge_world), col, 2.0f);
}

void PhysicsDebugDraw::DrawSegment(const b2Vec2& p1, const b2Vec2& p2,
                                   const b2Color& color) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  dl->AddLine(ToScreen(p1), ToScreen(p2), ToImCol(color), 1.0f);
}

void PhysicsDebugDraw::DrawTransform(const b2Transform& xf) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  constexpr float kAxisScale = 0.4f;
  b2Vec2 p = xf.p;
  b2Vec2 px = p + kAxisScale * b2Vec2(xf.q.c, xf.q.s);
  b2Vec2 py = p + kAxisScale * b2Vec2(-xf.q.s, xf.q.c);
  ImVec2 sp = ToScreen(p);
  dl->AddLine(sp, ToScreen(px), IM_COL32(255, 0, 0, 255), 2.0f);
  dl->AddLine(sp, ToScreen(py), IM_COL32(0, 255, 0, 255), 2.0f);
}

void PhysicsDebugDraw::DrawPoint(const b2Vec2& p, float size,
                                 const b2Color& color) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  dl->AddCircleFilled(ToScreen(p), size, ToImCol(color));
}

}  // namespace G
