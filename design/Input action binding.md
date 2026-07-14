---
status: implemented
tags: [input, touch, actions, lua-api, gameplay]
---

# Input Action Binding and Touch

**Status: Implemented** (July 2026). Closes the action-binding gap
identified in [Engine comparison](Engine%20comparison.md) (parity with
high_impact and Anchor) and the touch portion of
[Multiplatform support](Multiplatform%20support.md).

## Overview

Named game actions bound to any combination of physical inputs:

```lua
G.input.bind("jump", { "key:space", "key:up", "mouse:left",
                       "gamepad:a", "touch" })
G.input.is_action_pressed("jump")   -- also is_action_down / _released
G.input.action_time("jump")         -- seconds held (fixed-timestep)
G.input.get_bindings("jump")        -- for settings menus; persistence is
                                    -- game code's job via G.save
```

Binding grammar parses at bind time and fails loudly (`luaL_error`);
querying an unbound action is also an error. Edges are **action-level**
(down = OR of bindings, edge against the action's previous frame), so
holding one bound key while tapping another does not re-fire "pressed".
Rebinding replaces bindings in place and resets state (hot-reload safe).

The `Touch` subsystem (`src/input.h`) tracks up to 10 fingers from SDL
finger events, reporting positions in viewport coordinates via the shared
`MapWindowToViewport` letterbox helper. Released fingers stay observable
for exactly one frame; `FINGER_CANCELED` counts as a release. Lua:
`G.input.touches()/touch_count()/is_touch_down/pressed/released`, with
`G.test.touch_down/move/up` for synthetic injection. SDL's default
touch→mouse emulation stays enabled, and the web shells set
`touch-action: none` on the canvas.

## Invariants

- `Actions::Update()` runs in `Game::RunFrame` **after** event polling and
  test injection — never from `Engine::StartFrame`, which precedes polling
  and would make action edges lag raw device queries by one frame.
- `action_time` counts display frames × `TimeStepInSeconds()`, so it is
  deterministic under `game run --test`.

## Deferred

Axis/trigger bindings (need threshold + deadzone policy), Lua touch event
callbacks (`G.touchpressed` style), virtual on-screen controls, and
declarative bindings in conf.json.

## Key files

`src/actions.{h,cc}`, `src/input.{h,cc}` (Touch), `src/lua_input.cc`,
`tests/test_actions.cc`, `tests/test_touch.cc`.
