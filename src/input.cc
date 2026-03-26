#include "input.h"

#include <algorithm>

#include "controllerdb.h"

namespace G {

void Keyboard::InitForFrame() {
  int size;
  auto* keyboard = SDL_GetKeyboardState(&size);
  DCHECK(size == kKeyboardTable, " unexpected keyboard table");
  previous_pressed_.reset();
  for (int i = 0; i < size; ++i) {
    previous_pressed_[i] = keyboard[i];
  }
  previous_mods_ = mods_;
  mods_ = SDL_GetModState();
}

Keyboard::Keyboard(Allocator* allocator)
    : table_(allocator), events_(256, allocator) {
  char buf[256];
  for (int i = SDL_SCANCODE_UNKNOWN; i < SDL_SCANCODE_COUNT; ++i) {
    auto scancode = static_cast<SDL_Scancode>(i);
    const char* name = SDL_GetScancodeName(scancode);
    if (name == nullptr) continue;
    if (!*name) continue;
    strcpy(buf, name);
    for (char* c = buf; *c; c++) {
      if (*c >= 'A' && *c <= 'Z') *c = *c - 'A' + 'a';
    }
    table_.Insert(buf, scancode);
  }
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
  table_.Insert("space", SDL_SCANCODE_SPACE);
  table_.Insert("spacebar", SDL_SCANCODE_SPACE);
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
  table_.Insert("+", {SDL_SCANCODE_EQUALS, {SDL_KMOD_SHIFT}});
}

void Keyboard::PushEvent(const SDL_Event& event) {
  if (event.type == SDL_EVENT_KEY_DOWN) {
    const SDL_Scancode c = event.key.scancode;
    const auto mod = event.key.mod;
    pressed_[c] = true;
    mods_ = static_cast<SDL_Keymod>(mods_ | mod);
  } else if (event.type == SDL_EVENT_KEY_UP) {
    const SDL_Scancode c = event.key.scancode;
    const auto mod = event.key.mod;
    pressed_[c] = false;
    mods_ = static_cast<SDL_Keymod>(mods_ & ~mod);
  }
}

void Mouse::PushEvent(const SDL_Event& event) {
  if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    mouse_wheel_ += FVec(event.wheel.x, event.wheel.y) / 50;
    mouse_wheel_.x = std::clamp(0.0f, mouse_wheel_.x, 1.0f);
    mouse_wheel_.y = std::clamp(0.0f, mouse_wheel_.y, 1.0f);
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
      event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
    if (event.button.button == SDL_BUTTON_LEFT) {
      pressed_[0] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
      pressed_[1] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    } else if (event.button.button == SDL_BUTTON_RIGHT) {
      pressed_[2] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    }
  }
}

Controllers::Controllers(Allocator* allocator)
    : button_table_(allocator), axis_table_(allocator) {
  // Button table.
  button_table_.Insert("a", SDL_GAMEPAD_BUTTON_SOUTH);
  button_table_.Insert("b", SDL_GAMEPAD_BUTTON_EAST);
  button_table_.Insert("x", SDL_GAMEPAD_BUTTON_WEST);
  button_table_.Insert("y", SDL_GAMEPAD_BUTTON_NORTH);
  button_table_.Insert("start", SDL_GAMEPAD_BUTTON_START);
  button_table_.Insert("back", SDL_GAMEPAD_BUTTON_BACK);
  button_table_.Insert("dpadl", SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  button_table_.Insert("dpadr", SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
  button_table_.Insert("dpadu", SDL_GAMEPAD_BUTTON_DPAD_UP);
  button_table_.Insert("dpadd", SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  // Axis table.
  axis_table_.Insert("lanalogx", SDL_GAMEPAD_AXIS_LEFTX);
  axis_table_.Insert("ranalogx", SDL_GAMEPAD_AXIS_RIGHTX);
  axis_table_.Insert("lanalogy", SDL_GAMEPAD_AXIS_LEFTY);
  axis_table_.Insert("ranalogy", SDL_GAMEPAD_AXIS_RIGHTY);
  axis_table_.Insert("ltrigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
  axis_table_.Insert("rtrigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
}

namespace {
SDL_IOStream* IOStreamFromMemory(ByteSlice data) {
  SDL_IOStream* io =
      SDL_IOFromMem(const_cast<uint8_t*>(data.data()), data.size());
  CHECK(io != nullptr);
  return io;
}
}  // namespace

void Controllers::Initialize(ByteSlice db) {
  if (db.empty()) {
    LOG("Using the default controllers database");
    db = MakeByteSlice(kControllerDatabase);
  } else {
    LOG("Using custom controllers database");
  }
  SDL_IOStream* io = IOStreamFromMemory(db);
  CHECK(SDL_AddGamepadMappingsFromIO(io, /*closeio=*/true) > 0,
        "Could not add Joystick database: ", SDL_GetError());
  // Open controllers.
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  DCHECK(static_cast<size_t>(count) < controllers_.size());
  if (count == 0) LOG("Found no joysticks");
  for (int i = 0; i < count; ++i) {
    Controller& controller = controllers_[i];
    controller.ptr = SDL_OpenGamepad(ids[i]);
    CHECK(controller.ptr, "Could not open controller ", i, ": ",
          SDL_GetError());
    LOG("Opened joystick: ", SDL_GetGamepadName(controller.ptr));
    open_controllers_[i] = true;
  }
  SDL_free(ids);
}

void Controllers::InitForFrame() {
  for (size_t i = 0; i < controllers_.size(); ++i) {
    if (!open_controllers_[i]) continue;
    controllers_[i].previously_pressed = controllers_[i].pressed;
    controllers_[i].pressed.reset();
  }
}

void Controllers::PushEvent(const SDL_Event& event) {
  if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
    const size_t i = event.gdevice.which;
    DCHECK(i < controllers_.size());
    if (!open_controllers_[i]) {
      controllers_[i].ptr = SDL_OpenGamepad(event.gdevice.which);
      CHECK(controllers_[i].ptr, "Could not open joystick ", i, ": ",
            SDL_GetError());
      open_controllers_[i] = true;
      LOG("Opened joystick: ", SDL_GetGamepadName(controllers_[i].ptr));
    }
  }
  if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
    const size_t i = event.jdevice.which;
    DCHECK(i < controllers_.size());
    if (open_controllers_[i] &&
        event.jdevice.which ==
            SDL_GetJoystickID(SDL_GetGamepadJoystick(controllers_[i].ptr))) {
      LOG("Closed joystick: ", SDL_GetGamepadName(controllers_[i].ptr));
      SDL_CloseGamepad(controllers_[i].ptr);
      open_controllers_[i] = false;
    }
  }
  if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
    const size_t i = event.jbutton.which;
    DCHECK(i < controllers_.size());
    DCHECK(open_controllers_[i]);
    DCHECK(event.jbutton.button < controllers_[i].pressed.size());
    controllers_[i].pressed[event.jbutton.button] = true;
    active_controller_ = i;
  }
  if (event.type == SDL_EVENT_JOYSTICK_BUTTON_UP) {
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
      SDL_CloseGamepad(controllers_[i].ptr);
    }
  }
}

}  // namespace G
