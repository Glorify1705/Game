#pragma once
#ifndef _GAME_INPUT_H
#define _GAME_INPUT_H

#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>

#include "SDL.h"
#include "array.h"
#include "assets.h"
#include "circular_buffer.h"
#include "dictionary.h"
#include "vec.h"

namespace G {

class Keyboard {
 public:
  Keyboard(Allocator* allocator);
  bool IsDown(SDL_Scancode k) const { return pressed_[k]; }

  bool IsReleased(SDL_Scancode k) const {
    return previous_pressed_[k] && !pressed_[k];
  }

  bool IsPressed(SDL_Scancode k) const {
    return !previous_pressed_[k] && pressed_[k];
  }

  SDL_Scancode StrToScancode(std::string_view key) const {
    SDL_Scancode result;
    if (!table_.Lookup(key, &result)) {
      return SDL_SCANCODE_UNKNOWN;
    }
    return result;
  }

  void InitForFrame();

  void PushEvent(const SDL_Event& event);

  enum EventType { kDown, kUp };

  void ForAllKeypresses(void (*fn)(SDL_Scancode, EventType, void*),
                        void* userdata);

 private:
  inline static constexpr std::size_t kQueueSize = 256;
  inline static constexpr std::size_t kKeyboardTable = SDL_NUM_SCANCODES;

  struct Event {
    SDL_Scancode code;
    EventType type;
  };

  std::bitset<kKeyboardTable + 1> pressed_;
  std::bitset<kKeyboardTable + 1> previous_pressed_;
  Dictionary<SDL_Scancode> table_;
  FixedArray<Event> events_;
};

class Mouse {
 public:
  Mouse() {
    std::fill(pressed_.begin(), pressed_.end(), false);
    std::fill(previous_pressed_.begin(), previous_pressed_.end(), false);
  }
  enum Button { kLeft = 0, kRight = 1, kMiddle = 2 };
  static FVec2 GetPosition() {
    int x, y;
    SDL_GetMouseState(&x, &y);
    return FVec(x, y);
  }

  void InitForFrame() { previous_pressed_ = pressed_; }

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

 private:
  FVec2 mouse_wheel_ = FVec2::Zero();
  std::array<bool, 3> previous_pressed_, pressed_;
};

class Controllers {
 public:
  Controllers(DbAssets* assets, Allocator* allocator);
  ~Controllers();

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

  SDL_GameControllerButton StrToButton(std::string_view key) const {
    SDL_GameControllerButton result;
    if (!button_table_.Lookup(key, &result)) {
      return SDL_CONTROLLER_BUTTON_INVALID;
    }
    return result;
  }

  int AxisPositions(SDL_GameControllerAxis axis, int controller_id) const {
    if (controller_id == -1) return 0;
    return SDL_GameControllerGetAxis(controllers_[controller_id].ptr, axis);
  }

  int TriggerPositions(SDL_GameControllerAxis axis, int controller_id) const {
    if (controller_id == -1) return 0;
    return SDL_GameControllerGetAxis(controllers_[controller_id].ptr, axis);
  }

  SDL_GameControllerAxis StrToAxisOrTrigger(std::string_view key) const {
    SDL_GameControllerAxis result;
    if (!axis_table_.Lookup(key, &result)) {
      return SDL_CONTROLLER_AXIS_INVALID;
    }
    return result;
  }

  int active_controller() const { return active_controller_; }

 private:
  struct Controller {
    SDL_GameController* ptr = nullptr;
    std::bitset<32> pressed;
    std::bitset<32> previously_pressed;
  };
  std::array<Controller, 64> controllers_;
  std::bitset<64> open_controllers_;
  int active_controller_ = -1;
  Dictionary<SDL_GameControllerButton> button_table_;
  Dictionary<SDL_GameControllerAxis> axis_table_;
};

}  // namespace G

#endif  // _GAME_INPUT_H
