#pragma once
#ifndef _GAME_PHYSICS_DEBUG_DRAW_H
#define _GAME_PHYSICS_DEBUG_DRAW_H

#include "box2d/box2d.h"
#include "color.h"
#include "renderer.h"

namespace G {

// Implements Box2D's b2Draw interface using the engine's Renderer.
// All Box2D coordinates (meters) are scaled by pixels_per_meter to
// convert to pixel space for rendering.
class PhysicsDebugDraw final : public b2Draw {
 public:
  PhysicsDebugDraw(Renderer* renderer, float pixels_per_meter)
      : renderer_(renderer), ppm_(pixels_per_meter) {}

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
  // Converts a Box2D position (meters) to pixel coordinates.
  FVec2 ToPixels(const b2Vec2& v) const {
    return FVec(v.x * ppm_, v.y * ppm_);
  }

  // Converts a b2Color (0-1 floats) to engine Color (0-255 uint8).
  Color ToColor(const b2Color& c) const {
    return Color{static_cast<uint8_t>(c.r * 255),
                 static_cast<uint8_t>(c.g * 255),
                 static_cast<uint8_t>(c.b * 255),
                 static_cast<uint8_t>(c.a * 255)};
  }

  Renderer* renderer_;
  float ppm_;
};

}  // namespace G

#endif  // _GAME_PHYSICS_DEBUG_DRAW_H
