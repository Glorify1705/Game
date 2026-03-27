#pragma once
#ifndef _GAME_TIMER_H
#define _GAME_TIMER_H

#include <cstdint>
#include <string_view>

#include "easing.h"

struct lua_State;

namespace G {

enum TimerType : uint8_t {
  kAfter,
  kEvery,
  kDuring,
  kTween,
  kCooldown,
};

struct EveryData {
  int32_t remaining;
};

struct TweenData {
  int target_ref;
  int keys_ref;
  EasingType easing;
};

struct CooldownData {
  int32_t remaining;
  bool ready;
};

struct Timer {
  TimerType type;
  bool active;
  bool ignore_time_scale;

  float elapsed;
  float duration;
  float speed;

  uint32_t tag_hash;

  int action_ref;
  int after_ref;
  int condition_ref;

  union {
    EveryData every;
    TweenData tween;
    CooldownData cooldown;
  };
};

class TimerSystem {
 public:
  TimerSystem();

  void SetLuaState(lua_State* state) { state_ = state; }

  uint32_t After(float delay, int action_ref);
  uint32_t After(float delay, int action_ref, uint32_t tag);

  uint32_t Every(float delay, int action_ref, int32_t times);
  uint32_t Every(float delay, int action_ref, int32_t times, uint32_t tag);

  uint32_t During(float duration, int action_ref, int after_ref);
  uint32_t During(float duration, int action_ref, int after_ref, uint32_t tag);

  uint32_t Tween(float duration, int target_ref, int keys_ref,
                 EasingType easing, int after_ref);
  uint32_t Tween(float duration, int target_ref, int keys_ref,
                 EasingType easing, int after_ref, uint32_t tag);

  uint32_t Cooldown(float delay, int condition_ref, int action_ref,
                    int32_t times);
  uint32_t Cooldown(float delay, int condition_ref, int action_ref,
                    int32_t times, uint32_t tag);

  void Cancel(uint32_t tag);
  void CancelAll();
  bool Exists(uint32_t tag) const;

  void SetRealTime(uint32_t tag, bool real_time);

  void Update(float scaled_dt, float raw_dt);

  void Clear();

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
