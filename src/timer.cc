#include "timer.h"

#include <array>
#include <cstring>

#include "easing.h"
#include "libraries/rapidhash.h"
#include "logging.h"
#include "lua.h"

namespace G {
namespace {

struct EasingEntry {
  std::string_view name;
  EasingType type;
};

constexpr std::array<EasingEntry, 41> kEasingNames = {{
    {"linear", kLinear},          {"in-quad", kInQuad},
    {"out-quad", kOutQuad},       {"in-out-quad", kInOutQuad},
    {"quad", kOutQuad},           {"in-cubic", kInCubic},
    {"out-cubic", kOutCubic},     {"in-out-cubic", kInOutCubic},
    {"cubic", kOutCubic},         {"in-quart", kInQuart},
    {"out-quart", kOutQuart},     {"in-out-quart", kInOutQuart},
    {"quart", kOutQuart},         {"in-quint", kInQuint},
    {"out-quint", kOutQuint},     {"in-out-quint", kInOutQuint},
    {"quint", kOutQuint},         {"in-sine", kInSine},
    {"out-sine", kOutSine},       {"in-out-sine", kInOutSine},
    {"sine", kOutSine},           {"in-expo", kInExpo},
    {"out-expo", kOutExpo},       {"in-out-expo", kInOutExpo},
    {"expo", kOutExpo},           {"in-circ", kInCirc},
    {"out-circ", kOutCirc},       {"in-out-circ", kInOutCirc},
    {"circ", kOutCirc},           {"in-back", kInBack},
    {"out-back", kOutBack},       {"in-out-back", kInOutBack},
    {"back", kOutBack},           {"in-elastic", kInElastic},
    {"out-elastic", kOutElastic}, {"in-out-elastic", kInOutElastic},
    {"elastic", kOutElastic},     {"in-bounce", kInBounce},
    {"out-bounce", kOutBounce},   {"in-out-bounce", kInOutBounce},
    {"bounce", kOutBounce},
}};

}  // namespace

TimerSystem::TimerSystem() {
  std::memset(timers_, 0, sizeof(timers_));
  for (size_t i = 0; i < kMaxTimers; ++i) {
    timers_[i].active = false;
    timers_[i].action_ref = LUA_NOREF;
    timers_[i].after_ref = LUA_NOREF;
    timers_[i].condition_ref = LUA_NOREF;
  }
}

uint32_t TimerSystem::HashTag(std::string_view tag) {
  return static_cast<uint32_t>(rapidhash(tag.data(), tag.size()));
}

uint32_t TimerSystem::NextAutoTag() { return ++auto_tag_counter_; }

size_t TimerSystem::AllocSlot() {
  for (size_t i = 0; i < kMaxTimers; ++i) {
    if (!timers_[i].active) {
      count_++;
      return i;
    }
  }
  LOG("Timer system: no free slots");
  return kMaxTimers;
}

void TimerSystem::FreeSlot(size_t index) {
  FreeLuaRefs(timers_[index]);
  timers_[index].active = false;
  count_--;
}

void TimerSystem::FreeLuaRefs(Timer& timer) {
  if (state_ == nullptr) return;
  if (timer.action_ref != LUA_NOREF) {
    luaL_unref(state_, LUA_REGISTRYINDEX, timer.action_ref);
    timer.action_ref = LUA_NOREF;
  }
  if (timer.after_ref != LUA_NOREF) {
    luaL_unref(state_, LUA_REGISTRYINDEX, timer.after_ref);
    timer.after_ref = LUA_NOREF;
  }
  if (timer.condition_ref != LUA_NOREF) {
    luaL_unref(state_, LUA_REGISTRYINDEX, timer.condition_ref);
    timer.condition_ref = LUA_NOREF;
  }
  if (timer.type == kTween) {
    if (timer.tween.target_ref != LUA_NOREF) {
      luaL_unref(state_, LUA_REGISTRYINDEX, timer.tween.target_ref);
      timer.tween.target_ref = LUA_NOREF;
    }
    if (timer.tween.keys_ref != LUA_NOREF) {
      luaL_unref(state_, LUA_REGISTRYINDEX, timer.tween.keys_ref);
      timer.tween.keys_ref = LUA_NOREF;
    }
  }
}

void TimerSystem::CancelByHash(uint32_t hash) {
  for (size_t i = 0; i < kMaxTimers; ++i) {
    if (timers_[i].active && timers_[i].tag_hash == hash) {
      FreeSlot(i);
      return;
    }
  }
}

void TimerSystem::CallLuaRef(int ref) {
  if (state_ == nullptr || ref == LUA_NOREF) return;
  lua_rawgeti(state_, LUA_REGISTRYINDEX, ref);
  if (lua_pcall(state_, 0, 0, 0) != 0) {
    LOG("Timer callback error: ", lua_tostring(state_, -1));
    lua_pop(state_, 1);
  }
}

bool TimerSystem::CallLuaCondition(int ref) {
  if (state_ == nullptr || ref == LUA_NOREF) return false;
  lua_rawgeti(state_, LUA_REGISTRYINDEX, ref);
  if (lua_pcall(state_, 0, 1, 0) != 0) {
    LOG("Timer condition error: ", lua_tostring(state_, -1));
    lua_pop(state_, 1);
    return false;
  }
  bool result = lua_toboolean(state_, -1);
  lua_pop(state_, 1);
  return result;
}

void TimerSystem::UpdateTween(Timer& timer, float t) {
  if (state_ == nullptr) return;
  float eased = Ease(timer.tween.easing, t);
  lua_rawgeti(state_, LUA_REGISTRYINDEX, timer.tween.target_ref);
  lua_rawgeti(state_, LUA_REGISTRYINDEX, timer.tween.keys_ref);
  size_t len = lua_objlen(state_, -1);
  for (size_t i = 1; i <= len; i += 3) {
    lua_rawgeti(state_, -1, i);
    const char* key = lua_tostring(state_, -1);
    lua_pop(state_, 1);
    lua_rawgeti(state_, -1, i + 1);
    float start = static_cast<float>(lua_tonumber(state_, -1));
    lua_pop(state_, 1);
    lua_rawgeti(state_, -1, i + 2);
    float end = static_cast<float>(lua_tonumber(state_, -1));
    lua_pop(state_, 1);
    float value = start + (end - start) * eased;
    lua_pushnumber(state_, value);
    lua_setfield(state_, -3, key);
  }
  lua_pop(state_, 2);
}

uint32_t TimerSystem::After(float delay, int action_ref) {
  return After(delay, action_ref, NextAutoTag());
}

uint32_t TimerSystem::After(float delay, int action_ref, uint32_t tag) {
  CancelByHash(tag);
  size_t slot = AllocSlot();
  if (slot >= kMaxTimers) return 0;
  Timer& t = timers_[slot];
  t.type = kAfter;
  t.active = true;
  t.ignore_time_scale = false;
  t.elapsed = 0;
  t.duration = delay;
  t.speed = 1.0f;
  t.tag_hash = tag;
  t.action_ref = action_ref;
  t.after_ref = LUA_NOREF;
  t.condition_ref = LUA_NOREF;
  return tag;
}

uint32_t TimerSystem::Every(float delay, int action_ref, int32_t times) {
  return Every(delay, action_ref, times, NextAutoTag());
}

uint32_t TimerSystem::Every(float delay, int action_ref, int32_t times,
                            uint32_t tag) {
  CancelByHash(tag);
  size_t slot = AllocSlot();
  if (slot >= kMaxTimers) return 0;
  Timer& t = timers_[slot];
  t.type = kEvery;
  t.active = true;
  t.ignore_time_scale = false;
  t.elapsed = 0;
  t.duration = delay;
  t.speed = 1.0f;
  t.tag_hash = tag;
  t.action_ref = action_ref;
  t.after_ref = LUA_NOREF;
  t.condition_ref = LUA_NOREF;
  t.every.remaining = times;
  return tag;
}

uint32_t TimerSystem::During(float duration, int action_ref, int after_ref) {
  return During(duration, action_ref, after_ref, NextAutoTag());
}

uint32_t TimerSystem::During(float duration, int action_ref, int after_ref,
                             uint32_t tag) {
  CancelByHash(tag);
  size_t slot = AllocSlot();
  if (slot >= kMaxTimers) return 0;
  Timer& t = timers_[slot];
  t.type = kDuring;
  t.active = true;
  t.ignore_time_scale = false;
  t.elapsed = 0;
  t.duration = duration;
  t.speed = 1.0f;
  t.tag_hash = tag;
  t.action_ref = action_ref;
  t.after_ref = after_ref;
  t.condition_ref = LUA_NOREF;
  return tag;
}

uint32_t TimerSystem::Tween(float duration, int target_ref, int keys_ref,
                            EasingType easing, int after_ref) {
  return Tween(duration, target_ref, keys_ref, easing, after_ref,
               NextAutoTag());
}

uint32_t TimerSystem::Tween(float duration, int target_ref, int keys_ref,
                            EasingType easing, int after_ref, uint32_t tag) {
  CancelByHash(tag);
  size_t slot = AllocSlot();
  if (slot >= kMaxTimers) return 0;
  Timer& t = timers_[slot];
  t.type = kTween;
  t.active = true;
  t.ignore_time_scale = false;
  t.elapsed = 0;
  t.duration = duration;
  t.speed = 1.0f;
  t.tag_hash = tag;
  t.action_ref = LUA_NOREF;
  t.after_ref = after_ref;
  t.condition_ref = LUA_NOREF;
  t.tween.target_ref = target_ref;
  t.tween.keys_ref = keys_ref;
  t.tween.easing = easing;
  return tag;
}

uint32_t TimerSystem::Cooldown(float delay, int condition_ref, int action_ref,
                               int32_t times) {
  return Cooldown(delay, condition_ref, action_ref, times, NextAutoTag());
}

uint32_t TimerSystem::Cooldown(float delay, int condition_ref, int action_ref,
                               int32_t times, uint32_t tag) {
  CancelByHash(tag);
  size_t slot = AllocSlot();
  if (slot >= kMaxTimers) return 0;
  Timer& t = timers_[slot];
  t.type = kCooldown;
  t.active = true;
  t.ignore_time_scale = false;
  t.elapsed = 0;
  t.duration = delay;
  t.speed = 1.0f;
  t.tag_hash = tag;
  t.action_ref = action_ref;
  t.after_ref = LUA_NOREF;
  t.condition_ref = condition_ref;
  t.cooldown.remaining = times;
  t.cooldown.ready = false;
  return tag;
}

void TimerSystem::Cancel(uint32_t tag) { CancelByHash(tag); }

void TimerSystem::CancelAll() {
  for (size_t i = 0; i < kMaxTimers; ++i) {
    if (timers_[i].active) {
      FreeSlot(i);
    }
  }
}

bool TimerSystem::Exists(uint32_t tag) const {
  for (size_t i = 0; i < kMaxTimers; ++i) {
    if (timers_[i].active && timers_[i].tag_hash == tag) return true;
  }
  return false;
}

void TimerSystem::SetRealTime(uint32_t tag, bool real_time) {
  for (size_t i = 0; i < kMaxTimers; ++i) {
    if (timers_[i].active && timers_[i].tag_hash == tag) {
      timers_[i].ignore_time_scale = real_time;
      return;
    }
  }
}

void TimerSystem::Update(float scaled_dt, float raw_dt) {
  for (size_t i = 0; i < kMaxTimers; ++i) {
    Timer& timer = timers_[i];
    if (!timer.active) continue;

    float dt = timer.ignore_time_scale ? raw_dt : scaled_dt;
    dt *= timer.speed;

    switch (timer.type) {
      case kAfter: {
        timer.elapsed += dt;
        if (timer.elapsed >= timer.duration) {
          CallLuaRef(timer.action_ref);
          FreeSlot(i);
        }
        break;
      }
      case kEvery: {
        timer.elapsed += dt;
        while (timer.active && timer.elapsed >= timer.duration) {
          timer.elapsed -= timer.duration;
          CallLuaRef(timer.action_ref);
          if (timer.every.remaining > 0) {
            timer.every.remaining--;
            if (timer.every.remaining == 0) {
              CallLuaRef(timer.after_ref);
              FreeSlot(i);
            }
          }
        }
        break;
      }
      case kDuring: {
        timer.elapsed += dt;
        float fraction = timer.elapsed / timer.duration;
        if (fraction > 1.0f) fraction = 1.0f;
        if (state_ != nullptr && timer.action_ref != LUA_NOREF) {
          lua_rawgeti(state_, LUA_REGISTRYINDEX, timer.action_ref);
          lua_pushnumber(state_, dt);
          lua_pushnumber(state_, timer.elapsed);
          lua_pushnumber(state_, fraction);
          if (lua_pcall(state_, 3, 0, 0) != 0) {
            LOG("Timer during callback error: ", lua_tostring(state_, -1));
            lua_pop(state_, 1);
          }
        }
        if (timer.elapsed >= timer.duration) {
          CallLuaRef(timer.after_ref);
          FreeSlot(i);
        }
        break;
      }
      case kTween: {
        timer.elapsed += dt;
        float t = timer.elapsed / timer.duration;
        if (t > 1.0f) t = 1.0f;
        UpdateTween(timer, t);
        if (timer.elapsed >= timer.duration) {
          UpdateTween(timer, 1.0f);
          CallLuaRef(timer.after_ref);
          FreeSlot(i);
        }
        break;
      }
      case kCooldown: {
        timer.elapsed += dt;
        if (timer.elapsed >= timer.duration) {
          if (CallLuaCondition(timer.condition_ref)) {
            CallLuaRef(timer.action_ref);
            timer.elapsed = 0;
            if (timer.cooldown.remaining > 0) {
              timer.cooldown.remaining--;
              if (timer.cooldown.remaining == 0) {
                CallLuaRef(timer.after_ref);
                FreeSlot(i);
              }
            }
          }
        }
        break;
      }
    }
  }
}

void TimerSystem::Clear() {
  for (size_t i = 0; i < kMaxTimers; ++i) {
    if (timers_[i].active) {
      FreeSlot(i);
    }
  }
  auto_tag_counter_ = 0;
}

EasingType EasingFromName(std::string_view name) {
  for (const auto& entry : kEasingNames) {
    if (name == entry.name) return entry.type;
  }
  return kLinear;
}

}  // namespace G
