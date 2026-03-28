#pragma once
#ifndef _GAME_EASING_H
#define _GAME_EASING_H

#include <cmath>
#include <cstdint>

namespace G {

// Easing functions for interpolation. Each family (quad, cubic, etc.) comes
// in three variants: in (accelerate), out (decelerate), in-out (both).
enum EasingType : uint8_t {
  kLinear,        // Constant speed, no acceleration.
  kInQuad,        // Quadratic (t^2) ease in.
  kOutQuad,       // Quadratic ease out.
  kInOutQuad,     // Quadratic ease in then out.
  kInCubic,       // Cubic (t^3) ease in.
  kOutCubic,      // Cubic ease out.
  kInOutCubic,    // Cubic ease in then out.
  kInQuart,       // Quartic (t^4) ease in.
  kOutQuart,      // Quartic ease out.
  kInOutQuart,    // Quartic ease in then out.
  kInQuint,       // Quintic (t^5) ease in.
  kOutQuint,      // Quintic ease out.
  kInOutQuint,    // Quintic ease in then out.
  kInSine,        // Sinusoidal ease in.
  kOutSine,       // Sinusoidal ease out.
  kInOutSine,     // Sinusoidal ease in then out.
  kInExpo,        // Exponential (2^t) ease in.
  kOutExpo,       // Exponential ease out.
  kInOutExpo,     // Exponential ease in then out.
  kInCirc,        // Circular (quarter-circle arc) ease in.
  kOutCirc,       // Circular ease out.
  kInOutCirc,     // Circular ease in then out.
  kInBack,        // Overshoots then returns, ease in.
  kOutBack,       // Overshoots then settles, ease out.
  kInOutBack,     // Overshoot on both ends.
  kInElastic,     // Spring-like oscillation, ease in.
  kOutElastic,    // Spring-like oscillation, ease out.
  kInOutElastic,  // Spring-like oscillation on both ends.
  kInBounce,      // Bouncing ball effect, ease in.
  kOutBounce,     // Bouncing ball effect, ease out.
  kInOutBounce,   // Bouncing ball effect on both ends.
  kEasingCount,
};

constexpr float kPi = 3.14159265358979323846f;
// Overshoot constant for back easing (used in EaseInBack). Controls how far
// the animation overshoots before settling. ~10% overshoot at the default.
constexpr float kBackOvershoot = 1.70158f;

inline float EaseInQuad(float t) { return t * t; }
inline float EaseInCubic(float t) { return t * t * t; }
inline float EaseInQuart(float t) { return t * t * t * t; }
inline float EaseInQuint(float t) { return t * t * t * t * t; }
inline float EaseInSine(float t) { return 1.0f - std::cos(t * kPi * 0.5f); }

inline float EaseInExpo(float t) {
  return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
}

inline float EaseInCirc(float t) { return 1.0f - std::sqrt(1.0f - t * t); }

inline float EaseInBack(float t) {
  return t * t * ((kBackOvershoot + 1.0f) * t - kBackOvershoot);
}

inline float EaseInElastic(float t) {
  if (t == 0.0f || t == 1.0f) return t;
  return -std::pow(2.0f, 10.0f * (t - 1.0f)) *
         std::sin((t - 1.0f - 0.075f) * 2.0f * kPi / 0.3f);
}

inline float EaseOutBounce(float t) {
  if (t < 1.0f / 2.75f) {
    return 7.5625f * t * t;
  } else if (t < 2.0f / 2.75f) {
    t -= 1.5f / 2.75f;
    return 7.5625f * t * t + 0.75f;
  } else if (t < 2.5f / 2.75f) {
    t -= 2.25f / 2.75f;
    return 7.5625f * t * t + 0.9375f;
  }
  t -= 2.625f / 2.75f;
  return 7.5625f * t * t + 0.984375f;
}

inline float EaseIn(EasingType base, float t) {
  switch (base) {
    case kInQuad:
    case kOutQuad:
    case kInOutQuad:
      return EaseInQuad(t);
    case kInCubic:
    case kOutCubic:
    case kInOutCubic:
      return EaseInCubic(t);
    case kInQuart:
    case kOutQuart:
    case kInOutQuart:
      return EaseInQuart(t);
    case kInQuint:
    case kOutQuint:
    case kInOutQuint:
      return EaseInQuint(t);
    case kInSine:
    case kOutSine:
    case kInOutSine:
      return EaseInSine(t);
    case kInExpo:
    case kOutExpo:
    case kInOutExpo:
      return EaseInExpo(t);
    case kInCirc:
    case kOutCirc:
    case kInOutCirc:
      return EaseInCirc(t);
    case kInBack:
    case kOutBack:
    case kInOutBack:
      return EaseInBack(t);
    case kInElastic:
    case kOutElastic:
    case kInOutElastic:
      return EaseInElastic(t);
    case kInBounce:
    case kOutBounce:
    case kInOutBounce:
      return 1.0f - EaseOutBounce(1.0f - t);
    default:
      return t;
  }
}

inline float Ease(EasingType type, float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  switch (type) {
    case kLinear:
      return t;
    case kInQuad:
    case kInCubic:
    case kInQuart:
    case kInQuint:
    case kInSine:
    case kInExpo:
    case kInCirc:
    case kInBack:
    case kInElastic:
    case kInBounce:
      return EaseIn(type, t);
    case kOutQuad:
    case kOutCubic:
    case kOutQuart:
    case kOutQuint:
    case kOutSine:
    case kOutExpo:
    case kOutCirc:
    case kOutBack:
    case kOutElastic:
    case kOutBounce:
      return 1.0f - EaseIn(type, 1.0f - t);
    case kInOutQuad:
    case kInOutCubic:
    case kInOutQuart:
    case kInOutQuint:
    case kInOutSine:
    case kInOutExpo:
    case kInOutCirc:
    case kInOutBack:
    case kInOutElastic:
    case kInOutBounce:
      if (t < 0.5f) return EaseIn(type, t * 2.0f) * 0.5f;
      return 0.5f + (1.0f - EaseIn(type, (1.0f - t) * 2.0f)) * 0.5f;
    default:
      return t;
  }
}

}  // namespace G

#endif  // _GAME_EASING_H
