#pragma once
#ifndef _GAME_PARTICLES_H
#define _GAME_PARTICLES_H

#include <cmath>
#include <cstdint>

#include "allocators.h"
#include "color.h"

namespace G {

// Forward declaration (defined in renderer.h).
enum BlendMode : uint8_t;

// Maximum number of stops in a property ramp.
inline constexpr uint8_t kMaxRampStops = 8;

// How a particle property varies over its normalized lifetime [0, 1].
struct PropertyRamp {
  enum Mode : uint8_t {
    kConstant,     // Single fixed value.
    kRandomRange,  // Random value between min and max, picked at spawn.
    kRamp,         // Linear interpolation through evenly-spaced stops.
  };

  Mode mode = kConstant;
  uint8_t num_stops = 1;
  float stops[kMaxRampStops] = {0};
  float min = 0, max = 0;

  // Creates a constant ramp.
  static PropertyRamp Constant(float value) {
    PropertyRamp r;
    r.mode = kConstant;
    r.num_stops = 1;
    r.stops[0] = value;
    return r;
  }

  // Creates a random range ramp.
  static PropertyRamp Range(float lo, float hi) {
    PropertyRamp r;
    r.mode = kRandomRange;
    r.min = lo;
    r.max = hi;
    return r;
  }

  // Creates a ramp with N stops.
  static PropertyRamp Stops(const float* values, uint8_t count) {
    PropertyRamp r;
    r.mode = kRamp;
    r.num_stops = count < kMaxRampStops ? count : kMaxRampStops;
    for (uint8_t i = 0; i < r.num_stops; ++i) r.stops[i] = values[i];
    return r;
  }
};

// Color ramp: interpolates RGBA color over particle lifetime.
struct ColorRamp {
  Color stops[kMaxRampStops] = {};
  uint8_t num_stops = 1;

  static ColorRamp Constant(Color c) {
    ColorRamp r;
    r.num_stops = 1;
    r.stops[0] = c;
    return r;
  }
};

// Emission shape for particle spawning.
enum class EmissionShape : uint8_t {
  kPoint,   // All particles spawn at emitter position.
  kCircle,  // Random position within a circle.
  kRect,    // Random position within a rectangle.
};

// Declarative configuration for a particle emitter.
struct EmitterDef {
  // Spawning rate and pool capacity.
  float emission_rate = 0;
  uint32_t max_particles = 1000;

  // Initial particle state.
  PropertyRamp initial_speed = PropertyRamp::Constant(100);
  PropertyRamp initial_size = PropertyRamp::Constant(4);
  PropertyRamp initial_spin = PropertyRamp::Constant(0);
  float lifetime_min = 1.0f;
  float lifetime_max = 2.0f;
  float direction = 0;
  float spread = 3.14159265f;

  // Emission shape.
  EmissionShape shape = EmissionShape::kPoint;
  float shape_width = 0;
  float shape_height = 0;

  // Over-lifetime modulation.
  PropertyRamp size_over_life = PropertyRamp::Constant(1.0f);
  PropertyRamp speed_over_life = PropertyRamp::Constant(1.0f);
  PropertyRamp spin_over_life = PropertyRamp::Constant(1.0f);
  ColorRamp color_over_life = ColorRamp::Constant(Color::White());

  // Forces.
  float gravity_x = 0;
  float gravity_y = 0;
  float damping = 1.0f;

  // Rendering.
  BlendMode blend_mode = static_cast<BlendMode>(1);  // BLEND_ADD
};

// SoA particle storage. All arrays are parallel, sized to max_particles.
struct ParticlePool {
  float* x = nullptr;
  float* y = nullptr;
  float* vx = nullptr;
  float* vy = nullptr;
  float* age = nullptr;
  float* lifetime = nullptr;
  float* size = nullptr;
  float* initial_size = nullptr;
  float* angle = nullptr;
  float* spin = nullptr;
  float* initial_spin = nullptr;
  Color* color = nullptr;

  uint32_t count = 0;
  uint32_t max_particles = 0;

  // Allocates all SoA arrays from the given allocator.
  void Init(uint32_t capacity, Allocator* allocator);

  // Frees all SoA arrays.
  void Destroy(Allocator* allocator);
};

// Live emitter that owns a particle pool.
struct Emitter {
  EmitterDef def;
  ParticlePool pool;

  float emit_accumulator = 0;
  float x = 0, y = 0;
  bool active = false;

  Allocator* allocator = nullptr;

  // RNG state for this emitter.
  uint64_t rng_state = 0;
  uint64_t rng_inc = 0;

  // Creates an emitter with the given definition.
  void Init(const EmitterDef& definition, Allocator* alloc);

  // Frees the particle pool.
  void Destroy();

  // Advances all particles by dt seconds. Spawns new particles if active.
  void Update(float dt);

  // Spawns count particles immediately.
  void Burst(uint32_t count);

  // Spawns count particles at the given position.
  void BurstAt(uint32_t count, float bx, float by);

  // Returns the number of live particles.
  uint32_t ParticleCount() const { return pool.count; }

  // Returns true if the emitter is actively spawning particles.
  bool IsActive() const { return active; }

  // Seeds the RNG from the given value.
  void Seed(uint64_t seed);

  // Returns a random float in [lo, hi).
  float RandomFloat(float lo, float hi);

  // Returns a random uint32.
  uint32_t RandomUint32();
};

// Evaluates a property ramp at normalized lifetime t in [0, 1].
float EvalRamp(const PropertyRamp& ramp, float t);

// Evaluates a color ramp at normalized lifetime t in [0, 1].
Color EvalColorRamp(const ColorRamp& ramp, float t);

// Picks the initial value from a property ramp at spawn time.
float EvalSpawnRamp(const PropertyRamp& ramp, Emitter* emitter);

// Per-particle data for GPU instanced rendering.
struct ParticleInstanceData {
  float x, y;   // World-space center position.
  float size;   // Half-extent of the quad.
  float angle;  // Rotation in radians.
  Color color;  // RGBA color (4 bytes).
};

static_assert(sizeof(ParticleInstanceData) == 20);

}  // namespace G

#endif  // _GAME_PARTICLES_H
