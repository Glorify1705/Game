# REPL and Live Interaction

The engine already supports Fennel (a Lisp that compiles to Lua) and has file-watching hot reload. But there is no way to evaluate code interactively against the running game — you must save a file, wait for the watcher to detect it, and the entire Lua state reinitializes (`LoadMain` + `Init`). This document proposes a REPL system that allows evaluating arbitrary Lua/Fennel expressions against the live game state, from a browser, a terminal, or an LLM agent.

## Goals

1. A developer can type a Lua or Fennel expression and see the result immediately, without restarting the game or losing state.
2. The REPL is accessible from a browser tab, so it doesn't fight the game window for focus.
3. The `G.*` API (graphics, input, physics, sound, etc.) is fully available from the REPL.
4. An LLM (Claude, etc.) can connect to the REPL programmatically and send/receive structured messages, enabling AI-assisted testing and exploration.
5. The REPL integrates with the existing hot reload — a file save triggers a full reload, but REPL evaluation does not.

## Non-goals

- A full IDE or visual debugger (breakpoints, stepping, watch windows). This is a REPL, not a debugger.
- Replacing the file-based hot reload. The REPL complements it.
- An in-game overlay console rendered inside the game window. That requires its own UI system (text input, scrolling, focus management). A browser-based REPL avoids all of that.
- Emscripten/WASM support. The engine is Linux-only for now.

## Prior art survey

### How other engines solve this

