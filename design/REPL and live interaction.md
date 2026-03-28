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

**LOVE2D + Lovebird** — The closest analog to what we want. [Lovebird](https://github.com/rxi/lovebird) runs a tiny HTTP server inside the game process. A browser at `http://127.0.0.1:8000` shows an interactive Lua console. You type expressions, they execute in the game's Lua VM, results display in the browser. All `print()` output streams to the browser in real time. The entire thing is a single Lua file — you add `lovebird.update()` to your game loop.

**Strengths:** Dead simple, browser UI is free (HTML/CSS), decoupled from the game window, no dependencies beyond Lua's socket library.
**Weaknesses:** HTTP request/response is half-duplex — no streaming output, no push notifications. Polling for `print()` output. No structured protocol for tool integration.

**DragonRuby** — C engine with embedded mRuby. Has a built-in console and a `repl.rb` file that is evaluated on save. The critical design choice: all game state flows through an `args` object owned by the engine, not module-level variables. Code is stateless by convention, so reloading code never loses state.

**Strengths:** State preservation by design (the `args` architecture). Gameplay recording/replay against new code.
**Weaknesses:** Requires buying into the `args`-first architecture. mRuby is not standard Ruby.

**Common Lisp (Trial/Kandria)** — The gold standard. The game runs inside SBCL connected to Emacs via SLIME/SWANK. `C-c C-c` compiles a single function and the change goes live instantly — no restart, no reload, the new function replaces the old one in the running image. CLOS even migrates existing instances when class definitions change. Kandria shipped on Steam using this workflow.

**Strengths:** The most powerful live modification capability. Everything is redefiniable. The running image IS the state.
**Weaknesses:** Common Lisp is niche, SBCL binaries are large, Emacs/SLIME learning curve.

**Fennel + LOVE2D** — A separate Lua thread reads stdin and evaluates Fennel expressions in the game context. The `min-love2d-fennel` template includes error recovery — if a reload causes an error, it switches to recovery mode instead of crashing. Fennel's `,reload module-name` command ejects a module from `package.loaded` and re-requires it.

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
{"id": 7, "op": "screenshot"}
{"id": 8, "op": "log_subscribe", "level": "debug", "channel": "all"}
{"id": 9, "op": "log_unsubscribe"}
{"id": 10, "op": "breakpoint", "file": "main.lua", "line": 42}
{"id": 11, "op": "watch", "expr": "player.x", "interval": 5}
{"id": 12, "op": "unwatch", "watch_id": 1}
{"id": 13, "op": "docs", "query": "G.graphics"}
{"id": 14, "op": "hotload"}
{"id": 15, "op": "profile", "frames": 60}
```

**Server → Client:**
```json
{"id": 1, "type": "result", "ok": true, "value": "42.31"}
{"id": 1, "type": "output", "text": "hello from print\n"}
{"id": 2, "type": "result", "ok": true, "value": "3"}
{"id": 1, "type": "result", "ok": false, "error": "attempt to index nil value"}
{"id": 3, "type": "result", "ok": true, "value": "stepped 1 frame"}
{"id": 7, "type": "result", "ok": true, "format": "png", "data": "<base64>"}
{"type": "log", "level": "debug", "channel": "physics", "message": "[physics.cc:42] body awake", "timestamp": 12.345}
{"type": "watch", "watch_id": 1, "frame": 120, "value": "152.3"}
{"type": "error", "message": "Lua error in update(): attempt to call nil"}
{"type": "state_change", "field": "paused", "value": true}
```

### Operations

| Op | Description | Parameters |
|---|---|---|
| `eval` | Evaluate code in the running Lua VM | `code` (string), `lang` ("lua" or "fennel", default "lua"), `timeout` (optional, ms) |
| `step` | Advance N frames then pause | `n` (int, default 1) |
| `resume` | Resume normal execution after stepping | — |
| `pause` | Pause the game loop (freeze updates) | — |
| `inspect` | Deep-print a Lua value (tables, metatables, userdata) | `expr` (string — a Lua expression), `depth` (int, default 3) |
| `complete` | Tab-completion: list fields matching a prefix | `prefix` (string, e.g. "G.graphics.d") |
| `state` | Return engine state summary (FPS, Lua memory, error status) | — |
| `screenshot` | Capture current frame as PNG | `format` ("png" or "raw", default "png") |
| `log_subscribe` | Start streaming engine log messages to this client | `level` (min level, default "info"), `channel` ("all" or specific) |
| `log_unsubscribe` | Stop streaming log messages | — |
| `watch` | Periodically evaluate an expression and push results | `expr` (string), `interval` (int, frames between evals, default 1) |
| `unwatch` | Stop a watch | `watch_id` (int) |
| `docs` | Query the `_Docs` table for API documentation | `query` (string — module or function path) |
| `hotload` | Trigger a hot reload as if files changed | — |
| `profile` | Capture per-frame timing for N frames | `frames` (int, default 60) |
| `files` | List game script files | `pattern` (glob, optional) |
| `read_file` | Read a game script's source code | `path` (string) |
| `write_file` | Write to a game script (triggers hot reload) | `path` (string), `content` (string) |

The `eval` operation wraps the code in a function. If the code starts with `return`, it is used as-is. Otherwise, the server tries two strategies:
1. First, compile as `return <code>` (so bare expressions like `G.clock.gametime()` return their value).
2. If that fails, compile as `<code>` verbatim (so statements like `x = 10` work).

This matches standard Lua REPL behavior (see the `lua` interactive interpreter source).

### Output capture

During `eval`, `print()` is temporarily redirected to send `output` messages to the requesting client. Multiple `output` messages may arrive before the final `result`. The client reassembles them for display.

### Language detection

If `lang` is not specified, the server defaults to Lua. Clients that want Fennel must specify `"lang": "fennel"` explicitly — no auto-detection heuristics. This avoids ambiguity for LLM clients that may generate Lua code starting with `(` (e.g., `(function() ... end)()`).

### Push messages (server-initiated)

Some messages are sent by the server without a client request:

| Type | When | Content |
|---|---|---|
| `log` | When `log_subscribe` is active and a matching log line is emitted | level, channel, message, timestamp |
| `watch` | Every N frames for each active watch | watch_id, frame number, evaluated value |
| `error` | When the Lua VM enters error state (runtime error in update/draw) | error message, traceback |
| `state_change` | When engine state changes (paused, resumed, hot-reloaded, error cleared) | field name, new value |
| `hot_reload` | When hot reload completes | file count, whether scripts changed |

Push messages have no `id` field — they are not responses to requests.

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

The eval queue is a lock-free SPSC (single-producer, single-consumer) ring buffer, or a simple mutex-guarded queue (we already use `std::mutex` for the file watcher).

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

## Cross-platform TCP and WebSocket library

### Requirements

The REPL server needs:
1. **TCP server** — accept connections, read/write bytes, non-blocking or polling
2. **HTTP server** — serve the embedded HTML page on GET `/`
3. **WebSocket server** — upgrade HTTP connections, frame/unframe messages per RFC 6455
4. **Cross-platform** — Linux (primary), macOS, Windows
5. **Small and vendorable** — fits the engine's single-file/small-library pattern
6. **No heavy dependencies** — no OpenSSL, no Boost, no external build system requirements
7. **C or C++ with C-compatible interface** — matches the engine's vendoring pattern

### Library evaluation

#### Option 1: Hand-rolled POSIX/Winsock2 + manual WebSocket

Write a cross-platform socket abstraction (~300 lines for the platform layer) and implement the WebSocket handshake and framing manually.

**WebSocket is simpler than it looks.** The server-side handshake is: read the HTTP upgrade request, extract `Sec-WebSocket-Key`, SHA-1 hash it with the magic GUID, base64-encode, send back the `101 Switching Protocols` response. This is ~80 lines of code. Frame encoding/decoding (RFC 6455 section 5) is another ~150 lines — read opcode + length + mask, XOR the payload, done. We only need text frames (opcode 0x1), close frames (0x8), and ping/pong (0x9/0xA).

| Aspect | Assessment |
|---|---|
| Size | ~500–800 lines total |
| Dependencies | None (OS sockets + SHA-1 for WebSocket) |
| Cross-platform work | Moderate: `select()`/`poll()` on POSIX, `WSAPoll()` on Windows, different error codes, different header includes |
| WebSocket compliance | Partial — sufficient for localhost dev tool, no extensions, no compression |
| Maintenance | All bugs are our bugs |

**SHA-1 requirement**: WebSocket handshake requires SHA-1 (RFC 6455 section 4.2.2). Options:
- Use a public-domain single-file SHA-1 (~100 lines of C, e.g., from [Brad Conte's crypto-algorithms](https://github.com/B-Con/crypto-algorithms) or Steve Reid's implementation)
- Extract from an existing vendored library (SQLite3 has no SHA-1, but Lua doesn't either)
- Use the OS (Linux: `<openssl/sha.h>` if available, but adds a dependency)

Best choice: vendor a ~100-line public-domain SHA-1. It's used exactly once (during WebSocket handshake) and never for security.

#### Option 2: mongoose (cesanta/mongoose)

An embedded web server library. Two files: `mongoose.c` + `mongoose.h`.

| Aspect | Assessment |
|---|---|
| License | **GPLv2** (open source) or commercial (~$2000/yr) |
| Size | ~15,000 lines in mongoose.c |
| Features | TCP, HTTP, WebSocket, MQTT, DNS, TLS (via mbedTLS), JSON |
| Cross-platform | Linux, macOS, Windows, FreeRTOS, Zephyr, bare-metal |
| Dependencies | None (self-contained) |
| API style | Event-driven, single-threaded poll loop — call `mg_mgr_poll()` once per frame |
| Vendoring | Two files, add to CMake, done |

**Strengths:** Complete solution — TCP + HTTP + WebSocket in one library. Event-driven model maps perfectly to the game loop (call `mg_mgr_poll()` from `ProcessReplQueue()`). Battle-tested on embedded systems and the International Space Station. Has built-in JSON parsing. Supports distinguishing raw TCP from HTTP/WebSocket on the same port.

**Weaknesses:** GPLv2 license is the deal-breaker. The engine uses MIT-compatible licenses throughout. A commercial license exists but costs money and adds a licensing dependency. The library is also larger than strictly needed — we don't need MQTT, DNS, multipart form parsing, etc.

**Verdict:** Best technical fit but **license incompatible**. Only viable if the engine goes GPL or buys a commercial license.

#### Option 3: cpp-httplib

Single-header C++ HTTP/WebSocket library.

| Aspect | Assessment |
|---|---|
| License | MIT |
| Size | Single header, ~10,000 lines |
| Features | HTTP server/client, WebSocket (recent addition), multipart, ranges |
| Cross-platform | Linux, macOS, Windows |
| Dependencies | Optional OpenSSL for HTTPS |
| Threading model | **Blocking** — one thread per connection |

**Weaknesses:** Blocking I/O model requires spawning a thread per WebSocket connection. The WebSocket support is relatively new and less battle-tested than the HTTP side. C++-only (uses `std::string`, `std::thread`, `std::function` heavily). The blocking model means we'd need a separate thread reading the WebSocket and forwarding to the eval queue, which is what we'd do anyway, but the library's internal threading adds unnecessary complexity.

**Verdict:** Usable but awkward. The blocking model fights the engine's polling architecture.

#### Option 4: libwebsockets

Production-grade WebSocket library in pure C.

| Aspect | Assessment |
|---|---|
| License | MIT + static linking exception |
| Size | ~60,000 lines across many files |
| Features | HTTP/1.1, HTTP/2, WebSocket, event loop, TLS (mbedTLS/OpenSSL) |
| Cross-platform | Linux, macOS, Windows, FreeBSD, ESP32 |
| Dependencies | CMake build system; optional mbedTLS or OpenSSL |
| API style | Event-driven callbacks, poll-based |

**Key feature:** `LWS_SERVER_OPTION_FALLBACK_TO_RAW` — accepts both WebSocket and raw TCP on the same port, distinguished automatically by the first bytes received. This is exactly what we need for dual-mode (browser WebSocket + CLI raw TCP).

**Weaknesses:** The library is large (~60K lines) and has a complex build system with many configuration options. Overkill for a localhost dev tool. The callback-heavy API is hard to debug. Vendoring would add significant bulk to `libraries/`.

**Verdict:** Technically excellent but too heavy. We don't need HTTP/2, TLS, or compression for a localhost REPL.

#### Option 5: wslay

Lightweight WebSocket library in pure C. I/O-agnostic — you provide the socket read/write callbacks.

| Aspect | Assessment |
|---|---|
| License | MIT |
| Size | ~3,000 lines |
| Features | WebSocket frame encode/decode only — no HTTP, no TCP, no event loop |
| Cross-platform | Portable C (no platform-specific code) |
| Dependencies | None |

**Strengths:** Does exactly one thing (WebSocket framing) and does it well. I/O-agnostic design means it works with any socket implementation — raw POSIX, Winsock, anything. No opinions about threading or event loops.

**Weaknesses:** Does not handle the HTTP upgrade handshake — you must parse the HTTP request and send the 101 response yourself. Does not provide TCP server functionality. Must be combined with a socket layer.

**Verdict:** Good WebSocket layer to combine with hand-rolled TCP. The total code (platform sockets + wslay + HTTP handshake) would be ~1500 lines of library code.

### Recommendation: Hand-rolled sockets + minimal WebSocket

For a localhost-only dev tool, the simplest correct approach is best:

1. **Platform socket abstraction** (~200 lines) — `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()`, `close()`, `poll()` wrapped in a cross-platform API. Use `poll()` on POSIX (available on Linux, macOS, and modern BSDs) and `WSAPoll()` on Windows. Both have identical semantics.

2. **Minimal HTTP parser** (~100 lines) — We only need to parse `GET / HTTP/1.1` and the WebSocket upgrade headers. This is string matching, not a general HTTP parser.

3. **WebSocket handshake + framing** (~200 lines) — SHA-1 + base64 for the handshake, frame encode/decode for messages. Server-to-client frames are unmasked (per RFC 6455). Client-to-server frames are masked (browser requirement) — we XOR with the 4-byte mask.

4. **JSON parsing** — Use the engine's existing `FixedStringBuffer` for building JSON responses. For parsing incoming JSON, a minimal recursive-descent parser (~200 lines) suffices for our restricted message format (flat objects with string/number/boolean values only). Alternatively, vendor a single-header JSON library.

Total: ~700–1000 lines of new code, zero external dependencies, complete control.

**Why not use a library:** Every candidate either has license issues (mongoose), is too heavy (libwebsockets), uses the wrong threading model (cpp-httplib), or only solves half the problem (wslay). The complete problem — TCP + HTTP + WebSocket for localhost — is small enough to implement directly, and the result is simpler to maintain than any library integration.

### JSON parsing

The engine already has a complete JSON parser in `src/json.h` / `src/json.cc`. It supports all JSON types (objects, arrays, strings, numbers, booleans, null), handles escape sequences including `\uXXXX` Unicode, and uses the engine's allocator interface:

```cpp
ErrorOr<JsonValue*> ParseJson(std::string_view input, Allocator* allocator);
```

The parser returns `ErrorOr`, so malformed messages from clients are handled cleanly. String values are zero-copy (pointing into the input buffer) when no escape sequences are present. Object lookup is via `operator[]` which returns a null sentinel for missing keys — no crashes on unexpected messages. This is exactly what we need for parsing incoming NDJSON requests:

```cpp
auto result = ParseJson(line, allocator);
if (result.is_error()) { /* send error response */ return; }
const auto& msg = *result.value();
int id = static_cast<int>(msg["id"].GetNumber());
std::string_view op = msg["op"].GetString();
std::string_view code = msg["code"].GetString();  // returns "" if missing
```

For building JSON responses, the engine's `FixedStringBuffer` is sufficient. REPL responses are simple flat objects:

```cpp
FixedStringBuffer<8192> buf;
buf.Append("{\"id\":", request_id, ",\"type\":\"result\",\"ok\":true,\"value\":\"");
buf.Append(EscapeJsonString(result));
buf.Append("\"}\n");
```

A small `EscapeJsonString` helper (~30 lines) handles quoting `"`, `\`, newlines, and control characters in result values. No new dependencies needed.

### Platform socket details

```cpp
// src/repl_socket.h — Cross-platform socket abstraction

namespace G {

// Type aliases to paper over POSIX vs Winsock differences.
#ifdef _WIN32
using SocketHandle = SOCKET;           // UINT_PTR
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;              // file descriptor
constexpr SocketHandle kInvalidSocket = -1;
#endif

struct Socket {
    SocketHandle fd = kInvalidSocket;

    bool IsValid() const { return fd != kInvalidSocket; }
    void Close();
};

// Create a non-blocking TCP server socket bound to 127.0.0.1:port.
Socket CreateListenSocket(uint16_t port);

// Accept a pending connection (non-blocking, returns invalid if none).
Socket AcceptConnection(Socket listen_socket);

// Poll a set of sockets for readability. Returns number ready.
// Uses poll() on POSIX, WSAPoll() on Windows.
int PollSockets(Socket* sockets, size_t count, int timeout_ms);

// Non-blocking read. Returns bytes read, 0 = closed, -1 = would-block.
int SocketRecv(Socket s, void* buf, size_t len);

// Non-blocking write. Returns bytes written, -1 = would-block or error.
int SocketSend(Socket s, const void* buf, size_t len);

// Platform init (Winsock2 WSAStartup on Windows, no-op on POSIX).
void InitSockets();
void ShutdownSockets();

}  // namespace G
```

**Windows-specific concerns:**
- `WSAStartup(MAKEWORD(2, 2), &wsaData)` must be called before any socket operation. Call from `ReplServer::Init()`.
- `closesocket()` instead of `close()`.
- `WSAGetLastError()` instead of `errno`. `WSAEWOULDBLOCK` instead of `EAGAIN`/`EWOULDBLOCK`.
- `ioctlsocket(fd, FIONBIO, &mode)` to set non-blocking, vs `fcntl(fd, F_SETFL, O_NONBLOCK)`.
- Link against `ws2_32.lib` in CMake.

**macOS-specific concerns:**
- `SO_NOSIGPIPE` socket option to prevent `SIGPIPE` on broken connections (Linux uses `MSG_NOSIGNAL` flag on `send()` instead).
- `poll()` works identically to Linux.

All platform differences are isolated in `repl_socket.cc` behind the `Socket` API. The rest of the REPL code is platform-agnostic.

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

### Design for LLM ergonomics

The protocol is deliberately LLM-friendly:

1. **NDJSON** — LLMs are fluent in JSON. No binary protocols, no custom framing. Every message is a self-contained JSON object on one line.
2. **Plain TCP mode** — An LLM tool can use a raw TCP socket (no WebSocket overhead). The server auto-detects: if the first bytes aren't `GET ` or a WebSocket upgrade, it's raw TCP/NDJSON.
3. **Request-response with IDs** — The `id` field lets the LLM match responses to requests. This works naturally with tool-use patterns where each tool call expects a result.
4. **Rich introspection** — `inspect`, `complete`, `docs`, and `state` give the LLM everything it needs to explore the engine without prior knowledge.
5. **Streaming output** — `output` messages arrive before the `result`, so the LLM sees `print()` output interleaved with execution.
6. **Error recovery** — If eval fails, the LLM gets a structured error message (`"ok": false, "error": "..."`) and can retry or adjust.

### What an LLM can do with REPL access

**Explore and understand the game:**
```
→ {"id":1, "op":"docs", "query":"G.graphics"}
← {"id":1, "type":"result", "ok":true, "value":"draw_sprite(sprite, x, y) — Draws a sprite..."}

→ {"id":2, "op":"inspect", "expr":"G"}
← {"id":2, "type":"result", "ok":true, "value":"table: graphics, physics, sound, input, ..."}

→ {"id":3, "op":"complete", "prefix":"G.physics."}
← {"id":3, "type":"result", "ok":true, "value":["add_box","add_circle","destroy_handle",...]}
```

**Read and modify game state:**
```
→ {"id":4, "op":"eval", "code":"return player.x, player.y"}
← {"id":4, "type":"result", "ok":true, "value":"152.3\t84.7"}

→ {"id":5, "op":"eval", "code":"player.x = 200"}
← {"id":5, "type":"result", "ok":true, "value":""}
```

**Step through execution and observe:**
```
→ {"id":6, "op":"pause"}
← {"id":6, "type":"result", "ok":true}

→ {"id":7, "op":"eval", "code":"return G.physics.position(enemy_body)"}
← {"id":7, "type":"result", "ok":true, "value":"300.0\t150.0"}

→ {"id":8, "op":"step", "n":10}
← {"id":8, "type":"result", "ok":true, "value":"stepped 10 frames"}

→ {"id":9, "op":"eval", "code":"return G.physics.position(enemy_body)"}
← {"id":9, "type":"result", "ok":true, "value":"305.2\t148.1"}
```

**Test and iterate on game scripts:**
```
→ {"id":10, "op":"read_file", "path":"main.lua"}
← {"id":10, "type":"result", "ok":true, "value":"local _Game = {}\nfunction _Game:init()..."}

→ {"id":11, "op":"write_file", "path":"main.lua", "content":"-- modified script..."}
← {"id":11, "type":"result", "ok":true}
← {"type":"hot_reload", "file_count":1, "has_script_changes":true}
```

**Monitor with watches and log streaming:**
```
→ {"id":12, "op":"watch", "expr":"player.health", "interval":60}
← {"id":12, "type":"result", "ok":true, "watch_id":1}
← {"type":"watch", "watch_id":1, "frame":60, "value":"100"}
← {"type":"watch", "watch_id":1, "frame":120, "value":"85"}

→ {"id":13, "op":"log_subscribe", "level":"debug", "channel":"physics"}
← {"id":13, "type":"result", "ok":true}
← {"type":"log", "level":"debug", "channel":"physics", "message":"...", "timestamp":1.234}
```

**Screenshot for visual verification:**
```
→ {"id":14, "op":"screenshot"}
← {"id":14, "type":"result", "ok":true, "format":"png", "data":"iVBORw0KGgo..."}
```

### MCP server wrapper

For integration with Claude Code or similar MCP-aware tools, a thin MCP server wraps the TCP connection. This is a separate process (Python or TypeScript) that:

1. Connects to `localhost:9741` via raw TCP
2. Exposes MCP tools that map 1:1 to protocol operations
3. Includes the LuaLS stub file as an MCP resource so the LLM has the full API reference
4. Handles connection lifecycle (reconnect if game restarts)

```python
# mcp_game_server.py — MCP server wrapping the REPL TCP connection
# Runs as: python mcp_game_server.py (stdio transport)

import json
import socket
from mcp.server import Server

class GameConnection:
    """Persistent TCP connection to the game's REPL server."""

    def __init__(self, host="127.0.0.1", port=9741):
        self.host = host
        self.port = port
        self.sock = None
        self.request_id = 0
        self.buffer = b""

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))

    def send_request(self, op, **params):
        self.request_id += 1
        msg = {"id": self.request_id, "op": op, **params}
        self.sock.sendall((json.dumps(msg) + "\n").encode())
        return self._collect_response(self.request_id)

    def _collect_response(self, request_id):
        """Read lines until we get the result for this request ID."""
        outputs = []
        while True:
            line = self._read_line()
            msg = json.loads(line)
            if msg.get("id") == request_id:
                if msg.get("type") == "output":
                    outputs.append(msg["text"])
                elif msg.get("type") == "result":
                    return {
                        "output": "".join(outputs),
                        "ok": msg["ok"],
                        "value": msg.get("value", ""),
                        "error": msg.get("error", ""),
                    }

    def _read_line(self):
        while b"\n" not in self.buffer:
            data = self.sock.recv(4096)
            if not data:
                raise ConnectionError("Game disconnected")
            self.buffer += data
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode()

server = Server("game-engine")
game = GameConnection()

@server.tool("game_eval")
def game_eval(code: str, lang: str = "lua") -> str:
    """Evaluate Lua or Fennel code in the running game engine.
    Returns the result of the expression, or any error message.
    Use 'return expr' to get values back. Statements without return
    execute but return empty string."""
    result = game.send_request("eval", code=code, lang=lang)
    parts = []
    if result["output"]:
        parts.append(f"Output:\n{result['output']}")
    if result["ok"]:
        parts.append(f"Result: {result['value']}")
    else:
        parts.append(f"Error: {result['error']}")
    return "\n".join(parts)

@server.tool("game_inspect")
def game_inspect(expr: str, depth: int = 3) -> str:
    """Deep-inspect a Lua value. Shows table contents, metatables,
    userdata types. Use 'G' to see all engine modules, 'G.graphics'
    to see graphics functions, etc."""
    result = game.send_request("inspect", expr=expr, depth=depth)
    return result["value"] if result["ok"] else f"Error: {result['error']}"

@server.tool("game_complete")
def game_complete(prefix: str) -> str:
    """Tab-complete a Lua expression. Returns matching field names.
    Example: prefix='G.graphics.draw' returns all draw_* functions."""
    result = game.send_request("complete", prefix=prefix)
    return result["value"] if result["ok"] else f"Error: {result['error']}"

@server.tool("game_state")
def game_state() -> str:
    """Get engine state: FPS, Lua memory usage, error status,
    paused/running, frame count."""
    result = game.send_request("state")
    return result["value"] if result["ok"] else f"Error: {result['error']}"

@server.tool("game_step")
def game_step(n: int = 1) -> str:
    """Advance the game by exactly N frames, then pause.
    Useful for observing state changes frame-by-frame."""
    result = game.send_request("step", n=n)
    return result["value"] if result["ok"] else f"Error: {result['error']}"

@server.tool("game_screenshot")
def game_screenshot() -> str:
    """Capture the current frame as a base64-encoded PNG image."""
    result = game.send_request("screenshot")
    if result["ok"]:
        return f"data:image/png;base64,{result['value']}"
    return f"Error: {result['error']}"

@server.tool("game_docs")
def game_docs(query: str) -> str:
    """Query the engine's API documentation. Use 'G' for top-level modules,
    'G.graphics' for graphics functions, 'G.graphics.draw_sprite' for
    a specific function's signature and docs."""
    result = game.send_request("docs", query=query)
    return result["value"] if result["ok"] else f"Error: {result['error']}"

@server.tool("game_read_file")
def game_read_file(path: str) -> str:
    """Read a game script file's source code."""
    result = game.send_request("read_file", path=path)
    return result["value"] if result["ok"] else f"Error: {result['error']}"

@server.tool("game_write_file")
def game_write_file(path: str, content: str) -> str:
    """Write to a game script file. This triggers a hot reload automatically."""
    result = game.send_request("write_file", path=path, content=content)
    return result["value"] if result["ok"] else f"Error: {result['error']}"

@server.resource("game://api-reference")
def api_reference() -> str:
    """The complete Lua API reference for the game engine (LuaLS stub file).
    Read this to understand all available functions before writing code."""
    with open("definitions/game.lua") as f:
        return f.read()

if __name__ == "__main__":
    game.connect()
    server.run()
```

### Claude Code integration

Add to `.claude/settings.json`:

```json
{
  "mcpServers": {
    "game-engine": {
      "command": "python",
      "args": ["mcp_game_server.py"],
      "cwd": "/path/to/game/project"
    }
  }
}
```

This gives Claude Code direct access to the running game. When asked to "make the player jump higher", Claude can:
1. Read the API docs via `game_docs("G.physics")`
2. Inspect current state via `game_eval("return player.jump_force")`
3. Modify the script file via `game_write_file`
4. Verify the change via `game_step(60)` + `game_screenshot()`

## Interaction with hot reload

The REPL server's C++ state (TCP connections, WebSocket frames, eval queue) **survives hot reload**. This follows the same pattern as the physics world and renderer — C++ module state is untouched during reload.

When a hot reload triggers:
1. The REPL server continues accepting connections and receiving messages (the REPL thread is independent of the reload).
2. Messages received during reload are queued normally.
3. After reload completes, `ProcessReplQueue()` drains the queue as usual.
4. The server sends a `{"type": "hot_reload", ...}` push message to all connected clients.

**REPL evaluation does NOT trigger hot reload.** Evaluating `player.x = 100` modifies live state without reloading scripts. This is the core difference from the file-watcher workflow.

**The `hotload` operation explicitly triggers reload** — useful for LLMs that modify script files via `write_file` and want to control when the reload happens (rather than waiting for the file watcher's 10ms poll interval).

## Interaction with the allocation model

The REPL server needs a general-purpose allocator for:
- Connection buffers (read/write buffers per client, resizable)
- Eval queue entries (variable-size code strings)
- Response queue entries (variable-size result strings)
- JSON parse state (temporary, per-message)

The eval queue entries and response strings have varied lifetimes (allocated when a message arrives, freed when the response is sent), so the main `ArenaAllocator` (LIFO-only) won't work.

**Approach: use `MimallocAllocator`**, following the same pattern as Lua and the proposed networking module:

```cpp
// In EngineModules constructor:
MimallocAllocator repl_allocator(
    allocator->Alloc(Megabytes(2), kMaxAlign),
    Megabytes(2));
```

2 MB is generous — the REPL processes one message at a time, and messages are small (a few KB at most). The largest allocation would be a `screenshot` response (~500 KB for a base64-encoded PNG of a 1920x1080 frame), which is still well within budget.

## Implementation plan

### Phase 1: TCP server + raw NDJSON eval

**Files:**
```
src/repl.h           — ReplServer class declaration
src/repl.cc          — TCP server, message parsing, eval queue
src/repl_socket.h    — Cross-platform socket abstraction (types + API)
src/repl_socket.cc   — Platform-specific socket implementation
```

**Tasks:**

1. **Socket abstraction** (`repl_socket.h/.cc`)
   - Implement `CreateListenSocket`, `AcceptConnection`, `PollSockets`, `SocketRecv`, `SocketSend`, `Close` for POSIX.
   - Add `#ifdef _WIN32` paths for Winsock2 (can be stubbed initially since Linux is primary).
   - Set `SO_REUSEADDR` to allow quick restarts.
   - Set non-blocking mode on all sockets.
   - Bind to `127.0.0.1` only.

2. **ReplServer class** (`repl.h/.cc`)
   ```cpp
   class ReplServer {
   public:
       // Initialize server on the given port. Does not start listening yet.
       void Init(uint16_t port, Allocator* allocator);

       // Start the listener thread. Called from EngineModules::Initialize().
       void Start();

       // Stop the listener thread and close all connections.
       void Stop();

       // Drain the eval queue — called from the main thread each frame.
       // Evaluates pending requests against the given Lua state.
       void ProcessQueue(lua_State* state);

   private:
       // Runs on the REPL thread.
       void ListenerLoop();

       // Parse an NDJSON message and enqueue it.
       void HandleMessage(std::string_view line, int client_index);

       Socket listen_socket_;
       std::thread listener_thread_;
       std::atomic<bool> running_{false};

       // Eval queue: REPL thread produces, main thread consumes.
       struct EvalRequest {
           int client_index;
           int request_id;
           FixedStringBuffer<8192> code;
           // ... lang, op type
       };

       std::mutex queue_mutex_;
       CircularBuffer<EvalRequest> eval_queue_;

       // Response queue: main thread produces, REPL thread consumes.
       struct EvalResponse {
           int client_index;
           FixedStringBuffer<8192> json;
       };

       std::mutex response_mutex_;
       CircularBuffer<EvalResponse> response_queue_;
   };
   ```

3. **Eval execution** (in `ProcessQueue`)
   - Pop requests from eval queue.
   - For `eval` operation:
     - Try `luaL_loadbuffer` with `"return " + code`.
     - If that fails, try `luaL_loadbuffer` with `code` as-is.
     - If compilation succeeds, `lua_pcall`.
     - Capture result via `Lua::LogValue` or `lua_tostring`.
     - Capture errors from `lua_pcall` return value.
   - Redirect `print()` during eval:
     ```cpp
     // Before eval:
     lua_getglobal(state, "print");  // save original
     lua_pushcfunction(state, repl_print_capture);
     lua_setglobal(state, "print");
     // ... eval ...
     // After eval:
     lua_setglobal(state, "print");  // restore original
     ```
   - Build NDJSON response and push to response queue.

4. **Integration into game loop** (`game.cc`)
   - Add `ReplServer repl_server` to `EngineModules`.
   - Call `repl_server.Init(config.repl_port, allocator)` in `EngineModules::Initialize()`.
   - Call `repl_server.Start()` after initialization.
   - Insert `repl_server.ProcessQueue(lua_state)` in `Game::Run()` between Update and Render.
   - Call `repl_server.Stop()` in `EngineModules::Deinitialize()`.

5. **CMake changes**
   - Add `src/repl.cc` and `src/repl_socket.cc` to `ENGINE_SRCS`.
   - On Windows: link `ws2_32`.
   - Guard behind `GAME_DEV_MODE` compile definition.

6. **Testing**
   - Test with `echo '{"id":1,"op":"eval","code":"return 1+1"}' | nc localhost 9741`.
   - Test with `socat` for persistent connection.
   - Add a basic GoogleTest that starts the server, connects, sends eval, checks response.

### Phase 2: WebSocket + browser UI

**Tasks:**

1. **WebSocket upgrade handling**
   - Detect HTTP requests by checking if first bytes are `GET `.
   - Parse `Sec-WebSocket-Key` header.
   - Vendor a ~100-line public-domain SHA-1 implementation.
   - Implement base64 encoding (~50 lines).
   - Send `101 Switching Protocols` response with `Sec-WebSocket-Accept`.
   - Mark client as WebSocket mode (vs raw TCP mode).

2. **WebSocket frame encode/decode**
   - Decode incoming frames: read opcode, payload length (7-bit, 16-bit, or 64-bit), mask, XOR payload.
   - Handle text frames (opcode 0x1), close frames (0x8), ping (0x9) → reply pong (0xA).
   - Encode outgoing text frames (server frames are unmasked).
   - Handle frame fragmentation for messages > 125 bytes (unlikely for REPL but technically required).

3. **HTTP static file serving**
   - On `GET /` with no WebSocket upgrade: send `200 OK` with embedded HTML.
   - On `GET /ws` with WebSocket upgrade: perform upgrade.
   - All other paths: `404 Not Found`.

4. **Embedded browser UI**
   - Single `constexpr const char* kReplHtml` with the full HTML/CSS/JS.
   - Terminal-style output area with auto-scroll.
   - Input line with prompt, history (up/down arrows), multiline (Shift+Enter).
   - WebSocket connection with auto-reconnect.
   - Syntax highlighting for Lua keywords (minimal regex-based coloring).
   - Step/Resume buttons and FPS/memory status bar.

5. **`state` operation**
   - Return JSON with: `fps`, `lua_memory_kb`, `frame_count`, `has_error`, `error_message`, `paused`, `game_time`.
   - Browser UI polls this every 500ms for the status bar.

### Phase 3: Frame stepping + inspection + completion

**Tasks:**

1. **Frame stepping state machine**
   - Add `repl_paused_` and `repl_frames_remaining_` to `Game`.
   - `pause` operation: set `repl_paused_ = true`.
   - `resume` operation: set `repl_paused_ = false`.
   - `step` operation: set `repl_paused_ = true`, `repl_frames_remaining_ = n`.
   - In `Game::Run()`, when paused: skip the `while (accum >= kStep)` Update loop. Still poll events and render (so the window stays responsive). Still process REPL queue (so the LLM can send more commands while paused).
   - When `repl_frames_remaining_ > 0`: run exactly one Update, decrement counter.

2. **`inspect` operation**
   - Evaluate `expr` to get the value on the Lua stack.
   - Call a recursive pretty-printer (extend `Lua::LogValue`):
     - Tables: list keys and values up to `depth` levels. Show `{...}` for deeper nesting.
     - Metatables: show `[metatable: ...]` if present.
     - Userdata: show the metatable name (e.g., `[fvec2: x=1.5, y=2.3]`).
     - Functions: show `[function: source:line]` using `lua_getinfo`.
     - Strings: quote and truncate at 200 chars.
     - Limit total output to 8 KB.

3. **`complete` operation**
   - Parse the prefix into table path + partial name (e.g., `"G.graphics.d"` → path=`["G","graphics"]`, partial=`"d"`).
   - Walk the Lua table path: `lua_getglobal("G")`, `lua_getfield(-1, "graphics")`.
   - Iterate the final table with `lua_next()`, collecting keys that start with the partial.
   - Also check the metatable's `__index` table if present (for userdata method completion).
   - Return sorted list of matches.

4. **`docs` operation**
   - Access the `_Docs` global table (populated by `AddLibraryWithMetadata`).
   - Walk the path (e.g., `"G.graphics.draw_sprite"`) through `_Docs`.
   - Format the docstring, argument names/types, and return types into a readable string.
   - For modules, list all functions with one-line summaries.

### Phase 4: Fennel support

**Tasks:**

1. **Auto-detect Fennel code** — heuristic from the protocol section.
2. **Compile via existing path** — call `Lua::CompileFennelAsset` with the REPL code as input.
3. **Error formatting** — Fennel compilation errors reference the synthetic `"repl"` filename, not a real file. Format them cleanly.
4. **Fennel-specific completion** — Fennel macros and special forms aren't Lua tables. For now, fall back to Lua-level completion (which works for `G.*` API calls but not for Fennel builtins like `let`, `fn`, `match`).

### Phase 5: Watches, log streaming, screenshots

**Tasks:**

1. **`watch` / `unwatch`**
   - Maintain a list of active watches (max 16).
   - Each frame (or every N frames per the `interval`), evaluate the watch expression and push a `watch` message to connected clients.
   - Watches execute in `ProcessReplQueue` alongside normal eval requests.

2. **`log_subscribe` / `log_unsubscribe`**
   - Add a second log sink that captures messages for REPL clients.
   - Use the existing `SetLogSink` mechanism — replace the SDL sink with a multiplexing sink that logs to both SDL and the REPL client.
   - Filter by level and channel per the subscription parameters.
   - Push `log` messages to subscribed clients.

3. **`screenshot`**
   - Call `glReadPixels` to capture the framebuffer (already done for F12 screenshots).
   - Encode as PNG using `stb_image_write` (already vendored).
   - Base64-encode the PNG data.
   - Return as the result value.
   - For raw TCP clients: return base64-encoded PNG in the `data` field.
   - For WebSocket clients: same (could use binary frames, but base64 in JSON is simpler).

4. **`profile` operation**
   - Record per-frame timing for N frames using the existing `TIMER` / profiler infrastructure.
   - Return a summary: min/max/avg frame time, per-subsystem breakdown (Update, Physics, Lua, Render).

### Phase 6: File operations + MCP server

**Tasks:**

1. **`files` operation** — list scripts in the asset database, optionally filtered by glob pattern.
2. **`read_file` operation** — read a script file's source from the asset database or filesystem.
3. **`write_file` operation** — write to a script file on disk. Triggers hot reload via `RequestHotload()`.
4. **MCP server** (`scripts/mcp_game_server.py`) — the Python script from the design above. Minimal dependencies: just the `mcp` Python package.
5. **Claude Code configuration** — document how to add the MCP server to `.claude/settings.json`.

### Phase 7: Polish and robustness

**Tasks:**

1. **Multiple simultaneous clients** — the server already supports this architecturally (each client has an index). Test with browser + LLM connected simultaneously.
2. **Connection cleanup** — detect broken connections (send fails, recv returns 0), clean up client state, remove watches.
3. **Timeout for eval** — if a REPL expression runs for more than 100ms (infinite loop), abort it via `lua_sethook` with an instruction count limit.
4. **Message size limits** — reject messages larger than 64 KB (prevent OOM from malicious input, even on localhost).
5. **Graceful shutdown** — when the game exits, send close frames to WebSocket clients, close all sockets, join the listener thread.
6. **Reconnection in browser UI** — if the WebSocket disconnects (game restarted), show a "Reconnecting..." indicator and retry every second.
7. **Error recovery** — if the Lua VM is in error state (from a crash in `update()`), REPL eval should still work. This allows the LLM to diagnose and fix errors in a crashed game. The eval queue processes even when `HasError()` is true.

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

Read from stdin on a background thread, evaluate in the game loop, print results to stdout. This is how Fennel's REPL works with LOVE2D.

**Pros:** Simplest possible implementation. Works with any terminal. No networking.
**Cons:** Stdout is already used for engine logging (via `SDL_Log`), so REPL output would be interleaved with log spam. The terminal and game window compete for focus on tiling window managers. No rich display (no syntax highlighting, no HTML). Not accessible to LLMs (they'd need to drive a terminal emulator).

This could be a useful fallback for quick debugging, but it shouldn't be the primary interface.

### Embedding a web view in the game window

Use a library like webview or CEF to render the REPL UI inside the game window. This combines "in-game" with "browser-based."

Rejected: massive dependency (CEF is hundreds of MB), complex integration, and we already have a perfectly good browser on the same machine.
