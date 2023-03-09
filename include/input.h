#pragma once
#ifndef _GAME_INPUT_H
#define _GAME_INPUT_H

#include <array>
#include <bitset>
#include <cstdint>

#include "SDL.h"
#include "circular_buffer.h"
#include "map.h"
#include "vec.h"

namespace G {

class Keyboard {
 public:
  Keyboard();
  bool IsDown(SDL_Scancode k) const { return pressed_[k]; }

  bool IsReleased(SDL_Scancode k) const {
    return previous_pressed_[k] && !pressed_[k];
  }

  bool IsPressed(SDL_Scancode k) const {
    return !previous_pressed_[k] && pressed_[k];
  }

  SDL_Scancode StrToScancode(const char* key, size_t length) const {
    SDL_Scancode result;
    if (!table_.Lookup(key, length, &result)) {
      return SDL_SCANCODE_UNKNOWN;
    }
    return result;
  }

  void InitForFrame();

  void PushEvent(const SDL_Event& event);

 private:
  inline static constexpr size_t kQueueSize = 256;
  inline static constexpr size_t kKeyboardTable = SDL_NUM_SCANCODES;
  using Event = char;

  FixedCircularBuffer<Event, kQueueSize> keyup_events_;
  FixedCircularBuffer<Event, kQueueSize> keydown_events_;
  std::bitset<kKeyboardTable + 1> pressed_;
  std::bitset<kKeyboardTable + 1> previous_pressed_;
  LookupTable<SDL_Scancode> table_;
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

  void InitForFrame() { pressed_ = previous_pressed_; }

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
  Controllers();
  ~Controllers();

  void PushEvent(const SDL_Event& event);

  void InitForFrame();

  int joysticks() const { return controllers_.size(); }

 private:
  struct Controller {
    SDL_GameController* ptr = nullptr;
    std::bitset<32> pressed;
    std::bitset<32> previously_pressed;
  };
  std::array<Controller, 64> controllers_;
  std::bitset<64> opened_controllers_;
};

}  // namespace G

#endif  // _GAME_INPUT_H