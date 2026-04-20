#pragma once
#ifndef _GAME_INPUT_H
#define _GAME_INPUT_H

#include <SDL3/SDL.h>

#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>

#include "array.h"
#include "circular_buffer.h"
#include "dictionary.h"
#include "vec.h"

namespace G {

class Keyboard {
 public:
  struct PressConditions {
    SDL_Scancode code;
    SDL_Keymod mods;

    PressConditions(SDL_Scancode c) : code(c), mods(SDL_KMOD_NONE) {}

    PressConditions() : PressConditions(SDL_SCANCODE_UNKNOWN) {}

    template <SDL_Keymod... Mods>
    PressConditions(SDL_Scancode c, std::initializer_list<SDL_Keymod> ms)
        : code(c), mods(SDL_KMOD_NONE) {
      for (SDL_Keymod mod : ms) mods = static_cast<SDL_Keymod>(mods | mod);
    }
  };

  Keyboard(Allocator* allocator);

  bool IsDown(const PressConditions& p) const {
    const bool result = pressed_[p.code] && ((mods_ & p.mods) == p.mods);
    return result;
  }

  bool IsReleased(const PressConditions& p) const {
    if (previous_pressed_[p.code] && !pressed_[p.code]) {
      return !p.mods || (previous_mods_ & p.mods);
    }
    return false;
  }

  bool IsPressed(const PressConditions& p) const {
    if (!previous_pressed_[p.code] && pressed_[p.code]) {
      return !p.mods || (mods_ & p.mods);
    }
    return false;
  }

  PressConditions MapKey(std::string_view key) const {
    PressConditions result;
    table_.Lookup(key, &result);
    return result;
  }

  void InitForFrame();

  void PushEvent(const SDL_Event& event);

  enum EventType { kDown, kUp };

  // Enables synthetic-input mode. InitForFrame stops querying SDL state and
  // instead carries `pressed_` over into `previous_pressed_`, so injected
  // keypresses persist across frames.
  void SetTestMode(bool enabled) { test_mode_ = enabled; }

  // Synthetically presses or releases a key. Only valid in test mode.
  void InjectKeyDown(SDL_Scancode code) { pressed_[code] = true; }
  void InjectKeyUp(SDL_Scancode code) { pressed_[code] = false; }

 private:
  inline static constexpr size_t kQueueSize = 256;
  inline static constexpr size_t kKeyboardTable = SDL_SCANCODE_COUNT;

  struct Event {
    SDL_Scancode code;
    EventType type;
  };

  std::bitset<kKeyboardTable + 1> pressed_;
  std::bitset<kKeyboardTable + 1> previous_pressed_;
  SDL_Keymod previous_mods_;
  SDL_Keymod mods_;
  Dictionary<PressConditions> table_;
  FixedArray<Event> events_;
  bool test_mode_ = false;
};

class Mouse {
 public:
  Mouse() {
    std::fill(pressed_.begin(), pressed_.end(), false);
    std::fill(previous_pressed_.begin(), previous_pressed_.end(), false);
  }
  enum Button { kLeft = 0, kRight = 1, kMiddle = 2 };
  FVec2 GetPosition() const {
    if (test_mode_) return test_position_;
    float x, y;
    SDL_GetMouseState(&x, &y);
    FVec2 pos(x, y);
    // Map from window coordinates to viewport coordinates, accounting for
    // aspect-correct letterboxing (black bars on sides or top/bottom).
    if (window_size_.x > 0 && viewport_size_.x > 0 &&
        window_size_ != viewport_size_) {
      float scale_x = window_size_.x / viewport_size_.x;
      float scale_y = window_size_.y / viewport_size_.y;
      float scale = scale_x < scale_y ? scale_x : scale_y;
      FVec2 draw_size = viewport_size_ * scale;
      FVec2 offset = (window_size_ - draw_size) * 0.5f;
      pos = (pos - offset) / scale;
    }
    return pos;
  }

  // Updates the window and viewport sizes for mouse coordinate mapping.
  void SetWindowAndViewport(FVec2 window, FVec2 viewport) {
    window_size_ = window;
    viewport_size_ = viewport;
  }

  void InitForFrame() {
    previous_pressed_ = pressed_;
    mouse_wheel_ = FVec2::Zero();
  }

  bool IsDown(int button) {
    return previous_pressed_[button] && pressed_[button];
  }

  bool IsReleased(int button) {
    return previous_pressed_[button] && !pressed_[button];
  }

  bool IsPressed(int button) {
    return !previous_pressed_[button] && pressed_[button];
  }

  void PushEvent(const SDL_Event& event);

  FVec2 GetWheel() const { return mouse_wheel_; }

  // Enables synthetic-input mode. GetPosition() then returns the test
  // position instead of querying SDL.
  void SetTestMode(bool enabled) { test_mode_ = enabled; }

