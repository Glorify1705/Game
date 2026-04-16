#include <cmath>

#include "collision.h"
#include "collision_world.h"
#include "test_fixture.h"

namespace G {

// Narrow-phase collision tests (no allocator needed).

TEST(CollisionTest, CircleCircleOverlap) {
  auto r = TestCircleCircle(FVec(0, 0), 10, FVec(15, 0), 10);
  EXPECT_TRUE(r.hit);
  // Separation normal pushes A away from B (leftward).
  EXPECT_NEAR(r.normal.x, -1.0f, 1e-5f);
  EXPECT_NEAR(r.normal.y, 0.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 5.0f, 1e-5f);
}

TEST(CollisionTest, CircleCircleNoOverlap) {
  auto r = TestCircleCircle(FVec(0, 0), 10, FVec(25, 0), 10);
  EXPECT_FALSE(r.hit);
}

TEST(CollisionTest, CircleCircleTangent) {
  auto r = TestCircleCircle(FVec(0, 0), 10, FVec(20, 0), 10);
  // Tangent: sum of radii == distance, so overlap = 0. Our test uses >,
  // so tangent is not a collision.
  EXPECT_FALSE(r.hit);
}

TEST(CollisionTest, CircleCircleContained) {
  auto r = TestCircleCircle(FVec(0, 0), 20, FVec(5, 0), 5);
  EXPECT_TRUE(r.hit);
  EXPECT_NEAR(r.normal.x, -1.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 20.0f, 1e-5f);
}

TEST(CollisionTest, CircleCircleCoincident) {
  auto r = TestCircleCircle(FVec(5, 5), 10, FVec(5, 5), 10);
  EXPECT_TRUE(r.hit);
  EXPECT_NEAR(r.depth, 20.0f, 1e-5f);
}

TEST(CollisionTest, AABBAABBOverlap) {
  auto r = TestAABBAABB(FVec(0, 0), 10, 10, FVec(15, 0), 10, 10);
  EXPECT_TRUE(r.hit);
  // Separation normal pushes A away from B (leftward).
  EXPECT_NEAR(r.normal.x, -1.0f, 1e-5f);
  EXPECT_NEAR(r.normal.y, 0.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 5.0f, 1e-5f);
}

TEST(CollisionTest, AABBAABBNoOverlap) {
  auto r = TestAABBAABB(FVec(0, 0), 10, 10, FVec(25, 0), 10, 10);
  EXPECT_FALSE(r.hit);
}

TEST(CollisionTest, AABBAABBVerticalOverlap) {
  auto r = TestAABBAABB(FVec(0, 0), 10, 10, FVec(0, 12), 10, 10);
  EXPECT_TRUE(r.hit);
  // Separation normal pushes A away from B (upward).
  EXPECT_NEAR(r.normal.x, 0.0f, 1e-5f);
  EXPECT_NEAR(r.normal.y, -1.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 8.0f, 1e-5f);
}

TEST(CollisionTest, AABBAABBContained) {
  auto r = TestAABBAABB(FVec(0, 0), 20, 20, FVec(5, 5), 5, 5);
  EXPECT_TRUE(r.hit);
}

TEST(CollisionTest, CircleAABBOverlap) {
  auto r = TestCircleAABB(FVec(15, 0), 10, FVec(0, 0), 10, 10);
  EXPECT_TRUE(r.hit);
  EXPECT_NEAR(r.normal.x, 1.0f, 1e-5f);
  EXPECT_NEAR(r.normal.y, 0.0f, 1e-5f);
  EXPECT_NEAR(r.depth, 5.0f, 1e-5f);
}

TEST(CollisionTest, CircleAABBNoOverlap) {
  auto r = TestCircleAABB(FVec(25, 0), 5, FVec(0, 0), 10, 10);
  EXPECT_FALSE(r.hit);
}

TEST(CollisionTest, CircleAABBCenterInside) {
  auto r = TestCircleAABB(FVec(5, 0), 10, FVec(0, 0), 20, 20);
  EXPECT_TRUE(r.hit);
  // Center is inside AABB, should push toward nearest edge.
  EXPECT_GT(r.depth, 0);
}

TEST(CollisionTest, CircleAABBCorner) {
  // Circle overlapping the corner of the AABB.
  auto r = TestCircleAABB(FVec(13, 13), 5, FVec(0, 0), 10, 10);
  EXPECT_TRUE(r.hit);
  // Normal should push circle away from corner, roughly toward (1, 1).
  float len = std::sqrt(r.normal.x * r.normal.x + r.normal.y * r.normal.y);
  EXPECT_NEAR(len, 1.0f, 1e-4f);
  EXPECT_GT(r.normal.x, 0);
  EXPECT_GT(r.normal.y, 0);
}

TEST(CollisionTest, TestShapesDispatch) {
  CollisionShape circle = MakeCircle(10);
  CollisionShape aabb = MakeAABB(20, 20);

  // Circle vs Circle
  auto r1 = TestShapes(circle, FVec(0, 0), circle, FVec(15, 0));
  EXPECT_TRUE(r1.hit);

  // AABB vs AABB
  auto r2 = TestShapes(aabb, FVec(0, 0), aabb, FVec(15, 0));
  EXPECT_TRUE(r2.hit);

  // Circle vs AABB
  auto r3 = TestShapes(circle, FVec(15, 0), aabb, FVec(0, 0));
  EXPECT_TRUE(r3.hit);

  // AABB vs Circle (reversed)
  auto r4 = TestShapes(aabb, FVec(0, 0), circle, FVec(15, 0));
  EXPECT_TRUE(r4.hit);
  // Separation normal pushes A (AABB) away from B (circle), i.e. leftward.
  EXPECT_LT(r4.normal.x, 0);
}

TEST(CollisionTest, RaycastCircleHit) {
  auto result = RaycastCircle(FVec(0, 0), FVec(1, 0), 100.0f, FVec(50, 0), 10);
  EXPECT_TRUE(result.hit);
  EXPECT_NEAR(result.t, 40.0f, 1e-3f);
  EXPECT_NEAR(result.normal.x, -1.0f, 1e-3f);
}

TEST(CollisionTest, RaycastCircleMiss) {
  auto result = RaycastCircle(FVec(0, 0), FVec(1, 0), 100.0f, FVec(0, 50), 10);
  EXPECT_FALSE(result.hit);
}

TEST(CollisionTest, RaycastAABBHit) {
  auto result =
      RaycastAABB(FVec(0, 0), FVec(1, 0), 100.0f, FVec(50, 0), 10, 10);
  EXPECT_TRUE(result.hit);
  EXPECT_NEAR(result.t, 40.0f, 1e-3f);
  EXPECT_NEAR(result.normal.x, -1.0f, 1e-3f);
}

TEST(CollisionTest, RaycastAABBMiss) {
  auto result =
      RaycastAABB(FVec(0, 0), FVec(1, 0), 100.0f, FVec(0, 50), 10, 10);
  EXPECT_FALSE(result.hit);
}

TEST(CollisionTest, PointInCircle) {
  CollisionShape c = MakeCircle(10);
  EXPECT_TRUE(PointInShape(FVec(5, 5), c, FVec(0, 0)));
  EXPECT_FALSE(PointInShape(FVec(15, 0), c, FVec(0, 0)));
}

TEST(CollisionTest, PointInAABB) {
  CollisionShape a = MakeAABB(20, 20);
  EXPECT_TRUE(PointInShape(FVec(5, 5), a, FVec(0, 0)));
  EXPECT_FALSE(PointInShape(FVec(15, 0), a, FVec(0, 0)));
}

// CollisionWorld tests (need allocator).

class CollisionWorldTest : public BaseTest {};

TEST_F(CollisionWorldTest, AddRemove) {
  CollisionWorld world(64.0f, alloc);
  world.Update();

  CollisionShape circle = MakeCircle(10);
  auto h = world.Add(circle, FVec(100, 100), {}, false, 0);
  EXPECT_TRUE(world.IsValid(h));
  EXPECT_EQ(world.active_count(), 1u);

  world.Remove(h);
  EXPECT_FALSE(world.IsValid(h));
  EXPECT_EQ(world.active_count(), 0u);
}

TEST_F(CollisionWorldTest, HandleGenerations) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h1 = world.Add(circle, FVec(0, 0), {}, false, 0);
  world.Remove(h1);

