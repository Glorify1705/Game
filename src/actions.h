#pragma once
#ifndef _GAME_ACTIONS_H
#define _GAME_ACTIONS_H

#include <cstdint>
#include <string_view>

#include "allocators.h"
#include "array.h"
#include "dictionary.h"
#include "error.h"
#include "input.h"
#include "segmented_list.h"

namespace G {

// Maps named game actions ("jump", "fire") to physical input bindings and
// answers edge and hold queries. An action is down while any of its
// bindings is down; pressed/released edges are computed at the action
// level, so holding one bound key while tapping another does not produce a
// second pressed edge.
//
// Update() must run once per frame after event polling and test-input
// injection so that action edges agree with raw device queries made in the
// same frame.
class Actions {
 public:
  // Maximum bindings per action; Bind fails beyond this.
  inline static constexpr size_t kMaxBindingsPerAction = 8;

  Actions(Keyboard* keyboard, Mouse* mouse, Controllers* controllers,
          Touch* touch, Allocator* allocator);

  // (Re)binds an action, replacing any previous bindings and resetting its
  // state. Binding forms: "key:<name>", "mouse:<left|middle|right|0|1|2>",
  // "gamepad:<button>", and "touch" (any finger).
  ErrorOr<void> Bind(std::string_view action,
                     Slice<const std::string_view> bindings);

  // Returns true if the action has been bound.
  bool Has(std::string_view action) const;

  // Copies up to |capacity| binding strings (exactly as passed to Bind)
  // into |out|. Returns the number written; 0 for unbound actions.
  size_t GetBindings(std::string_view action, std::string_view* out,
                     size_t capacity) const;

  // True while any binding of the action is down.
  bool IsDown(std::string_view action) const;

  // True the frame the action transitions to down.
  bool IsPressed(std::string_view action) const;

  // True the frame the action transitions to up.
  bool IsReleased(std::string_view action) const;

  // Seconds the action has been continuously down (display frames times
  // the fixed timestep, so deterministic under --test); 0 when not down.
  double DownTime(std::string_view action) const;

  // Recomputes every action's state from the current device state. Call
  // once per frame, after events and test injection, before game update.
  void Update();

 private:
  // Input source of a single binding.
  enum class Device : uint8_t {
    kKeyboard,  // A key with optional modifiers.
    kMouse,     // A mouse button index.
    kGamepad,   // A gamepad button on the active controller.
    kTouch,     // Any finger touching the screen.
  };

  struct Binding {
    Device device;
    union {
      Keyboard::PressConditions key;
      int mouse_button;
      SDL_GamepadButton pad_button;
    };
    // Interned original binding string, for GetBindings.
    uint32_t str_handle;

    Binding() : device(Device::kTouch), str_handle(0) {}
  };

  struct Action {
    Binding bindings[kMaxBindingsPerAction];
    size_t binding_count = 0;
    bool down = false;
    bool previous_down = false;
    uint64_t frames_down = 0;
  };

  // Parses one binding string; false on unknown device or name.
  bool ParseBinding(std::string_view str, Binding* out) const;
  // True if the physical input behind the binding is currently down.
  bool BindingDown(const Binding& binding) const;
  const Action* Find(std::string_view action) const;

  Keyboard* keyboard_;
  Mouse* mouse_;
  Controllers* controllers_;
  Touch* touch_;
  Dictionary<Action*> by_name_;
  // Segmented so the pointers held by by_name_ stay stable as it grows.
  SegmentedList<Action> storage_;
};

}  // namespace G

#endif  // _GAME_ACTIONS_H
