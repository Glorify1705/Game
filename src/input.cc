#include <algorithm>

#include "clock.h"
#include "controller_db.h"
#include "input.h"

namespace G {

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
  table_.Insert("z", SDL_SCANCODE_Z);
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
    mouse_wheel_.x = std::clamp(0.0f, mouse_wheel_.x, 1.0f);
    mouse_wheel_.y = std::clamp(0.0f, mouse_wheel_.y, 1.0f);
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

Controllers::Controllers() {
  {
    TIMER("Processing Joystick Database");
    std::string_view db = ControllerDB();
    SDL_RWops* rwops = SDL_RWFromMem(
        const_cast<void*>(static_cast<const void*>(db.data())), db.size());
    CHECK(SDL_GameControllerAddMappingsFromRW(rwops, /*freerw=*/true) > 0,
          "Could not add Joystick database: ", SDL_GetError());
  }
  table_.Insert("a", SDL_CONTROLLER_BUTTON_A);
  table_.Insert("b", SDL_CONTROLLER_BUTTON_B);
  table_.Insert("x", SDL_CONTROLLER_BUTTON_X);
  table_.Insert("y", SDL_CONTROLLER_BUTTON_Y);
  table_.Insert("start", SDL_CONTROLLER_BUTTON_START);
  table_.Insert("back", SDL_CONTROLLER_BUTTON_BACK);
  const int controllers = SDL_NumJoysticks();
  CHECK(controllers >= 0, "Failed to get joysticks: ", SDL_GetError());
  DCHECK(controllers < controllers_.size());
  LOG("Found ", controllers, " joysticks");
  for (int i = 0; i < controllers; ++i) {
    if (!SDL_IsGameController(i)) {
      LOG("Skipping controller ", i);
      continue;
    }
    Controller& controller = controllers_[i];
    controller.ptr = SDL_GameControllerOpen(i);
    CHECK(controller.ptr, "Could not open controller ", i, ": ",
          SDL_GetError());
    LOG("Opened joystick: ", SDL_GameControllerName(controller.ptr));
    opened_controllers_[i] = true;
  }
}

void Controllers::InitForFrame() {
  for (size_t i = 0; i < controllers_.size(); ++i) {
    if (!opened_controllers_[i]) continue;
    controllers_[i].previously_pressed = controllers_[i].pressed;
    controllers_[i].pressed.reset();
  }
}

void Controllers::PushEvent(const SDL_Event& event) {
  if (event.type == SDL_CONTROLLERDEVICEADDED) {
    const int i = event.cdevice.which;
    DCHECK(i < controllers_.size());
    if (!opened_controllers_[i]) {
      controllers_[i].ptr = SDL_GameControllerOpen(i);
      CHECK(controllers_[i].ptr, "Could not open joystick ", i, ": ",
            SDL_GetError());
      opened_controllers_[i] = true;
      LOG("Opened joystick: ", SDL_GameControllerName(controllers_[i].ptr));
    }
  }
  if (event.type == SDL_JOYDEVICEREMOVED) {
    const int i = event.jdevice.which;
    DCHECK(i < controllers_.size());
    if (opened_controllers_[i] &&
        event.cdevice.which ==
            SDL_JoystickInstanceID(
                SDL_GameControllerGetJoystick(controllers_[i].ptr))) {
      LOG("Closed joystick: ", SDL_GameControllerName(controllers_[i].ptr));
      SDL_GameControllerClose(controllers_[i].ptr);
      opened_controllers_[i] = false;
    }
  }
  if (event.type == SDL_JOYBUTTONDOWN) {
    const int i = event.jbutton.which;
    DCHECK(i < controllers_.size());
    DCHECK(opened_controllers_[i]);
    DCHECK(event.jbutton.button < controllers_[i].pressed.size());
    controllers_[i].pressed[event.jbutton.button] = true;
    active_controller_ = i;
  }
  if (event.type == SDL_JOYBUTTONUP) {
    const int i = event.jbutton.which;
    DCHECK(i < controllers_.size());
    DCHECK(event.jbutton.button < controllers_[i].pressed.size());
    controllers_[i].pressed[event.jbutton.button] = true;
    active_controller_ = i;
  }
}

Controllers::~Controllers() {
  for (size_t i = 0; i < controllers_.size(); ++i) {
    if (opened_controllers_[i]) {
      SDL_GameControllerClose(controllers_[i].ptr);
    }
  }
}

}  // namespace G