**LÖVE2D + Lovebird** — The closest analog to what we want. [Lovebird](https://github.com/rxi/lovebird) runs a tiny HTTP server inside the game process. A browser at `http://127.0.0.1:8000` shows an interactive Lua console. You type expressions, they execute in the game's Lua VM, results display in the browser. All `print()` output streams to the browser in real time. The entire thing is a single Lua file — you add `lovebird.update()` to your game loop.

**Strengths:** Dead simple, browser UI is free (HTML/CSS), decoupled from the game window, no dependencies beyond Lua's socket library.
**Weaknesses:** HTTP request/response is half-duplex — no streaming output, no push notifications. Polling for `print()` output. No structured protocol for tool integration.

**DragonRuby** — C engine with embedded mRuby. Has a built-in console and a `repl.rb` file that is evaluated on save. The critical design choice: all game state flows through an `args` object owned by the engine, not module-level variables. Code is stateless by convention, so reloading code never loses state.

**Strengths:** State preservation by design (the `args` architecture). Gameplay recording/replay against new code.
**Weaknesses:** Requires buying into the `args`-first architecture. mRuby is not standard Ruby.

**Common Lisp (Trial/Kandria)** — The gold standard. The game runs inside SBCL connected to Emacs via SLIME/SWANK. `C-c C-c` compiles a single function and the change goes live instantly — no restart, no reload, the new function replaces the old one in the running image. CLOS even migrates existing instances when class definitions change. Kandria shipped on Steam using this workflow.

**Strengths:** The most powerful live modification capability. Everything is redefiniable. The running image IS the state.
**Weaknesses:** Common Lisp is niche, SBCL binaries are large, Emacs/SLIME learning curve.

**Fennel + LÖVE2D** — A separate Lua thread reads stdin and evaluates Fennel expressions in the game context. The `min-love2d-fennel` template includes error recovery — if a reload causes an error, it switches to recovery mode instead of crashing. Fennel's `,reload module-name` command ejects a module from `package.loaded` and re-requires it.

**Phaser.js** — JavaScript in the browser. The browser DevTools IS the REPL. You type `game.scene.scenes[0].player.x = 100` in the console and the player moves. Zero additional tooling needed.

**Godot** — Separate process model. Editor and game communicate over TCP (port 6007). A "Remote" tab shows the live node tree. An expression evaluator was merged in Godot 4.4, but only works at breakpoints.

**PICO-8** — The command prompt on boot IS a Lua REPL. Typing `.` advances one frame (calls `_update()` then `_draw()`). This frame-advance-from-REPL is a uniquely powerful debugging feature.

**TIC-80** — Supports Fennel as a scripting language. Built-in code editor, but no live REPL — edit and restart.

### Protocol survey

**SWANK (SLIME):** S-expression messages prefixed by 6-char hex length. Request IDs for async matching. Message types: `:emacs-rex` (eval), `:return` (result), `:emacs-interrupt`. Battle-tested for decades.

**nREPL (Clojure):** TCP + bencode serialization. Messages are associative maps with `:id`, `:op`, `:code`, `:ns`. Middleware-based architecture. Every request provokes one or more responses; `:status ["done"]` signals completion.

**Both are overkill for our use case.** We don't need middleware, namespaces, or completion. But they demonstrate the core pattern: a TCP server in the game process, message framing with request IDs, and eval/result message types.

### Key lessons

1. **The state problem is universal.** Every engine struggles with preserving state across code reloads. Solutions range from "don't bother" (PICO-8) to "state is a parameter" (DragonRuby's `args`) to "the image IS the state" (Common Lisp). Our REPL sidesteps this by evaluating in-place — no reload, no state loss.

2. **A network protocol enables tooling.** SWANK, nREPL, Lovebird's HTTP, Godot's TCP — separating the REPL from the game process enables editor integration, remote debugging, and LLM interaction. A simple TCP + JSON protocol is the right choice for us.

3. **Browser-based UIs are underrated.** Lovebird and Phaser show that a browser tab is an excellent debug interface — decoupled from the game, rich rendering, accessible remotely, zero UI code on the engine side.

4. **Frame-advance from the REPL is powerful.** PICO-8's `.` command (advance one frame) is extremely useful for debugging. We should support this.

## Architecture

### Overview

```
┌─────────────────────────────────────────────────┐
│                   Game Process                   │
│                                                  │
│  ┌──────────┐   ┌──────────┐   ┌─────────────┐  │
│  │ Game Loop │──▶│  Lua VM  │◀──│ REPL Server │  │
│  └──────────┘   └──────────┘   └──────┬───────┘  │
│                                       │ TCP :9741 │
└───────────────────────────────────────┼──────────┘
                                        │
                    ┌───────────────────┼───────────┐
                    │                   │           │
              ┌─────▼──────┐   ┌───────▼───┐  ┌───▼────┐
              │   Browser   │   │   Claude  │  │ Editor │
              │  (HTML/JS)  │   │  (MCP/API)│  │(future)│
              └────────────┘   └───────────┘  └────────┘
```

Two components:

1. **REPL Server (C++)** — A TCP server running inside the game process, on a dedicated thread. Accepts connections, receives eval requests, queues them for the main thread, and sends back results. Also serves a small embedded HTML page for the browser UI.
2. **Browser UI (HTML/JS)** — A single HTML page with a terminal-style REPL. Connects to the server via WebSocket. Sends expressions, receives results and streaming `print()` output.

### Why WebSocket over plain HTTP

Lovebird uses HTTP polling for `print()` output. This has visible latency (at least one poll interval) and wastes bandwidth. WebSocket gives us:
- Push-based output streaming (print statements appear instantly)
- Full-duplex communication (send an eval while output is streaming)
- A single persistent connection (no reconnection overhead)
- Native browser API support (no libraries needed)

### Why not just a TCP socket with a custom protocol

A custom TCP protocol would be simpler on the C++ side, but:
- Browser JavaScript cannot open raw TCP sockets (WebSocket is the only option for browser ↔ localhost)
- A plain TCP mode is still useful for non-browser clients (LLMs, editors, scripts). We support both: the same TCP server speaks HTTP/WebSocket for browser connections and a line-based JSON protocol for raw TCP connections, distinguished by the first bytes received.

## Wire protocol

### Message format

JSON-RPC 2.0-inspired, but simplified. Messages are newline-delimited JSON (NDJSON).

**Client → Server:**
```json
{"id": 1, "op": "eval", "code": "return G.clock.gametime()"}
{"id": 2, "op": "eval", "code": "(+ 1 2)", "lang": "fennel"}
{"id": 3, "op": "step"}
{"id": 4, "op": "step", "n": 10}
{"id": 5, "op": "complete", "prefix": "G.graphics.draw"}
{"id": 6, "op": "inspect", "expr": "G"}
```

**Server → Client:**
```json
{"id": 1, "type": "result", "ok": true, "value": "42.31"}
{"id": 1, "type": "output", "text": "hello from print\n"}
{"id": 2, "type": "result", "ok": true, "value": "3"}
{"id": 1, "type": "result", "ok": false, "error": "attempt to index nil value"}
{"id": 3, "type": "result", "ok": true, "value": "stepped 1 frame"}
```

### Operations

| Op | Description | Parameters |
|---|---|---|
| `eval` | Evaluate code in the running Lua VM | `code` (string), `lang` ("lua" or "fennel", default "lua") |
| `step` | Advance N frames then pause | `n` (int, default 1) |
| `inspect` | Deep-print a Lua value (tables, metatables, userdata) | `expr` (string — a Lua expression) |
| `complete` | Tab-completion: list fields matching a prefix | `prefix` (string, e.g. "G.graphics.d") |
| `state` | Return engine state summary (FPS, Lua memory, error status) | — |

The `eval` operation wraps the code in a function. If the code starts with `return`, it is used as-is. Otherwise, the server tries two strategies:
1. First, compile as `return <code>` (so bare expressions like `G.clock.gametime()` return their value).
2. If that fails, compile as `<code>` verbatim (so statements like `x = 10` work).

This matches standard Lua REPL behavior (see the `lua` interactive interpreter source).

### Output capture

During `eval`, `print()` is temporarily redirected to send `output` messages to the requesting client. Multiple `output` messages may arrive before the final `result`. The client reassembles them for display.

### Language detection

If `lang` is not specified, the server attempts auto-detection:
- If the code starts with `(` and doesn't start with `(function` or `(nil`, treat as Fennel.
- Otherwise, treat as Lua.

This heuristic can be overridden with the explicit `lang` parameter.

## Thread model

The game loop runs on the main thread. The REPL server accepts connections on a separate thread. But **Lua evaluation must happen on the main thread** — the Lua VM is not thread-safe, and the `G.*` APIs (OpenGL calls, SDL state, physics) are main-thread-only.

### Execution flow

```
REPL Thread                          Main Thread (game loop)
──────────                           ─────────────────────
Accept connection
Receive {"op":"eval", ...}
  → Push to eval queue ────────────▶ Between update() and draw():
                                       Check eval queue
                                       Pop request
                                       luaL_loadbuffer + lua_pcall
                                       Serialize result
  ◀──────────────────────────────── Push to response queue
Read from response queue
Send {"type":"result", ...}
```

The eval queue is a lock-free SPSC (single-producer, single-consumer) ring buffer, or a simple mutex-guarded queue (we already use `SDL_mutex` for the file watcher).

### Where in the frame to evaluate

REPL expressions execute **after** the game's `update()` and **before** `draw()`. This means:
- Physics has stepped, input is fresh, game state is consistent.
- Drawing commands issued by the REPL (e.g., `G.graphics.draw_rect(...)`) will appear on screen that frame.
- If the REPL expression modifies state (e.g., moves an entity), the change is visible next frame.

The evaluation point is a new step in the game loop:

```
for (;;) {
    // ... existing event processing, hot reload check ...

    while (accum >= kStep) {
        Update(t, kStep);
        t += kStep;
        accum -= kStep;
    }

    ProcessReplQueue();  // ← NEW: evaluate pending REPL expressions

    Render();
}
```

### Frame stepping

The `step` operation pauses the game loop's update/draw cycle and advances exactly N frames, then pauses again. This is implemented with a simple counter:

```cpp
if (repl_stepping_) {
    if (repl_frames_remaining_ > 0) {
        Update(t, kStep);
        --repl_frames_remaining_;
    }
    // Don't advance accum — game is paused.
} else {
    // Normal frame loop.
}
```

The `step` operation sets `repl_stepping_ = true` and `repl_frames_remaining_ = N`. A `resume` operation sets `repl_stepping_ = false`.

## Browser UI

The REPL server also serves a small static HTML page when it receives an HTTP GET on `/`. This page contains:

- A terminal-style input/output area (monospace, dark background, scrollable)
- A single input line at the bottom with a prompt (`> ` for Lua, `λ> ` for Fennel)
- WebSocket connection to `ws://localhost:9741/ws`
- Up/down arrow history navigation (in-memory, session-only)
- Shift+Enter for multiline input
- Tab completion via the `complete` operation
- Syntax highlighting for Lua/Fennel (minimal — keywords and strings only, no library dependency)
- A language toggle (Lua / Fennel)
- A "Step" button that sends `{"op": "step"}` and a "Resume" button
- Live engine status bar (FPS, Lua memory, error state) updated via periodic `state` requests

The entire HTML/CSS/JS is embedded as a string constant in the C++ source (like Lovebird does). No external files. No build step. This means:

```cpp
constexpr const char* kReplHtml = R"html(
<!DOCTYPE html>
<html>
...
</html>
)html";
```

The browser UI is intentionally minimal — it's a development tool, not a product. No framework, no bundler, no npm.

### Why not serve from a file

Embedding the HTML avoids the question of "where does the REPL UI live at runtime?" — no asset path, no file lookup, no packaging concerns. The HTML is compiled into the binary. Changes to the HTML require recompiling the engine, which is acceptable for a development tool that changes rarely. If rapid iteration on the HTML is needed, the server can optionally read from a file path (checked with `access()`) and fall back to the embedded version.

## LLM integration

An LLM like Claude can interact with the engine via the same TCP protocol used by the browser. The LLM (or a tool wrapper around it) opens a TCP connection and sends/receives NDJSON messages. No special LLM-specific protocol is needed.

### MCP tool exposure

For integration with Claude Code or similar MCP-aware tools, an MCP server can wrap the TCP connection:

```json
{
  "name": "game_eval",
  "description": "Evaluate Lua/Fennel code in the running game engine",
  "parameters": {
    "code": { "type": "string", "description": "Lua or Fennel expression to evaluate" },
    "lang": { "type": "string", "enum": ["lua", "fennel"], "default": "lua" }
  }
}
```

This MCP server is a thin wrapper — it opens a TCP connection to `localhost:9741`, sends the eval request, collects output + result messages, and returns them as the tool response. It can be a small Python or Node script.

### What the LLM can do

With REPL access, an LLM can:
- **Explore the API:** `inspect G`, `inspect G.graphics`, `complete G.physics.`
- **Read game state:** `return G.clock.gametime()`, `return player.x, player.y`
- **Modify game state:** `player.x = 100`
- **Test functionality:** "Draw a red rectangle" → `G.graphics.set_color(1,0,0,1); G.graphics.draw_rect(10,10,100,100)`
- **Step through frames:** Send `step`, observe state, send more commands
- **Run test sequences:** Evaluate a series of commands and verify postconditions

The existing `_Docs` table (runtime API documentation) and the LuaLS stub file provide the LLM with a complete API reference. The `inspect` and `complete` operations give it interactive discovery.

## Implementation plan

### Phase 1: TCP server + Lua eval (core)

Add a `ReplServer` class to the engine:

```
src/repl.h      — ReplServer class declaration
src/repl.cc     — TCP server, message parsing, eval queue
```

- TCP server on port 9741 (configurable via conf.json), using POSIX sockets (Linux-only for now).
- Single client connection (simplest model; multiple connections can come later).
- NDJSON message framing.
- `eval` operation only (no step, complete, or inspect yet).
- Eval queue drained in `Game::Run()` between update and draw.
- Redirect `print()` during eval to capture output.
- Enabled only in dev mode (`game run`), disabled in packaged builds.
- Test with `nc localhost 9741` or `socat`.

### Phase 2: Browser UI

- Add WebSocket upgrade handling to the TCP server (RFC 6455 — the handshake is simple, we don't need a library for basic frame encoding).
- Serve the embedded HTML page on HTTP GET `/`.
- Build the terminal-style browser UI.
- Add `state` operation for the live status bar.

### Phase 3: Frame stepping + inspection

- Implement `step` / `resume` operations and the stepping state machine in the game loop.
- Implement `inspect` (recursive table pretty-printing with depth limits, metatable display, userdata type names). We already have `Lua::LogValue` which does most of this.
- Implement `complete` by walking the `G` table and matching field names against the prefix.

### Phase 4: Fennel support

- Auto-detect Fennel code in eval requests.
- Compile Fennel to Lua using the existing `CompileFennelAsset` path, then evaluate the result.
- Display Fennel compilation errors clearly in the browser UI.
- Fennel-aware tab completion (harder — may need to compile a partial form to discover what it produces).

### Phase 5: MCP wrapper

- Write a small Python script that acts as an MCP server (stdio transport).
- Wraps the TCP connection to the game's REPL server.
- Exposes `game_eval`, `game_inspect`, `game_step`, and `game_state` as MCP tools.
- Include the LuaLS stub file content as a resource so the LLM has the full API reference.

## Configuration

In `conf.json`:

```json
{
  "repl": {
    "enabled": true,
    "port": 9741
  }
}
```

`enabled` defaults to `true` in dev mode (`game run`), `false` in packaged mode (`game package` output). The port defaults to 9741. Setting `"enabled": false` disables the REPL entirely — no socket is opened, no thread is started, zero overhead.

## Security considerations

The REPL server binds to `127.0.0.1` only (localhost). It is **not** exposed to the network. There is no authentication — anyone who can connect to `localhost:9741` can execute arbitrary Lua code. This is acceptable for a development tool that only runs during `game run`.

In packaged builds, the REPL is compiled out entirely (behind `#ifdef GAME_DEV_MODE` or similar), so there is no attack surface in released games.

## Alternatives considered

### In-game overlay console

An overlay console rendered inside the game window (like Quake's drop-down console or LoveDebug). This would require:
- A text input system (we don't have one — `SDL_TEXTINPUT` events aren't enough for a cursor, selection, clipboard, etc.)
- A rendering layer that draws on top of the game (we don't have a layer/z-order system yet)
- Focus management (when is input going to the console vs the game?)

All of this is substantial UI work that doesn't exist yet. A browser-based REPL avoids it entirely. The browser already has text input, scrolling, clipboard, selection, and rendering — we get all of it for free.

A future in-game overlay could be built on top of the same eval queue infrastructure.

### Stdio REPL on the terminal

Read from stdin on a background thread, evaluate in the game loop, print results to stdout. This is how Fennel's REPL works with LÖVE2D.

**Pros:** Simplest possible implementation. Works with any terminal. No networking.
**Cons:** Stdout is already used for engine logging (via `SDL_Log`), so REPL output would be interleaved with log spam. The terminal and game window compete for focus on tiling window managers. No rich display (no syntax highlighting, no HTML). Not accessible to LLMs (they'd need to drive a terminal emulator).

This could be a useful fallback for quick debugging, but it shouldn't be the primary interface.

### Embedding a web view in the game window

Use a library like webview or CEF to render the REPL UI inside the game window. This combines "in-game" with "browser-based."

Rejected: massive dependency (CEF is hundreds of MB), complex integration, and we already have a perfectly good browser on the same machine.
