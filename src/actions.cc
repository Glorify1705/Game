#include "actions.h"

#include "clock.h"
#include "string_table.h"
#include "stringlib.h"

namespace G {

namespace {

// Parses the "mouse:" suffix into a Mouse button index, or -1.
int ParseMouseButton(std::string_view name) {
  if (name == "left" || name == "0") return 0;
  if (name == "middle" || name == "1") return 1;
  if (name == "right" || name == "2") return 2;
  return -1;
}

}  // namespace

Actions::Actions(Keyboard* keyboard, Mouse* mouse, Controllers* controllers,
                 Touch* touch, Allocator* allocator)
    : keyboard_(keyboard),
      mouse_(mouse),
      controllers_(controllers),
      touch_(touch),
      by_name_(allocator),
      storage_(allocator) {}

bool Actions::ParseBinding(std::string_view str, Binding* out) const {
  out->str_handle = StringIntern(str);
  if (str == "touch") {
    out->device = Device::kTouch;
    return true;
  }
  std::string_view name = str;
  if (ConsumePrefix(&name, "key:")) {
    const Keyboard::PressConditions key = keyboard_->MapKey(name);
    if (key.code == SDL_SCANCODE_UNKNOWN) return false;
    out->device = Device::kKeyboard;
    out->key = key;
    return true;
  }
  name = str;
  if (ConsumePrefix(&name, "mouse:")) {
    const int button = ParseMouseButton(name);
    if (button < 0) return false;
    out->device = Device::kMouse;
    out->mouse_button = button;
    return true;
  }
  name = str;
  if (ConsumePrefix(&name, "gamepad:")) {
    const SDL_GamepadButton button = controllers_->StrToButton(name);
    if (button == SDL_GAMEPAD_BUTTON_INVALID) return false;
    out->device = Device::kGamepad;
    out->pad_button = button;
    return true;
  }
  return false;
}

ErrorOr<void> Actions::Bind(std::string_view action,
                            Slice<const std::string_view> bindings) {
  if (bindings.empty()) {
    return Error::Message("an action needs at least one binding");
  }
  if (bindings.size() > kMaxBindingsPerAction) {
    return Error::Message("too many bindings for action (max 8)");
  }
  Action parsed;
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (!ParseBinding(bindings[i], &parsed.bindings[i])) {
      return Error::Message(
          "invalid binding (expected \"key:<name>\", "
          "\"mouse:<left|middle|right|0|1|2>\", \"gamepad:<button>\", or "
          "\"touch\")");
    }
  }
  parsed.binding_count = bindings.size();

  Action* slot;
  if (by_name_.Lookup(action, &slot)) {
    // Rebind: replace bindings in place and reset edge/hold state.
    *slot = parsed;
    return {};
  }
  slot = storage_.Push(parsed);
  by_name_.Insert(action, slot);
  return {};
}

bool Actions::Has(std::string_view action) const {
  return by_name_.Contains(action);
}

size_t Actions::GetBindings(std::string_view action, std::string_view* out,
                            size_t capacity) const {
  const Action* found = Find(action);
  if (found == nullptr) return 0;
  const size_t count =
      found->binding_count < capacity ? found->binding_count : capacity;
  for (size_t i = 0; i < count; ++i) {
    out[i] = StringByHandle(found->bindings[i].str_handle);
  }
  return count;
}

bool Actions::IsDown(std::string_view action) const {
  const Action* found = Find(action);
  return found != nullptr && found->down;
}

bool Actions::IsPressed(std::string_view action) const {
  const Action* found = Find(action);
  return found != nullptr && found->down && !found->previous_down;
}

bool Actions::IsReleased(std::string_view action) const {
  const Action* found = Find(action);
  return found != nullptr && !found->down && found->previous_down;
}

double Actions::DownTime(std::string_view action) const {
  const Action* found = Find(action);
  if (found == nullptr || !found->down) return 0;
  return static_cast<double>(found->frames_down) * TimeStepInSeconds();
}

bool Actions::BindingDown(const Binding& binding) const {
  switch (binding.device) {
    case Device::kKeyboard:
      return keyboard_->IsDown(binding.key);
    case Device::kMouse:
      return mouse_->IsDown(binding.mouse_button);
    case Device::kGamepad:
      return controllers_->IsDown(binding.pad_button,
                                  controllers_->active_controller());
    case Device::kTouch:
      return touch_->AnyDown();
  }
  DIE("Unexpected binding device: ", static_cast<int>(binding.device));
}

const Actions::Action* Actions::Find(std::string_view action) const {
  Action* found = nullptr;
  by_name_.Lookup(action, &found);
  return found;
}

void Actions::Update() {
  for (Action& action : storage_) {
    action.previous_down = action.down;
    bool down = false;
    for (size_t i = 0; i < action.binding_count && !down; ++i) {
      down = BindingDown(action.bindings[i]);
    }
    action.down = down;
    action.frames_down = down ? action.frames_down + 1 : 0;
  }
}

}  // namespace G
