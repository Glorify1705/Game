#include <algorithm>

#include "clock.h"
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
  table_.Insert("f", SDL_SCANCODE_F);
  table_.Insert("q", SDL_SCANCODE_Q);
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

Controllers::Controllers(const Assets& assets) {
  const TextFileAsset* asset = assets.GetText("gamecontrollerdb.txt");
  CHECK(asset != nullptr, "Could not find game controller database");
  SDL_RWops* rwops = SDL_RWFromMem(
      const_cast<void*>(static_cast<const void*>(asset->contents()->Data())),
      asset->contents()->size());
  CHECK(SDL_GameControllerAddMappingsFromRW(rwops, /*freerw=*/true) > 0,
        "Could not add Joystick database: ", SDL_GetError());
  // Button table.
  button_table_.Insert("a", SDL_CONTROLLER_BUTTON_A);
  button_table_.Insert("b", SDL_CONTROLLER_BUTTON_B);
  button_table_.Insert("x", SDL_CONTROLLER_BUTTON_X);
  button_table_.Insert("y", SDL_CONTROLLER_BUTTON_Y);
  button_table_.Insert("start", SDL_CONTROLLER_BUTTON_START);
  button_table_.Insert("back", SDL_CONTROLLER_BUTTON_BACK);
  button_table_.Insert("dpadl", SDL_CONTROLLER_BUTTON_DPAD_LEFT);
  button_table_.Insert("dpadr", SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  button_table_.Insert("dpadu", SDL_CONTROLLER_BUTTON_DPAD_UP);
  button_table_.Insert("dpadd", SDL_CONTROLLER_BUTTON_DPAD_DOWN);
  // Axis table.
  axis_table_.Insert("lanalogx", SDL_CONTROLLER_AXIS_LEFTX);
  axis_table_.Insert("ranalogx", SDL_CONTROLLER_AXIS_RIGHTX);
  axis_table_.Insert("lanalogy", SDL_CONTROLLER_AXIS_LEFTY);
  axis_table_.Insert("ranalogy", SDL_CONTROLLER_AXIS_RIGHTY);
  axis_table_.Insert("ltrigger", SDL_CONTROLLER_AXIS_TRIGGERLEFT);
  axis_table_.Insert("rtrigger", SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
  // Load controllers.
  const int controllers = SDL_NumJoysticks();
  CHECK(controllers >= 0, "Failed to get joysticks: ", SDL_GetError());
  DCHECK(static_cast<size_t>(controllers) < controllers_.size());
  if (controllers == 0) LOG("Found no joysticks");
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
    open_controllers_[i] = true;
  }
}

void Controllers::InitForFrame() {
  for (size_t i = 0; i < controllers_.size(); ++i) {
    if (!open_controllers_[i]) continue;
    controllers_[i].previously_pressed = controllers_[i].pressed;
    controllers_[i].pressed.reset();
  }
}

void Controllers::PushEvent(const SDL_Event& event) {
  if (event.type == SDL_CONTROLLERDEVICEADDED) {
    const size_t i = event.cdevice.which;
    DCHECK(i < controllers_.size());
    if (!open_controllers_[i]) {
      controllers_[i].ptr = SDL_GameControllerOpen(i);
      CHECK(controllers_[i].ptr, "Could not open joystick ", i, ": ",
            SDL_GetError());
      open_controllers_[i] = true;
      LOG("Opened joystick: ", SDL_GameControllerName(controllers_[i].ptr));
    }
  }
  if (event.type == SDL_JOYDEVICEREMOVED) {
    const size_t i = event.jdevice.which;
    DCHECK(i < controllers_.size());
    if (open_controllers_[i] &&
        event.cdevice.which ==
            SDL_JoystickInstanceID(
                SDL_GameControllerGetJoystick(controllers_[i].ptr))) {
      LOG("Closed joystick: ", SDL_GameControllerName(controllers_[i].ptr));
      SDL_GameControllerClose(controllers_[i].ptr);
      open_controllers_[i] = false;
    }
  }
  if (event.type == SDL_JOYBUTTONDOWN) {
    const size_t i = event.jbutton.which;
    DCHECK(i < controllers_.size());
    DCHECK(open_controllers_[i]);
    DCHECK(event.jbutton.button < controllers_[i].pressed.size());
    controllers_[i].pressed[event.jbutton.button] = true;
    active_controller_ = i;
  }
  if (event.type == SDL_JOYBUTTONUP) {
    const size_t i = event.jbutton.which;
    DCHECK(i < controllers_.size());
    DCHECK(event.jbutton.button < controllers_[i].pressed.size());
    controllers_[i].pressed[event.jbutton.button] = true;
    active_controller_ = i;
  }
}

Controllers::~Controllers() {
  for (size_t i = 0; i < controllers_.size(); ++i) {
    if (open_controllers_[i]) {
      SDL_GameControllerClose(controllers_[i].ptr);
    }
  }
}

}  // namespace G