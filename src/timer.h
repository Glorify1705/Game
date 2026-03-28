#pragma once
#ifndef _GAME_TIMER_H
#define _GAME_TIMER_H

#include <cstdint>
#include <string_view>

#include "easing.h"
#include "lua.h"  // For lua_State and LUA_NOREF.

namespace G {

// Discriminant for the Timer union.
enum TimerType : uint8_t {
  kAfter,     // Fire once after a delay.
  kEvery,     // Fire repeatedly at a fixed interval.
  kDuring,    // Call every frame for a duration.
  kTween,     // Interpolate table fields over a duration.
  kCooldown,  // Fire when delay elapsed AND a condition is true.
};

// Per-type payload for repeating timers.
struct EveryData {
  int32_t remaining;  // Repetitions left (0 = infinite).
};

// Per-type payload for tween timers.
struct TweenData {
  int target_ref;  // Lua registry ref to the subject table.
  int keys_ref;    // Lua registry ref to the {key, start, end, ...} array.
  EasingType easing;
};

// Per-type payload for cooldown timers.
struct CooldownData {
  int32_t remaining;  // Repetitions left (0 = infinite).
  bool ready;         // Whether the cooldown period has elapsed.
};

// A single timer instance. Discriminated by `type`; the union holds
// type-specific data. Lua callback refs are stored as registry indices.
struct Timer {
  TimerType type;
  bool active;
  bool ignore_time_scale;

  float elapsed;   // Seconds elapsed since timer started.
  float duration;  // Total duration in seconds.
  float speed;

  uint32_t tag_hash;

  int action_ref;     // Lua registry ref to the main callback.
  int after_ref;      // Lua registry ref to the completion callback.
  int condition_ref;  // Lua registry ref to the condition function (cooldown).

  union {
    EveryData every;
    TweenData tween;
    CooldownData cooldown;
  };
};

// Manages a fixed-size pool of timers driven by Lua callbacks.
class TimerSystem {
 public:
  TimerSystem();

  // Sets the Lua state used for callback dispatch.
  void SetLuaState(lua_State* state) { state_ = state; }

  // Schedules a one-shot callback after a delay. Returns the tag hash.
  uint32_t After(float delay, int action_ref);
  uint32_t After(float delay, int action_ref, uint32_t tag);

  // Schedules a repeating callback at a fixed interval. Returns the tag hash.
  uint32_t Every(float delay, int action_ref, int32_t times);
  uint32_t Every(float delay, int action_ref, int32_t times, uint32_t tag);

  // Calls action every frame for the given duration. Returns the tag hash.
  uint32_t During(float duration, int action_ref, int after_ref);
  uint32_t During(float duration, int action_ref, int after_ref, uint32_t tag);

  // Interpolates Lua table fields toward target values. Returns the tag hash.
  uint32_t Tween(float duration, int target_ref, int keys_ref,
                 EasingType easing, int after_ref);
  uint32_t Tween(float duration, int target_ref, int keys_ref,
                 EasingType easing, int after_ref, uint32_t tag);

  // Fires when both the delay has elapsed and the condition returns true.
  uint32_t Cooldown(float delay, int condition_ref, int action_ref,
                    int32_t times);
  uint32_t Cooldown(float delay, int condition_ref, int action_ref,
                    int32_t times, uint32_t tag);

  // Cancels a timer by its tag hash.
  void Cancel(uint32_t tag);
  // Cancels all active timers.
  void CancelAll();
  // Returns true if a timer with the given tag hash is active.
  bool Exists(uint32_t tag) const;

  // Makes a timer ignore the game time scale (for UI elements during pause).
  void SetRealTime(uint32_t tag, bool real_time);

  // Advances all active timers. scaled_dt respects time scale, raw_dt does not.
  void Update(float scaled_dt, float raw_dt);

  // Cancels all timers and resets the auto-tag counter.
  void Clear();

  // Hashes a string tag name to a uint32_t for timer lookup.
  static uint32_t HashTag(std::string_view tag);

  size_t active_count() const { return count_; }

  static constexpr size_t kMaxTimers = 256;

 private:
  uint32_t NextAutoTag();
  size_t AllocSlot();
  void FreeSlot(size_t index);
  void FreeLuaRefs(Timer& timer);
  void CancelByHash(uint32_t hash);
  void CallLuaRef(int ref);
  bool CallLuaCondition(int ref);
  void UpdateTween(Timer& timer, float t);

  lua_State* state_ = nullptr;
  Timer timers_[kMaxTimers];
  size_t count_ = 0;

  uint32_t auto_tag_counter_ = 0;
};

EasingType EasingFromName(std::string_view name);

}  // namespace G

#endif  // _GAME_TIMER_H