  // Re-adding should reuse the same slot but with a new generation.
  auto h2 = world.Add(circle, FVec(0, 0), {}, false, 0);
  EXPECT_EQ(h1.index, h2.index);
  EXPECT_NE(h1.generation, h2.generation);

  // Old handle is invalid.
  EXPECT_FALSE(world.IsValid(h1));
  EXPECT_TRUE(world.IsValid(h2));
}

TEST_F(CollisionWorldTest, GetSetPosition) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape aabb = MakeAABB(20, 20);
  auto h = world.Add(aabb, FVec(10, 20), {}, false, 0);
  FVec2 pos = world.GetPosition(h);
  EXPECT_NEAR(pos.x, 10.0f, 1e-5f);
  EXPECT_NEAR(pos.y, 20.0f, 1e-5f);

  world.SetPosition(h, FVec(50, 60));
  pos = world.GetPosition(h);
  EXPECT_NEAR(pos.x, 50.0f, 1e-5f);
  EXPECT_NEAR(pos.y, 60.0f, 1e-5f);
}

TEST_F(CollisionWorldTest, OverlapQuery) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h1 = world.Add(circle, FVec(0, 0), {}, false, 0);
  auto h2 = world.Add(circle, FVec(15, 0), {}, false, 0);
  auto h3 = world.Add(circle, FVec(100, 100), {}, false, 0);
  world.Update();

  CollisionWorld::OverlapResult results[64];
  uint32_t count = world.GetOverlaps(h1, results, 64);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(results[0].handle, h2);

  // h3 is too far away.
  count = world.GetOverlaps(h3, results, 64);
  EXPECT_EQ(count, 0u);

  (void)h3;  // Suppress unused warning.
}

