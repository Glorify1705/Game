#include <algorithm>

#include "clock.h"
#include "controllerdb.h"
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

Keyboard::Keyboard(Allocator* allocator)
    : table_(allocator), events_(256, allocator) {
  table_.Insert("a", SDL_SCANCODE_A);
  table_.Insert("b", SDL_SCANCODE_B);
  table_.Insert("c", SDL_SCANCODE_C);
  table_.Insert("d", SDL_SCANCODE_D);
  table_.Insert("e", SDL_SCANCODE_E);
  table_.Insert("f", SDL_SCANCODE_F);
  table_.Insert("g", SDL_SCANCODE_G);
  table_.Insert("h", SDL_SCANCODE_H);
  table_.Insert("i", SDL_SCANCODE_I);
  table_.Insert("j", SDL_SCANCODE_J);
  table_.Insert("k", SDL_SCANCODE_K);
  table_.Insert("l", SDL_SCANCODE_L);
  table_.Insert("m", SDL_SCANCODE_M);
  table_.Insert("n", SDL_SCANCODE_N);
  table_.Insert("o", SDL_SCANCODE_O);
  table_.Insert("p", SDL_SCANCODE_P);
  table_.Insert("q", SDL_SCANCODE_Q);
  table_.Insert("r", SDL_SCANCODE_R);
  table_.Insert("s", SDL_SCANCODE_S);
  table_.Insert("t", SDL_SCANCODE_T);
  table_.Insert("u", SDL_SCANCODE_U);
  table_.Insert("v", SDL_SCANCODE_V);
  table_.Insert("w", SDL_SCANCODE_W);
  table_.Insert("x", SDL_SCANCODE_X);
  table_.Insert("y", SDL_SCANCODE_Y);
  table_.Insert("z", SDL_SCANCODE_Z);
  table_.Insert("0", SDL_SCANCODE_0);
  table_.Insert("1", SDL_SCANCODE_1);
  table_.Insert("2", SDL_SCANCODE_2);
  table_.Insert("3", SDL_SCANCODE_3);
  table_.Insert("4", SDL_SCANCODE_4);
  table_.Insert("5", SDL_SCANCODE_5);
  table_.Insert("6", SDL_SCANCODE_6);
  table_.Insert("7", SDL_SCANCODE_7);
  table_.Insert("8", SDL_SCANCODE_8);
  table_.Insert("9", SDL_SCANCODE_9);
  table_.Insert("tab", SDL_SCANCODE_TAB);
  table_.Insert("backspace", SDL_SCANCODE_BACKSPACE);
  table_.Insert("enter", SDL_SCANCODE_RETURN);
  table_.Insert("return", SDL_SCANCODE_RETURN);
  table_.Insert("lctrl", SDL_SCANCODE_LCTRL);
  table_.Insert("rctrl", SDL_SCANCODE_RCTRL);
  table_.Insert("lalt", SDL_SCANCODE_LALT);
  table_.Insert("ralt", SDL_SCANCODE_RALT);
  table_.Insert("lshift", SDL_SCANCODE_LSHIFT);
  table_.Insert("rshift", SDL_SCANCODE_RSHIFT);
  table_.Insert("f1", SDL_SCANCODE_F1);
  table_.Insert("f2", SDL_SCANCODE_F2);
  table_.Insert("f3", SDL_SCANCODE_F3);
  table_.Insert("f4", SDL_SCANCODE_F4);
  table_.Insert("f5", SDL_SCANCODE_F5);
  table_.Insert("f6", SDL_SCANCODE_F6);
  table_.Insert("f7", SDL_SCANCODE_F7);
  table_.Insert("f8", SDL_SCANCODE_F8);
  table_.Insert("f9", SDL_SCANCODE_F9);
  table_.Insert("f10", SDL_SCANCODE_F10);
  table_.Insert("f11", SDL_SCANCODE_F11);
  table_.Insert("f12", SDL_SCANCODE_F12);
  table_.Insert("escape", SDL_SCANCODE_ESCAPE);
  table_.Insert("esc", SDL_SCANCODE_ESCAPE);
}

void Keyboard::PushEvent(const SDL_Event& event) {
  if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
    const SDL_Scancode c = event.key.keysym.scancode;
    pressed_[c] = event.type == SDL_KEYDOWN;
  }
}

void Keyboard::ForAllKeypresses(void (*fn)(SDL_Scancode, EventType, void*),
                                void* userdata) {
  for (const Event& e : events_) {
    fn(e.code, e.type, userdata);
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

Controllers::Controllers(const Assets& assets, Allocator* allocator)
    : button_table_(allocator), axis_table_(allocator) {
  SDL_RWops* rwops = nullptr;
  const TextFileAsset* asset = assets.GetText("gamecontrollerdb.txt");
  if (asset == nullptr) {
    LOG("Could not find game controller database, using the default one");
    rwops = SDL_RWFromMem(
        const_cast<void*>(static_cast<const void*>(kControllerDatabase)),
        sizeof(kControllerDatabase));
  } else {
    rwops = SDL_RWFromMem(
        const_cast<void*>(static_cast<const void*>(asset->contents()->Data())),
        asset->contents()->size());
  }
  CHECK(rwops != nullptr);
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