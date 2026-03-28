---
name: Tab is debug key
description: Tab key is reserved for the engine debug overlay — never bind it in test programs
type: feedback
---

Do not use Tab as a keybinding in test/example Lua programs.

**Why:** Tab is the engine's debug key.

**How to apply:** When assigning keybindings in test programs, skip Tab. Use letter keys (M, N, etc.) for toggles instead.
