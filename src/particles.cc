#include "particles.h"

#include <cmath>
#include <cstring>

#include "logging.h"

namespace G {
namespace {

// Linearly interpolates between a and b.
float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// Interpolates a single uint8 channel.
uint8_t LerpChannel(uint8_t a, uint8_t b, float t) {
  return static_cast<uint8_t>(a + (b - a) * t);
}

// Number of float arrays in the SoA ParticlePool layout.
// Must match the number of float* fields assigned in ParticlePool::Init().
constexpr size_t kFloatArrayCount = 11;

// Computes the total byte size of all SoA arrays for a given capacity.
size_t PoolByteSize(uint32_t capacity) {
  return capacity * kFloatArrayCount * sizeof(float) + capacity * sizeof(Color);
}

// Bump allocator over a contiguous float buffer. Used by ParticlePool::Init()
// to carve the single allocation into per-field SoA arrays.
struct FloatSlice {
  float* pos;
  uint32_t capacity;

  // Returns the current position and advances by capacity floats.
  float* Next() {
    float* result = pos;
    pos += capacity;
    return result;
  }
};

}  // namespace

void ParticlePool::Init(uint32_t cap, Allocator* allocator) {
  max_particles = cap;
  count = 0;
  // Single allocation for all SoA arrays. The layout is kFloatArrayCount
  // float arrays of `cap` elements each, followed by one Color array.
  auto* mem = static_cast<uint8_t*>(
      allocator->Alloc(PoolByteSize(cap), alignof(float)));
  FloatSlice s = {reinterpret_cast<float*>(mem), cap};
  x = s.Next();
  y = s.Next();
  vx = s.Next();
  vy = s.Next();
  age = s.Next();
  lifetime = s.Next();
  size = s.Next();
  initial_size = s.Next();
  angle = s.Next();
  spin = s.Next();
  initial_spin = s.Next();
  color = reinterpret_cast<Color*>(s.pos);
}

void ParticlePool::SwapLast(uint32_t dst, uint32_t src) {
  x[dst] = x[src];
  y[dst] = y[src];
  vx[dst] = vx[src];
  vy[dst] = vy[src];
  age[dst] = age[src];
  lifetime[dst] = lifetime[src];
  size[dst] = size[src];
  initial_size[dst] = initial_size[src];
  angle[dst] = angle[src];
  spin[dst] = spin[src];
  initial_spin[dst] = initial_spin[src];
  color[dst] = color[src];
}

void ParticlePool::Destroy(Allocator* allocator) {
  if (x == nullptr) return;
  allocator->Dealloc(x, PoolByteSize(max_particles));
  x = nullptr;
  count = 0;
}

float EvalRamp(const PropertyRamp& ramp, float t) {
  if (ramp.num_stops <= 1) return ramp.stops[0];
  if (t <= 0.0f) return ramp.stops[0];
  if (t >= 1.0f) return ramp.stops[ramp.num_stops - 1];
  float index = t * (ramp.num_stops - 1);
  int low = static_cast<int>(index);
  int high = low + 1;
  if (high >= ramp.num_stops) high = ramp.num_stops - 1;
  float frac = index - low;
  return Lerp(ramp.stops[low], ramp.stops[high], frac);
}

Color EvalColorRamp(const ColorRamp& ramp, float t) {
  if (ramp.num_stops <= 1) return ramp.stops[0];
  if (t <= 0.0f) return ramp.stops[0];
  if (t >= 1.0f) return ramp.stops[ramp.num_stops - 1];
  float index = t * (ramp.num_stops - 1);
  int low = static_cast<int>(index);
  int high = low + 1;
  if (high >= ramp.num_stops) high = ramp.num_stops - 1;
  float frac = index - low;
  Color a = ramp.stops[low];
  Color b = ramp.stops[high];
  return Color{LerpChannel(a.r, b.r, frac), LerpChannel(a.g, b.g, frac),
               LerpChannel(a.b, b.b, frac), LerpChannel(a.a, b.a, frac)};
}

float EvalSpawnRamp(const PropertyRamp& ramp, Emitter* emitter) {
  switch (ramp.mode) {
    case PropertyRamp::kConstant:
      return ramp.stops[0];
    case PropertyRamp::kRandomRange:
      return emitter->RandomFloat(ramp.min, ramp.max);
    case PropertyRamp::kRamp:
      return ramp.stops[0];
  }
  return ramp.stops[0];
}

void Emitter::Init(const EmitterDef& definition, Allocator* alloc) {
  def = definition;
  allocator = alloc;
  pool.Init(def.max_particles, alloc);
  emit_accumulator = 0;
  x = 0;
  y = 0;
  active = false;
  rng.seed(reinterpret_cast<uintptr_t>(this) ^ 0x853c49e6748fea9bULL);
}

void Emitter::Destroy() {
  if (allocator == nullptr) return;
  pool.Destroy(allocator);
  allocator = nullptr;
}

float Emitter::RandomFloat(float lo, float hi) {
  float t = static_cast<float>(rng()) / 4294967296.0f;
  return lo + (hi - lo) * t;
}

namespace {

void SpawnOne(Emitter* e, float spawn_x, float spawn_y) {
  ParticlePool& p = e->pool;
  if (p.count >= p.max_particles) return;

  uint32_t i = p.count++;
  const EmitterDef& d = e->def;

  // Position from emission shape.
  float ox = 0, oy = 0;
  switch (d.shape) {
    case EmissionShape::kPoint:
      break;
    case EmissionShape::kCircle: {
      float a = e->RandomFloat(0, 2.0f * 3.14159265f);
      float r = d.shape_width * std::sqrt(e->RandomFloat(0, 1));
      ox = std::cos(a) * r;
      oy = std::sin(a) * r;
    } break;
    case EmissionShape::kRect:
      ox = e->RandomFloat(-d.shape_width, d.shape_width);
      oy = e->RandomFloat(-d.shape_height, d.shape_height);
      break;
  }
  p.x[i] = spawn_x + ox;
  p.y[i] = spawn_y + oy;

  // Velocity from direction + spread + speed.
  float angle = d.direction + e->RandomFloat(-d.spread, d.spread);
  float speed = EvalSpawnRamp(d.initial_speed, e);
  p.vx[i] = std::cos(angle) * speed;
  p.vy[i] = std::sin(angle) * speed;

  // Lifetime and age.
  p.lifetime[i] = e->RandomFloat(d.lifetime_min, d.lifetime_max);
  p.age[i] = 0;

  // Size and spin.
  float sz = EvalSpawnRamp(d.initial_size, e);
  p.size[i] = sz;
  p.initial_size[i] = sz;
  float sp = EvalSpawnRamp(d.initial_spin, e);
  p.spin[i] = sp;
  p.initial_spin[i] = sp;
  p.angle[i] = 0;

  // Start at the first color in the ramp.
  p.color[i] = d.color_over_life.stops[0];
}

}  // namespace

void Emitter::Update(float dt) {
  ParticlePool& p = pool;
  const EmitterDef& d = def;

  // Pass 1: Age and kill.
  for (uint32_t i = 0; i < p.count;) {
    p.age[i] += dt;
    if (p.age[i] >= p.lifetime[i]) {
      uint32_t last = p.count - 1;
      if (i != last) p.SwapLast(i, last);
      p.count--;
      // Don't advance i; re-check the swapped particle.
    } else {
      ++i;
    }
  }

  // Pass 2: Apply forces and integrate.
  float frame_damping = std::pow(d.damping, dt);
  for (uint32_t i = 0; i < p.count; ++i) {
    p.vx[i] += d.gravity_x * dt;
    p.vy[i] += d.gravity_y * dt;
    p.vx[i] *= frame_damping;
    p.vy[i] *= frame_damping;
    p.x[i] += p.vx[i] * dt;
    p.y[i] += p.vy[i] * dt;
    p.angle[i] += p.spin[i] * dt;
  }

  // Pass 3: Evaluate over-lifetime ramps.
  for (uint32_t i = 0; i < p.count; ++i) {
    float t = p.age[i] / p.lifetime[i];
    float size_mult = EvalRamp(d.size_over_life, t);
    p.size[i] = p.initial_size[i] * size_mult;
    float spin_mult = EvalRamp(d.spin_over_life, t);
    p.spin[i] = p.initial_spin[i] * spin_mult;
    p.color[i] = EvalColorRamp(d.color_over_life, t);
  }

  // Spawn new particles from emission rate.
  if (active && d.emission_rate > 0) {
    emit_accumulator += d.emission_rate * dt;
    while (emit_accumulator >= 1.0f && p.count < p.max_particles) {
      SpawnOne(this, x, y);
      emit_accumulator -= 1.0f;
    }
    // Cap accumulator to avoid burst after a pause.
    if (emit_accumulator > d.emission_rate) {
      emit_accumulator = d.emission_rate;
    }
  }
}

void Emitter::Burst(uint32_t count) { BurstAt(count, x, y); }

void Emitter::BurstAt(uint32_t count, float bx, float by) {
  for (uint32_t i = 0; i < count && pool.count < pool.max_particles; ++i) {
    SpawnOne(this, bx, by);
  }
}

}  // namespace G
