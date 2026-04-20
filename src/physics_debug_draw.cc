#include "physics_debug_draw.h"

namespace G {

void PhysicsDebugDraw::DrawPolygon(const b2Vec2* vertices, int32 vertexCount,
                                   const b2Color& color) {
  renderer_->SetColor(ToColor(color));
  renderer_->SetLineWidth(1.0f);
  for (int32 i = 0; i < vertexCount; ++i) {
    int32 next = (i + 1) % vertexCount;
    renderer_->DrawLine(ToPixels(vertices[i]), ToPixels(vertices[next]));
  }
}

void PhysicsDebugDraw::DrawSolidPolygon(const b2Vec2* vertices,
                                        int32 vertexCount,
                                        const b2Color& color) {
  // Draw as wire-frame with slightly brighter color.
  renderer_->SetColor(ToColor(color));
  renderer_->SetLineWidth(2.0f);
  for (int32 i = 0; i < vertexCount; ++i) {
    int32 next = (i + 1) % vertexCount;
    renderer_->DrawLine(ToPixels(vertices[i]), ToPixels(vertices[next]));
  }
}

void PhysicsDebugDraw::DrawCircle(const b2Vec2& center, float radius,
                                  const b2Color& color) {
  renderer_->SetColor(ToColor(color));
  renderer_->SetLineWidth(1.0f);
  renderer_->DrawCircleOutline(ToPixels(center), radius * ppm_);
}

void PhysicsDebugDraw::DrawSolidCircle(const b2Vec2& center, float radius,
                                       const b2Vec2& axis,
                                       const b2Color& color) {
  renderer_->SetColor(ToColor(color));
  renderer_->SetLineWidth(2.0f);
  FVec2 px_center = ToPixels(center);
  float px_radius = radius * ppm_;
  renderer_->DrawCircleOutline(px_center, px_radius);
  // Draw axis line from center to edge.
  FVec2 edge = px_center + FVec(axis.x, axis.y) * px_radius;
  renderer_->DrawLine(px_center, edge);
}

void PhysicsDebugDraw::DrawSegment(const b2Vec2& p1, const b2Vec2& p2,
                                   const b2Color& color) {
  renderer_->SetColor(ToColor(color));
  renderer_->SetLineWidth(1.0f);
  renderer_->DrawLine(ToPixels(p1), ToPixels(p2));
}

void PhysicsDebugDraw::DrawTransform(const b2Transform& xf) {
  constexpr float kAxisScale = 0.4f;
  b2Vec2 p = xf.p;
  b2Vec2 px = p + kAxisScale * b2Vec2(xf.q.c, xf.q.s);
  b2Vec2 py = p + kAxisScale * b2Vec2(-xf.q.s, xf.q.c);
  // X axis in red.
  renderer_->SetColor(Color{255, 0, 0, 255});
  renderer_->SetLineWidth(2.0f);
  renderer_->DrawLine(ToPixels(p), ToPixels(px));
  // Y axis in green.
  renderer_->SetColor(Color{0, 255, 0, 255});
  renderer_->DrawLine(ToPixels(p), ToPixels(py));
}

void PhysicsDebugDraw::DrawPoint(const b2Vec2& p, float size,
                                 const b2Color& color) {
  renderer_->SetColor(ToColor(color));
  renderer_->DrawCircle(ToPixels(p), size);
}

}  // namespace G
