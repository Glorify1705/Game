#pragma once
#ifndef _GAME_PHYSICS_DEBUG_DRAW_H
#define _GAME_PHYSICS_DEBUG_DRAW_H

#include <imgui.h>

#include "box2d/box2d.h"
#include "camera.h"
#include "vec.h"

namespace G {

// Implements Box2D's b2Draw interface using ImGui's overlay draw list.
// Renders on top of everything (including canvases and post-processing)
// because it draws during the ImGui pass, not the game renderer pass.
class PhysicsDebugDraw final : public b2Draw {
 public:
  PhysicsDebugDraw(float pixels_per_meter) : ppm_(pixels_per_meter) {}

  // Sets the camera and viewport for the current frame.
  void SetCamera(const Camera* camera, FVec2 viewport) {
    camera_ = camera;
    viewport_ = viewport;
  }

  void DrawPolygon(const b2Vec2* vertices, int32 vertexCount,
                   const b2Color& color) override;
  void DrawSolidPolygon(const b2Vec2* vertices, int32 vertexCount,
                        const b2Color& color) override;
  void DrawCircle(const b2Vec2& center, float radius,
                  const b2Color& color) override;
  void DrawSolidCircle(const b2Vec2& center, float radius,
                       const b2Vec2& axis, const b2Color& color) override;
  void DrawSegment(const b2Vec2& p1, const b2Vec2& p2,
                   const b2Color& color) override;
  void DrawTransform(const b2Transform& xf) override;
  void DrawPoint(const b2Vec2& p, float size, const b2Color& color) override;

 private:
  // Converts a Box2D position (meters) to screen pixels via camera.
  ImVec2 ToScreen(const b2Vec2& v) const;

  // Converts a b2Color to ImGui packed color.
  ImU32 ToImCol(const b2Color& c) const;

  float ppm_;
  const Camera* camera_ = nullptr;
  FVec2 viewport_;
};

}  // namespace G

#endif  // _GAME_PHYSICS_DEBUG_DRAW_H
