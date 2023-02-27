#include "input.h"

#include <algorithm>

template <typename T>
T Clamp(T min, T val, T max) {
  return std::min(max, std::max(min, val));
}

void Keyboard::InitForFrame() {
  int size;
  auto* keyboard = SDL_GetKeyboardState(&size);
  DCHECK(size == kKeyboardTable, " unexpected keyboard table");
  previous_pressed_.reset();
  for (int i = 0; i < size; ++i) {
    previous_pressed_[i] = keyboard[i];
  }
}

Keyboard::Keyboard() {
  table_.Insert("w", SDL_SCANCODE_W);
  table_.Insert("a", SDL_SCANCODE_A);
  table_.Insert("s", SDL_SCANCODE_S);
  table_.Insert("d", SDL_SCANCODE_D);
  table_.Insert("lshift", SDL_SCANCODE_LSHIFT);
  table_.Insert("rshift", SDL_SCANCODE_RSHIFT);
}

void Keyboard::PushEvent(const SDL_Event& event) {
  if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
    const SDL_Scancode c = event.key.keysym.scancode;
    keydown_events_.Push(c);
    pressed_[c] = event.type == SDL_KEYDOWN;
  }
}

void Mouse::PushEvent(const SDL_Event& event) {
  if (event.type == SDL_MOUSEWHEEL) {
    mouse_wheel_ += FVec(event.wheel.x, event.wheel.y) / 50;
    mouse_wheel_.x = Clamp(0.0f, mouse_wheel_.x, 1.0f);
    mouse_wheel_.y = Clamp(0.0f, mouse_wheel_.y, 1.0f);
  }
  if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
    if (event.button.button == SDL_BUTTON_LEFT) {
      pressed_[0] = event.type == SDL_MOUSEBUTTONDOWN;
    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
      pressed_[1] = event.type == SDL_MOUSEBUTTONDOWN;
    } else if (event.button.button == SDL_BUTTON_RIGHT) {
      pressed_[2] = event.type == SDL_MOUSEBUTTONDOWN;
    }
  }
}