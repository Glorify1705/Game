---
status: implemented
tags: [wasm, web, emscripten, packaging, portability]
---

# Web Target (Emscripten / WebGL2)

**Status: Implemented** (PR #93, July 2026). Covers the WASM portion of
[Multiplatform support](Multiplatform%20support.md); Android/iOS remain
future work.

## Overview

`game package --target web` produces a browser-ready directory
(`index.html`, `game.js`, `game.wasm`, `assets.sqlite3`, `assets.zip`)
that zips straight onto itch.io or any static host. The web engine is a
generic prebuilt artifact (`game-build-web` → `build-web/`); packaging a
game requires no Emscripten tooling — the emitted HTML shell fetches the
two asset files into MEMFS before `main()` runs.

Key decisions:

- **Single-threaded wasm32, fixed 512 MB memory** (no SharedArrayBuffer,
  no memory growth — memory-deterministic like desktop). Budgets in
  `src/memory_budgets.h`.
- **WebGL2 via GLES 3.0**: `src/gl_headers.h` shim (glad on desktop),
  per-platform GLSL prelude (`#version 300 es` + precision) prepended at
  shader compile time, MSAA via multisampled renderbuffer (GLES3 has no
  multisample textures), texture swizzle skipped (removed from WebGL2).
- **Main loop**: `Game::RunFrame()` driven by
  `emscripten_set_main_loop_arg` under requestAnimationFrame; all loop
  state lives in a heap-allocated `GameContext` because the emscripten
  unwind skips destructors.
- **Encapsulation**: `src/web_platform.{h,cc}` is the only translation
  unit including `<emscripten.h>`; per-platform files (network stub) are
  selected via `ENGINE_PLATFORM_SRCS` in CMake.
- **Saves** persist to IndexedDB (IDBFS at `/save`, `journal_mode=MEMORY`,
  debounced `FS.syncfs`). Only `G.save` persists; `G.filesystem` writes
  are ephemeral.
- **v1 limitations**: no networking (`G.network` errors), no debug UI,
  1px lines, 32 texture units.

## Verification tooling

`scripts/web/serve.py` + `scripts/web/index.html` (dev shell): the shell
POSTs engine logs and rendered frames (readPixels in a post-render rAF)
back to the server, so headless Firefox can assert on boot, rendering,
and IndexedDB persistence without a display.
