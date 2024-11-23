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
  struct PressConditions {
    SDL_Scancode code;
    SDL_Keymod mods;

    PressConditions(SDL_Scancode c) : code(c), mods(KMOD_NONE) {}

    PressConditions() : PressConditions(SDL_SCANCODE_UNKNOWN) {}

    template <SDL_Keymod... Mods>
    PressConditions(SDL_Scancode c, std::initializer_list<SDL_Keymod> ms)
        : code(c), mods(KMOD_NONE) {
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
      return true;
    }
    return !(mods_ & p.mods);
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

 private:
  inline static constexpr size_t kQueueSize = 256;
  inline static constexpr size_t kKeyboardTable = SDL_NUM_SCANCODES;

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
