#include "gtest/gtest.h"
#include "particles.h"

namespace G {

class ParticleTest : public ::testing::Test {
 protected:
  SystemAllocator allocator_;

  Allocator* allocator() { return &allocator_; }
};

// PropertyRamp evaluation.

TEST_F(ParticleTest, ConstantRamp) {
  auto ramp = PropertyRamp::Constant(42.0f);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 0.0f), 42.0f);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 0.5f), 42.0f);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 1.0f), 42.0f);
}

TEST_F(ParticleTest, TwoStopRamp) {
  float stops[] = {0.0f, 100.0f};
  auto ramp = PropertyRamp::Stops(stops, 2);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 0.5f), 50.0f);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 1.0f), 100.0f);
}

TEST_F(ParticleTest, ThreeStopRamp) {
  float stops[] = {10.0f, 50.0f, 0.0f};
  auto ramp = PropertyRamp::Stops(stops, 3);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 0.0f), 10.0f);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 0.25f), 30.0f);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 0.5f), 50.0f);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 0.75f), 25.0f);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 1.0f), 0.0f);
}

TEST_F(ParticleTest, RampClampsBelowZero) {
  float stops[] = {10.0f, 100.0f};
  auto ramp = PropertyRamp::Stops(stops, 2);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, -1.0f), 10.0f);
}

TEST_F(ParticleTest, RampClampsAboveOne) {
  float stops[] = {10.0f, 100.0f};
  auto ramp = PropertyRamp::Stops(stops, 2);
  EXPECT_FLOAT_EQ(EvalRamp(ramp, 2.0f), 100.0f);
}

// ColorRamp evaluation.

TEST_F(ParticleTest, ConstantColorRamp) {
  auto ramp = ColorRamp::Constant(Color{255, 0, 0, 255});
  Color c = EvalColorRamp(ramp, 0.5f);
  EXPECT_EQ(c.r, 255);
  EXPECT_EQ(c.g, 0);
  EXPECT_EQ(c.b, 0);
  EXPECT_EQ(c.a, 255);
}

TEST_F(ParticleTest, TwoStopColorRamp) {
  ColorRamp ramp;
  ramp.num_stops = 2;
  ramp.stops[0] = Color{0, 0, 0, 255};
  ramp.stops[1] = Color{254, 254, 254, 0};
  Color mid = EvalColorRamp(ramp, 0.5f);
  EXPECT_EQ(mid.r, 127);
  EXPECT_EQ(mid.g, 127);
  EXPECT_EQ(mid.b, 127);
  EXPECT_EQ(mid.a, 127);
}

// ParticlePool lifecycle.

TEST_F(ParticleTest, PoolInitAndDestroy) {
  ParticlePool pool;
  pool.Init(100, allocator());
  EXPECT_EQ(pool.count, 0u);
  EXPECT_EQ(pool.max_particles, 100u);
  EXPECT_NE(pool.x, nullptr);
  pool.Destroy(allocator());
  EXPECT_EQ(pool.x, nullptr);
}

TEST_F(ParticleTest, DoubleDestroyIsSafe) {
  ParticlePool pool;
  pool.Init(10, allocator());
  pool.Destroy(allocator());
  pool.Destroy(allocator());  // Should not crash.
}

// Emitter spawn and kill.

TEST_F(ParticleTest, EmitterSpawnAndKill) {
  EmitterDef def;
  def.max_particles = 100;
  def.lifetime_min = 0.1f;
  def.lifetime_max = 0.1f;
  def.initial_speed = PropertyRamp::Constant(0);

  Emitter e;
  e.Init(def, allocator());
  EXPECT_EQ(e.ParticleCount(), 0u);

  e.Burst(10);
  EXPECT_EQ(e.ParticleCount(), 10u);

  // Advance past lifetime to kill all particles.
  e.Update(0.2f);
  EXPECT_EQ(e.ParticleCount(), 0u);

  e.Destroy();
}

TEST_F(ParticleTest, BurstCapsAtPoolCapacity) {
  EmitterDef def;
  def.max_particles = 5;
  def.lifetime_min = 10.0f;
  def.lifetime_max = 10.0f;

  Emitter e;
  e.Init(def, allocator());
  e.Burst(100);
  EXPECT_EQ(e.ParticleCount(), 5u);
  e.Destroy();
}

TEST_F(ParticleTest, BurstAtPosition) {
  EmitterDef def;
  def.max_particles = 10;
  def.lifetime_min = 10.0f;
  def.lifetime_max = 10.0f;
  def.initial_speed = PropertyRamp::Constant(0);
  def.spread = 0;

  Emitter e;
  e.Init(def, allocator());
  e.BurstAt(1, 100.0f, 200.0f);
  EXPECT_EQ(e.ParticleCount(), 1u);
  EXPECT_FLOAT_EQ(e.pool.x[0], 100.0f);
  EXPECT_FLOAT_EQ(e.pool.y[0], 200.0f);
  e.Destroy();
}

// Continuous emission.

TEST_F(ParticleTest, ContinuousEmission) {
  EmitterDef def;
  def.max_particles = 1000;
  def.emission_rate = 100;
  def.lifetime_min = 10.0f;
  def.lifetime_max = 10.0f;

  Emitter e;
  e.Init(def, allocator());
  e.active = true;

  // At 100 particles/sec, 0.1 sec should spawn ~10 particles.
  e.Update(0.1f);
  EXPECT_GE(e.ParticleCount(), 9u);
  EXPECT_LE(e.ParticleCount(), 11u);

  e.Destroy();
}