  // Injects a synthetic mouse button press or release.
  void InjectButtonDown(int button) {
    if (button >= 0 && button < 3) pressed_[button] = true;
  }
  void InjectButtonUp(int button) {
    if (button >= 0 && button < 3) pressed_[button] = false;
  }

  // Adds to the synthetic wheel delta for this frame.
  void InjectWheel(float dx, float dy) { mouse_wheel_ += FVec(dx, dy); }

  // Sets the synthetic mouse position. Only used when test_mode_ is true.
  void SetTestPosition(float x, float y) { test_position_ = FVec(x, y); }

 private:
  FVec2 mouse_wheel_ = FVec2::Zero();
  std::array<bool, 3> previous_pressed_, pressed_;
  FVec2 test_position_ = FVec2::Zero();
  FVec2 window_size_ = FVec2::Zero();
  FVec2 viewport_size_ = FVec2::Zero();
  bool test_mode_ = false;
};

class Controllers {
 public:
  Controllers(Allocator* allocator);
  ~Controllers();

  // Loads controller mappings and opens connected joysticks.
  // If |db| is non-empty, uses the provided database; otherwise falls back to
  // the built-in default.
  void Initialize(ByteSlice db = {});

  void PushEvent(const SDL_Event& event);

  void InitForFrame();

  int joysticks() const { return controllers_.size(); }

  bool IsDown(int button, int controller_id) const {
    if (controller_id == -1) return false;
    auto& controller = controllers_[controller_id];
    return controller.previously_pressed[button] && controller.pressed[button];
  }

  bool IsReleased(int button, int controller_id) const {
    if (controller_id == -1) return false;
    auto& controller = controllers_[controller_id];
    return controller.previously_pressed[button] && !controller.pressed[button];
  }

  bool IsPressed(int button, int controller_id) const {
    if (controller_id == -1) return false;
    auto& controller = controllers_[controller_id];
    return !controller.previously_pressed[button] && controller.pressed[button];
  }

  SDL_GamepadButton StrToButton(std::string_view key) const {
    SDL_GamepadButton result;
    if (!button_table_.Lookup(key, &result)) {
      return SDL_GAMEPAD_BUTTON_INVALID;
    }
    return result;
  }

  int AxisPositions(SDL_GamepadAxis axis, int controller_id) const {
    if (controller_id == -1) return 0;
    if (test_mode_) {
      if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) return 0;
      return test_axes_[axis];
    }
    return SDL_GetGamepadAxis(controllers_[controller_id].ptr, axis);
  }

  int TriggerPositions(SDL_GamepadAxis axis, int controller_id) const {
    if (controller_id == -1) return 0;
    if (test_mode_) {
      if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) return 0;
      return test_axes_[axis];
    }
    return SDL_GetGamepadAxis(controllers_[controller_id].ptr, axis);
  }

  SDL_GamepadAxis StrToAxisOrTrigger(std::string_view key) const {
    SDL_GamepadAxis result;
    if (!axis_table_.Lookup(key, &result)) {
      return SDL_GAMEPAD_AXIS_INVALID;
    }
    return result;
  }

  int active_controller() const { return active_controller_; }

  // Enables synthetic-input mode. Reserves controller slot 0 for the test
  // controller, and AxisPositions / Inject* operate on it instead of real
  // hardware. InitForFrame carries pressed → previously_pressed for slot 0
  // (without resetting), so injected button presses persist until released.
  void SetTestMode(bool enabled) {
    test_mode_ = enabled;
    if (enabled) active_controller_ = 0;
  }

  // Synthetically presses or releases a button on the test controller.
  void InjectButtonDown(SDL_GamepadButton button) {
    if (button == SDL_GAMEPAD_BUTTON_INVALID ||
        static_cast<int>(button) >= 32) {
      return;
    }
    controllers_[0].pressed[button] = true;
  }
  void InjectButtonUp(SDL_GamepadButton button) {
    if (button == SDL_GAMEPAD_BUTTON_INVALID ||
        static_cast<int>(button) >= 32) {
      return;
    }
    controllers_[0].pressed[button] = false;
  }

  // Sets a synthetic axis or trigger position on the test controller.
  void InjectAxis(SDL_GamepadAxis axis, int value) {
    if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) return;
    test_axes_[axis] = value;
  }

 private:
  struct Controller {
    SDL_Gamepad* ptr = nullptr;
    std::bitset<32> pressed;
    std::bitset<32> previously_pressed;
  };
  std::array<Controller, 64> controllers_;
  std::bitset<64> open_controllers_;
  int active_controller_ = -1;
  Dictionary<SDL_GamepadButton> button_table_;
  Dictionary<SDL_GamepadAxis> axis_table_;
  std::array<int, SDL_GAMEPAD_AXIS_COUNT> test_axes_ = {};
  bool test_mode_ = false;
};

}  // namespace G

#endif  // _GAME_INPUT_H