TEST_F(CollisionWorldTest, CollisionFiltering) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);

  // h1 is category 0x0001, detects 0x0002
  auto h1 = world.Add(circle, FVec(0, 0), {0x0001, 0x0002}, false, 0);
  // h2 is category 0x0002, detects 0x0001
  auto h2 = world.Add(circle, FVec(15, 0), {0x0002, 0x0001}, false, 0);
  // h3 is category 0x0004, detects 0x0004 (doesn't match h1)
  auto h3 = world.Add(circle, FVec(5, 0), {0x0004, 0x0004}, false, 0);
  world.Update();

  CollisionWorld::OverlapResult results[64];

  // h1 should see h2 (categories match masks).
  uint32_t count = world.GetOverlaps(h1, results, 64);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(results[0].handle, h2);

  (void)h3;
}

TEST_F(CollisionWorldTest, Raycast) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h = world.Add(circle, FVec(50, 0), {}, false, 0);
  world.Update();

  CollisionWorld::RaycastHit hit;
  bool found = world.Raycast(FVec(0, 0), FVec(1, 0), 100, 0xFFFF, &hit);
  EXPECT_TRUE(found);
  EXPECT_EQ(hit.handle, h);
  EXPECT_NEAR(hit.t, 40.0f, 1e-2f);
}

TEST_F(CollisionWorldTest, RaycastMiss) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  world.Add(circle, FVec(0, 50), {}, false, 0);
  world.Update();

  CollisionWorld::RaycastHit hit;
  bool found = world.Raycast(FVec(0, 0), FVec(1, 0), 100, 0xFFFF, &hit);
  EXPECT_FALSE(found);
}

TEST_F(CollisionWorldTest, QueryPoint) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h = world.Add(circle, FVec(0, 0), {}, false, 0);
  world.Update();

  ColliderHandle results[64];
  uint32_t count = world.QueryPoint(FVec(5, 5), 0xFFFF, results, 64);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(results[0], h);

  count = world.QueryPoint(FVec(50, 50), 0xFFFF, results, 64);
  EXPECT_EQ(count, 0u);
}

TEST_F(CollisionWorldTest, MoveAndSlide) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  CollisionShape wall = MakeAABB(200, 20);

  auto player = world.Add(circle, FVec(100, 80), {}, false, 0);
  world.Add(wall, FVec(100, 100), {}, false, 0);  // Wall below player.
  world.Update();

  // Move player downward into wall. Keep velocity small enough that the
  // circle center doesn't pass the wall center (discrete resolution limit).
  auto result = world.MoveAndSlide(player, FVec(0, 15));

  // Player should be pushed out of the wall.
  EXPECT_LT(result.position.y, 100.0f);
  EXPECT_GE(result.contact_count, 1u);
}

TEST_F(CollisionWorldTest, MoveAndCollide) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  CollisionShape wall = MakeAABB(200, 20);

  auto player = world.Add(circle, FVec(100, 80), {}, false, 0);
  world.Add(wall, FVec(100, 100), {}, false, 0);
  world.Update();

  auto result = world.MoveAndCollide(player, FVec(0, 30));
  EXPECT_GE(result.contact_count, 1u);
}

TEST_F(CollisionWorldTest, TriggerDetection) {
  CollisionWorld world(64.0f, alloc);

  CollisionShape circle = MakeCircle(10);
  auto h1 = world.Add(circle, FVec(0, 0), {}, true, 0);  // trigger
  auto h2 = world.Add(circle, FVec(100, 100), {}, false, 0);

  // First frame: no overlaps.
  world.Update();
  EXPECT_EQ(world.new_trigger_count(), 0u);

  // Move h2 into h1's range.
  world.SetPosition(h2, FVec(5, 0));
  world.Update();
  EXPECT_GE(world.new_trigger_count(), 1u);

  // Next frame: still overlapping, no new triggers.
  world.Update();
  EXPECT_EQ(world.new_trigger_count(), 0u);

  // Move h2 away.
  world.SetPosition(h2, FVec(100, 100));
  world.Update();
  EXPECT_GE(world.lost_trigger_count(), 1u);

  (void)h1;
}

}  // namespace G