TEST_F(ParticleTest, EmissionStopsWhenInactive) {
  EmitterDef def;
  def.max_particles = 1000;
  def.emission_rate = 1000;
  def.lifetime_min = 10.0f;
  def.lifetime_max = 10.0f;

  Emitter e;
  e.Init(def, allocator());
  e.active = false;

  e.Update(1.0f);
  EXPECT_EQ(e.ParticleCount(), 0u);

  e.Destroy();
}

// Gravity and damping.

TEST_F(ParticleTest, GravityAffectsVelocity) {
  EmitterDef def;
  def.max_particles = 1;
  def.lifetime_min = 10.0f;
  def.lifetime_max = 10.0f;
  def.initial_speed = PropertyRamp::Constant(0);
  def.gravity_x = 0;
  def.gravity_y = 100.0f;
  def.damping = 1.0f;

  Emitter e;
  e.Init(def, allocator());
  e.Burst(1);

  float y0 = e.pool.y[0];
  e.Update(1.0f);
  // After 1 second of gravity, particle should have moved down.
  EXPECT_GT(e.pool.y[0], y0);
  EXPECT_NEAR(e.pool.vy[0], 100.0f, 1.0f);

  e.Destroy();
}

TEST_F(ParticleTest, DampingReducesVelocity) {
  EmitterDef def;
  def.max_particles = 1;
  def.lifetime_min = 10.0f;
  def.lifetime_max = 10.0f;
  def.initial_speed = PropertyRamp::Constant(100);
  def.direction = 0;
  def.spread = 0;
  def.damping = 0.5f;

  Emitter e;
  e.Init(def, allocator());
  e.Burst(1);

  float vx_initial = e.pool.vx[0];
  e.Update(1.0f);
  // After 1 second with damping=0.5, velocity should be roughly halved.
  EXPECT_LT(std::abs(e.pool.vx[0]), std::abs(vx_initial));

  e.Destroy();
}

// Over-lifetime ramp evaluation during update.

TEST_F(ParticleTest, SizeOverLifeApplied) {
  EmitterDef def;
  def.max_particles = 1;
  def.lifetime_min = 1.0f;
  def.lifetime_max = 1.0f;
  def.initial_size = PropertyRamp::Constant(10.0f);
  def.initial_speed = PropertyRamp::Constant(0);
  float size_stops[] = {1.0f, 0.0f};
  def.size_over_life = PropertyRamp::Stops(size_stops, 2);
  def.damping = 1.0f;

  Emitter e;
  e.Init(def, allocator());
  e.Burst(1);
  EXPECT_FLOAT_EQ(e.pool.initial_size[0], 10.0f);

  // At t=0.5 (halfway through life), size_over_life should be 0.5.
  // So effective size = 10.0 * 0.5 = 5.0.
  e.Update(0.5f);
  EXPECT_NEAR(e.pool.size[0], 5.0f, 0.5f);

  e.Destroy();
}

// Swap-and-compact correctness.

TEST_F(ParticleTest, SwapAndCompactPreservesData) {
  EmitterDef def;
  def.max_particles = 10;
  def.lifetime_min = 0.5f;
  def.lifetime_max = 0.5f;
  def.initial_speed = PropertyRamp::Constant(0);
  def.damping = 1.0f;

  Emitter e;
  e.Init(def, allocator());

  // Spawn 5 particles.
  e.Burst(5);
  EXPECT_EQ(e.ParticleCount(), 5u);

  // Override lifetime of first particle to die soon.
  e.pool.lifetime[0] = 0.01f;
  e.pool.age[0] = 0.0f;

  // Update past that particle's lifetime.
  e.Update(0.02f);
  EXPECT_EQ(e.ParticleCount(), 4u);

  // All remaining particles should still have age < lifetime.
  for (uint32_t i = 0; i < e.ParticleCount(); ++i) {
    EXPECT_LT(e.pool.age[i], e.pool.lifetime[i]);
  }

  e.Destroy();
}

// Edge cases.

TEST_F(ParticleTest, ZeroLifetimeParticlesDieImmediately) {
  EmitterDef def;
  def.max_particles = 10;
  def.lifetime_min = 0.0f;
  def.lifetime_max = 0.0f;

  Emitter e;
  e.Init(def, allocator());
  e.Burst(5);
  // Particles with 0 lifetime should die on first update.
  e.Update(0.001f);
  EXPECT_EQ(e.ParticleCount(), 0u);

  e.Destroy();
}

TEST_F(ParticleTest, EmitterWithZeroEmissionRate) {
  EmitterDef def;
  def.max_particles = 100;
  def.emission_rate = 0;
  def.lifetime_min = 1.0f;
  def.lifetime_max = 1.0f;

  Emitter e;
  e.Init(def, allocator());
  e.active = true;
  e.Update(1.0f);
  EXPECT_EQ(e.ParticleCount(), 0u);
  e.Destroy();
}

// SpawnRamp evaluation.

TEST_F(ParticleTest, SpawnRampConstant) {
  auto ramp = PropertyRamp::Constant(42.0f);
  Emitter e;
  EmitterDef def;
  def.max_particles = 1;
  e.Init(def, allocator());
  EXPECT_FLOAT_EQ(EvalSpawnRamp(ramp, &e), 42.0f);
  e.Destroy();
}

TEST_F(ParticleTest, SpawnRampRandomRange) {
  auto ramp = PropertyRamp::Range(10.0f, 20.0f);
  Emitter e;
  EmitterDef def;
  def.max_particles = 1;
  e.Init(def, allocator());

  // Spawn many values and check they're all in range.
  for (int i = 0; i < 100; ++i) {
    float v = EvalSpawnRamp(ramp, &e);
    EXPECT_GE(v, 10.0f);
    EXPECT_LT(v, 20.0f);
  }

  e.Destroy();
}

}  // namespace G
