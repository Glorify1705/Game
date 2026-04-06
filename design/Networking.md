---
status: in-design
tags: [networking, multiplayer]
---

# Networking Support

**Status: Under consideration.**

## Overview

This document evaluates options for adding networking support to the engine. The engine currently has no networking code — no socket abstractions, no message protocols, no multiplayer infrastructure. This is a greenfield design.

The goal is to choose a transport library that fits the engine's philosophy (small, vendorable, C-compatible, minimal dependencies) and design a Lua API that supports both client-server and peer-to-peer architectures.

## Motivation

- **Multiplayer games**: Real-time multiplayer (co-op, competitive, party games) requires reliable UDP transport with connection management.
- **Online features**: Leaderboards, matchmaking lobbies, chat, and other online services need basic networking.
- **Development tools**: Remote debugging, live-reload across machines, and remote console access are all networking use cases.

## Constraints

The engine has specific characteristics that constrain the choice of networking library:

| Constraint | Implication |
|---|---|
| C++ engine with C-style vendored libraries | Library must be C or have a C API. Pure C++ libraries (yojimbo, SLikeNet) are less desirable. |
| All dependencies vendored in `libraries/` | Library must be small enough to vendor. Heavy dependency chains (protobuf, OpenSSL) are disqualifying. |
| Lua scripting layer | The networking API will be exposed to Lua via `G.network.*`. The library's API must map cleanly to Lua bindings. |
| Cross-platform (Linux primary, Windows secondary) | Library must support at least Linux and Windows. |
| Small engine, small team | Complexity budget is limited. The library should solve transport-level problems so we don't have to. |
| Single-file or small-footprint preference | The engine vendors stb headers, sqlite3, glad, etc. Libraries that follow this pattern integrate cleanly. |

## Library Evaluation

### ENet

**Repository**: [github.com/lsalzman/enet](https://github.com/lsalzman/enet) | **License**: MIT

ENet is a thin reliable UDP library written in pure C. It is the de facto standard for C game networking.

**Properties:**
- ~14 source files, ~4,000-5,000 lines of code
- Zero dependencies (uses OS sockets directly)
- Reliable and unreliable packet delivery
- Multiple independent channels per connection (one channel stalling does not block others)
- Packet fragmentation and reassembly
- Bandwidth throttling and flow control
- Connection management with timeouts
- Supports both client-server and P2P topologies (a host can be both client and server simultaneously)

**What it does NOT provide:**
- Encryption (by design — transport only)
- NAT punchthrough (can be implemented manually with a rendezvous server, but not built-in)
- Authentication
- Serialization

**Maintenance:** Mature and stable. Last release v1.3.18 (April 2024). ~330 commits, 3.2k stars. The library is essentially "done" — infrequent updates reflect stability, not abandonment.

**Notable fork**: [zpl-c/enet](https://github.com/zpl-c/enet) — a maintained fork (v2.6.5, June 2025) that adds IPv6 support, monotonic time, and a **single-header option** (`enet.h` with `ENET_IMPLEMENTATION` define). This matches the engine's stb-style vendoring pattern exactly.

### GameNetworkingSockets (Valve)

**Repository**: [github.com/ValveSoftware/GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) | **License**: BSD 3-Clause

Valve's production networking library with a C API, used across Steam.

**Properties:**
- Large C++ codebase with a flat C API (`ISteamNetworkingSockets`)
- AES-GCM-256 encryption, Curve25519 key exchange (built-in, mandatory)
- NAT punchthrough via WebRTC ICE
- P2P with pluggable signaling
- Connection quality diagnostics

**Dependencies**: OpenSSL, Google Protobuf, ed25519-donna/curve25519-donna. Requires CMake + Ninja.

**Assessment: Too heavy.** The dependency chain alone (protobuf, OpenSSL) would be the largest addition to the project by far. Building it is nontrivial. The library solves problems (encryption, relay infrastructure, Steam integration) that are not priorities for this engine. The open-source version does not include Valve's SDR relay network — that is Steam-only infrastructure.

### netcode.io

**Repository**: [github.com/mas-bandwidth/netcode](https://github.com/mas-bandwidth/netcode) | **License**: BSD 3-Clause

A secure connection protocol by Glenn Fiedler (author of Gaffer On Games). Pure C.

**Properties:**
- ~2 core files (`netcode.c`, `netcode.h`)
- Encrypted and signed UDP packets via libsodium
- Connect token authentication (prevents DDoS amplification)
- Client slot management
- Replay attack protection

**What it does NOT provide:**
- Reliable messaging (this is a connection/encryption layer only)
- NAT punchthrough
- P2P support

**Dependencies**: libsodium (bundled in the repo).

**Assessment:** Solves a narrower problem than ENet — you would need to layer Glenn Fiedler's `reliable` library on top for reliability, and `serialize` for serialization, reconstructing what ENet provides in one package. The advantage is built-in encryption, which matters for competitive games with dedicated servers. For an indie engine where encryption is not a priority, this adds complexity without clear benefit.

### yojimbo

**Repository**: [github.com/mas-bandwidth/yojimbo](https://github.com/mas-bandwidth/yojimbo) | **License**: BSD 3-Clause

A higher-level networking library built on netcode + reliable + serialize + libsodium.

**Properties:**
- Client-server only (no P2P, by explicit design choice)
- Encrypted connections, reliable/unreliable messages, serialization framework
- C++ API

**Assessment: Wrong fit.** C++ API does not match the engine's C-style integration pattern. Client-server only. Depends on libsodium. Opinionated toward competitive FPS-style games with dedicated servers.

### SLikeNet (RakNet successor)

**Repository**: [github.com/SLikeSoft/SLikeNet](https://github.com/SLikeSoft/SLikeNet) | **License**: Mixed (RakNet + BSD/MIT)

**Assessment: Not recommended.** Effectively dead (last release September 2022, a security fix). Massive C++ codebase inherited from RakNet. Mixed licensing. No active development.

### SDL_net

**Repository**: [github.com/libsdl-org/SDL_net](https://github.com/libsdl-org/SDL_net) | **License**: zlib

**Assessment: Not recommended.** SDL_net is a thin cross-platform socket wrapper — raw TCP/UDP with no reliability layer, no connection management, no game-relevant features. Using it would mean writing all game networking from scratch on top of raw sockets. The SDL community itself recommends ENet over SDL_net for game networking.

### Summary

| Library | Language | Size | Dependencies | Encryption | P2P | Fits Engine? |
|---|---|---|---|---|---|---|
| **ENet** | C | ~14 files | None | No | Yes | **Best fit** |
| **zpl-c/enet** | C | Single header | None | No | Yes | **Best fit** |
| **netcode** | C | ~2 files | libsodium | Yes | No | Partial |
| **GameNetworkingSockets** | C++ (C API) | Large | OpenSSL, protobuf | Yes | Yes | Too heavy |
| **yojimbo** | C++ | Medium | libsodium | Yes | No | Wrong language |
| **SLikeNet** | C++ | Very large | Various | Yes | Yes | Dead project |
| **SDL_net** | C | Tiny | SDL | No | N/A | Too low-level |

## How Other Engines Do It

### Love2D

Love2D bundles **lua-enet** — Lua bindings directly over ENet. This is the recommended and most-used networking solution in the Love2D ecosystem. It also ships LuaSocket for lower-level TCP/UDP, but lua-enet is what multiplayer Love2D games use.

The community library **sock.lua** wraps lua-enet with an event-trigger system and automatic serialization, providing a higher-level API:

```lua
-- sock.lua example (Love2D)
local sock = require "sock"
local server = sock.newServer("*", 22122)

server:on("connect", function(data, client)
    print("Client connected!")
end)

server:on("playerPosition", function(data, client)
    -- data is automatically deserialized
    player.x, player.y = data.x, data.y
    server:sendToAll("playerPosition", data)
end)

function love.update(dt)
    server:update()
end
```

**Takeaway:** Love2D chose ENet as its transport and wraps it for Lua. This is the most directly relevant precedent — our engine follows the same architecture (C/C++ core with Lua scripting).

### Godot

Godot has a three-tier multiplayer architecture:

1. **High-level API** (`SceneMultiplayer`): RPC system with `@rpc` annotations, `MultiplayerSynchronizer` and `MultiplayerSpawner` nodes for automatic state replication.

2. **Mid-level abstraction** (`MultiplayerPeer`): Protocol-agnostic interface. Implementations include:
   - `ENetMultiplayerPeer` — the default, most commonly used
   - `WebRTCMultiplayerPeer` — for browser/P2P
   - `WebSocketMultiplayerPeer` — for web fallback

3. **Low-level wrappers**: `ENetConnection` and `ENetPacketPeer` wrap raw ENet functions.

```gdscript
# Godot server example
var peer = ENetMultiplayerPeer.new()
peer.create_server(7777, 32)  # port, max clients
multiplayer.multiplayer_peer = peer

@rpc("any_peer", "reliable")
func update_position(pos: Vector2):
    get_node(str(multiplayer.get_remote_sender_id())).position = pos
```

**Takeaway:** ENet is Godot's default and most-used transport. The abstraction layer allows swapping transports without changing game code.

### Unity

Unity's networking is fragmented across multiple solutions:

- **Netcode for GameObjects (NGO)**: Official high-level solution with `NetworkObject`, `NetworkBehaviour`, `NetworkVariable`, RPCs. Built on the Unity Transport Package.
- **Mirror**: Open-source community successor to the deprecated UNet. Most popular third-party solution.
- **FishNet**: Gaining traction. Server-authoritative, bandwidth-efficient, client-side prediction.
- **Photon**: Commercial cloud-based. Matchmaking, NAT punchthrough, rooms.

**Takeaway:** Unity demonstrates the industry trend toward layered architectures: low-level transport, mid-level connection management, high-level replication/RPC. Indie developers gravitate toward open-source solutions (Mirror, FishNet) over proprietary ones.

### Raylib

Raylib has no built-in networking. The community overwhelmingly uses **ENet** — there are [official community examples](https://github.com/raylib-extras/networking_example) of raylib + ENet client-server setups in pure C.

**Takeaway:** When a C game framework lacks networking, people reach for ENet. This reinforces ENet's position as the go-to C library.

## Architecture Patterns

### Client-Server

A dedicated server runs the game simulation authoritatively. Clients send inputs and receive state updates.

```
Client A ──┐
Client B ──┼── Server (authoritative)
Client C ──┘
```

| Aspect | Notes |
|---|---|
| Authority | Server is authoritative — easier to prevent cheating |
| Infrastructure | Requires server hosting (cost) |
| Latency | Round-trip to server adds latency |
| Scalability | Scales well with server capacity |
| NAT | Clients connect to server (simpler — server has a public IP) |
| Best for | Competitive games, larger player counts, games where anti-cheat matters |

### Peer-to-Peer

All peers connect directly (mesh topology) or through a host peer.

```
Client A ←→ Client B
   ↕    ╲ ╱    ↕
Client C ←→ Client D
```

| Aspect | Notes |
|---|---|
| Authority | No neutral authority — cheating is harder to prevent |
| Infrastructure | No server needed (free) |
| Latency | Direct connections can be lower latency |
| Scalability | Degrades with player count (N² connections in full mesh) |
| NAT | NAT punchthrough needed for all peers |
| Best for | Co-op, fighting games, small groups (2-4 players) |

### Synchronization Models

**Lockstep**: All players send inputs to all others, simulation advances only when all inputs are received. Requires bitwise-deterministic simulation (no floating-point divergence, identical RNG). Used by RTS games (StarCraft, Age of Empires). Minimal bandwidth (only inputs sent) but input delay proportional to worst player's latency.

**Rollback (GGPO)**: Players advance simulation immediately, predicting remote inputs. When actual inputs arrive and differ from prediction, roll back and resimulate. Requires fast state save/restore and deterministic simulation. Used by modern fighting games. Feels responsive but CPU cost increases with resimulation depth.

**Snapshot interpolation**: Server sends authoritative world-state snapshots at a fixed rate. Clients render in the past (~100ms behind), interpolating between two received snapshots. Local player uses client-side prediction for responsiveness. Used by most FPS games (Counter-Strike, Apex Legends). Does not require determinism. Bandwidth grows with world size but can be delta-compressed.

### What's appropriate for this engine

Snapshot interpolation is the most practical starting point:
- Does not require deterministic simulation (the engine uses floating-point physics via Box2D, which is not bitwise-deterministic across platforms)
- Well-understood pattern with extensive documentation (Glenn Fiedler's articles, Gabriel Gambetta's series)
- Works with both client-server and adapted P2P (one peer acts as host)
- Can be implemented incrementally — start with raw state broadcasting, add interpolation, then prediction

However, the networking *library* choice is independent of the synchronization model. The library provides transport (reliable/unreliable UDP, connections, channels). The synchronization model is game logic built on top. We should choose the library based on transport quality, not synchronization model.

## Recommendation: ENet (zpl-c fork)

### Why ENet

1. **Community standard.** Used or recommended by Love2D, Godot, Raylib's community, and countless indie games. Battle-tested over 20 years.

2. **Perfect fit for vendoring.** The zpl-c fork provides a single-header option (`enet.h` with `ENET_IMPLEMENTATION`), matching the stb pattern used throughout the engine (stb_truetype, stb_rect_pack, stb_vorbis, dr_wav).

3. **Zero dependencies.** Uses OS sockets directly. No OpenSSL, no protobuf, no libsodium. The smallest possible addition to the dependency tree.

4. **Pure C.** Integrates the same way as sqlite3, physfs, and glad.

5. **Supports both architectures.** An ENet host can simultaneously act as client and server. Client-server and P2P mesh are both possible without changing the transport layer.

6. **Appropriate scope.** ENet solves transport (reliability, ordering, fragmentation, connection management) and nothing else. It does not impose a synchronization model, serialization format, or game architecture. This matches the engine's philosophy of providing building blocks.

### Why the zpl-c fork over the original

| Feature | Original (lsalzman/enet) | Fork (zpl-c/enet) |
|---|---|---|
| IPv6 | No | Yes |
| Single-header option | No | Yes |
| Monotonic time | No | Yes (avoids clock drift issues) |
| Maintenance | Infrequent (stable) | Active (v2.6.5, June 2025) |
| API compatibility | — | Fully compatible with original |

The single-header option is the deciding factor. It can be vendored as a single file in `libraries/`, matching the engine's existing pattern.

### What ENet does NOT provide (and whether we care)

| Gap | Impact | Mitigation |
|---|---|---|
| **No encryption** | Packets can be read/modified in transit | Not a priority for an indie engine. Can layer encryption later (DTLS, libsodium) if needed for a specific game. |
| **No NAT punchthrough** | P2P between players behind NAT requires extra infrastructure | Can implement via a rendezvous server if needed. Most indie games use client-server with a public server, where NAT is not an issue. |
| **No serialization** | Must serialize game state manually | The engine already has ByteBuffer userdata and can add Lua-side serialization. This is game-specific anyway. |
| **No authentication** | No built-in way to verify player identity | Handle at the application layer (connect tokens, lobby system). |

## Integration Plan

### Step 1: Vendor the library

Add the zpl-c/enet single-header to `libraries/`:

```
libraries/
  enet.h          # single-header library (zpl-c/enet v2.6.5)
```

Create a compilation unit:

```c
// libraries/enet.c
#define ENET_IMPLEMENTATION
#include "enet.h"
```

Add to CMakeLists.txt:

```cmake
add_library(enet STATIC libraries/enet.c)
target_include_directories(enet PUBLIC ${PROJECT_SOURCE_DIR}/libraries)
target_link_libraries(Game PRIVATE ... enet ...)
```

On Linux, ENet requires linking against nothing extra (sockets are in libc). On Windows, it requires `ws2_32.lib` and `winmm.lib`:

```cmake
if(WIN32)
    target_link_libraries(enet PRIVATE ws2_32 winmm)
endif()
```

### Step 2: Engine module

Add a `Network` module to `EngineModules` in `src/game.cc`, following the same pattern as `Sound`, `Physics`, etc.

Create `src/network.h` and `src/network.cc` wrapping ENet:

```cpp
// src/network.h
#pragma once
#include <enet.h>
#include <functional>
#include <vector>

struct NetworkEvent {
    enum Type { Connect, Disconnect, Receive };
    Type type;
    uint32_t peer_id;
    uint8_t channel;
    std::vector<uint8_t> data;  // empty for connect/disconnect
};

class Network {
public:
    bool Init();
    void Shutdown();

    // Host management
    bool CreateServer(uint16_t port, size_t max_clients);
    bool CreateClient();
    bool Connect(const char* host, uint16_t port);
    void Disconnect();

    // Polling — called once per frame from the main loop
    void Poll(std::vector<NetworkEvent>& events);

    // Sending
    void Send(uint32_t peer_id, const void* data, size_t length,
              uint8_t channel, bool reliable);
    void Broadcast(const void* data, size_t length,
                   uint8_t channel, bool reliable);

private:
    ENetHost* host_ = nullptr;
};
```

### Step 3: Lua API

Expose networking through `G.network`, following the engine's existing Lua binding pattern (`lua_network.cc`):

```lua
-- Server example
local server = G.network.create_server(7777, 32)  -- port, max_clients

function _Game:update(t, dt)
    -- Poll returns events since last frame
    for _, event in ipairs(G.network.poll()) do
        if event.type == "connect" then
            print("Player connected: " .. event.peer_id)
        elseif event.type == "disconnect" then
            print("Player disconnected: " .. event.peer_id)
        elseif event.type == "receive" then
            local data = event.data  -- ByteBuffer
            -- handle message
        end
    end

    -- Send to a specific peer (reliable)
    G.network.send(peer_id, data, { channel = 0, reliable = true })

    -- Broadcast to all peers (unreliable, for position updates)
    G.network.broadcast(position_data, { channel = 1, reliable = false })
end
```

```lua
-- Client example
G.network.create_client()
G.network.connect("127.0.0.1", 7777)

function _Game:update(t, dt)
    for _, event in ipairs(G.network.poll()) do
        if event.type == "connect" then
            print("Connected to server!")
        elseif event.type == "receive" then
            -- handle server message
        end
    end

    G.network.send(server_id, input_data, { channel = 0, reliable = true })
end
```

The API design follows Love2D's lua-enet pattern (which is proven to work well for Lua game scripting) but adapted to the engine's `G.*` convention.

### Step 4: Channel conventions

ENet supports multiple channels per connection. A sensible default convention:

| Channel | Purpose | Mode |
|---|---|---|
| 0 | Control messages (connect, disconnect, game state changes) | Reliable, ordered |
| 1 | Entity state updates (positions, velocities) | Unreliable, unordered |
| 2 | Chat / text messages | Reliable, ordered |

The number of channels is configurable at host creation time. Game scripts can define their own channel semantics.

## Future Extensions

These are explicitly out of scope for the initial implementation but inform the design:

### Encryption

If a game needs encrypted connections, libsodium could be layered on top of ENet. Encrypt packet payloads before passing them to ENet, decrypt on receive. This is the approach used by games that need encryption but want to keep ENet as their transport.

### NAT punchthrough

For P2P behind NAT, the standard approach is:
1. Run a lightweight rendezvous server with a public IP
2. Both peers connect to the rendezvous server
3. The server exchanges peer addresses
4. Both peers send UDP packets to each other's external addresses simultaneously
5. Once punchthrough succeeds, communicate directly

This can be built on top of ENet without modifying ENet itself. The [enet-p2p](https://github.com/codecat/enet-p2p) project demonstrates this approach.

### Higher-level replication

A state synchronization / RPC layer (similar to Godot's `MultiplayerSynchronizer`) could be built in Lua on top of `G.network`. This would be a game-level library, not an engine feature — different games need different synchronization strategies.

### WebSocket / WebRTC fallback

If browser-based clients are ever needed, WebSocket or WebRTC transports could be added behind the same `G.network` API. ENet's API is simple enough that a WebSocket adapter with a compatibility shim is feasible. This is what Godot does with its `MultiplayerPeer` abstraction.

## Interaction with Hot Code Reloading

The engine's hot reload system has a specific flow: a background thread polls the filesystem every 10ms via `WriteAssetsToDb()`, sets an atomic flag when files change, and the main loop checks that flag every frame. When a reload triggers, the sequence is:

```
sound.StopAll()  →  assets->Load()  →  lua.LoadMain()  →  lua.Init()
```

The Lua VM (`lua_State*`) is **preserved** — it is not destroyed and recreated. Instead, the `_Game` global is set to `nil`, scripts are re-executed from source (clearing `package.loaded` entries so modules reload), and `init()` is called again. C++ module state (sound handles, physics world, renderer GPU resources) survives the reload.

### What this means for networking

The `Network` C++ module holds the `ENetHost*` and all peer connection state. Like the physics world and renderer, this state **must survive hot reload**. Tearing down connections on code reload would disconnect all players — unacceptable during development, which is the entire point of hot reload.

This means:

1. **ENet host and peer state lives in C++, not Lua.** The `Network` module owns the `ENetHost*`. Lua scripts get opaque peer IDs, not raw pointers. A hot reload re-executes all Lua code but the C++ network module is untouched — connections stay up, in-flight packets continue, peers remain connected.

2. **`init()` must not re-create the host.** Since `init()` is called again after every reload, a naive implementation where `init()` calls `G.network.create_server(7777, 32)` would try to create a second server on the same port. The C++ side must handle this gracefully — if a host already exists, `create_server()` should either return the existing host or log a warning and no-op. The simplest approach:

```cpp
bool Network::CreateServer(uint16_t port, size_t max_clients) {
    if (host_ != nullptr) {
        LOG("Network host already exists, skipping creation");
        return true;  // already running
    }
    // ... create ENet host
}
```

Alternatively, the Lua script can guard the call:

```lua
function _Game:init()
    if not G.network.is_active() then
        G.network.create_server(7777, 32)
    end
end
```

Both approaches work. The C++ guard is safer since it requires no discipline from the script author.

3. **Events during reload are not lost.** The reload sequence is synchronous on the main thread and takes a few milliseconds. During this window, no `poll()` calls happen, so ENet buffers incoming packets internally (ENet queues received packets until `enet_host_service` is called). The next `poll()` after reload returns all events that arrived during the reload window. No special buffering is needed — ENet's internal queue handles it.

4. **No `on_reload` callback needed for networking.** Sound calls `StopAll()` on reload because audio assets might have changed and streams need to be re-opened. Networking has no asset dependency — there are no "network assets" to reload. The network module can be completely passive during the reload cycle.

5. **Lua-side state is lost, C++-side state is not.** If a game script stores a table mapping peer IDs to player names, that table is lost on reload (all Lua globals reset). The connections themselves survive. The game script must re-derive any Lua-side bookkeeping from the C++ state after reload. This could be helped by providing `G.network.connected_peers()` which returns the list of currently connected peer IDs, so `init()` can rebuild its tables:

```lua
function _Game:init()
    if not G.network.is_active() then
        G.network.create_server(7777, 32)
    end
    -- Rebuild peer tracking after hot reload
    peers = {}
    for _, peer_id in ipairs(G.network.connected_peers()) do
        peers[peer_id] = { name = "unknown" }
        -- Could request name re-announcement from peer
    end
end
```

### Comparison with other modules

| Module | Reload behavior | Why |
|---|---|---|
| **Sound** | `StopAll()` — stops all playback | Audio streams reference asset data that may have changed |
| **Physics** | Preserved — world and bodies survive | Physics state is independent of script code |
| **Renderer** | GPU textures reloaded via callbacks | Image assets may have changed |
| **Network** (proposed) | Preserved — host and connections survive | Connection state is independent of script code |

## Interaction with the Allocation Model

The engine routes all allocations through its own allocator hierarchy. System `malloc` is used only once — to allocate the 4 GB main arena at startup. After that:

| Module | Allocator | Type | Size |
|---|---|---|---|
| Most modules | Main arena | `ArenaAllocator` | 4 GB total |
| Lua | Dedicated | `MimallocAllocator` | 64 MB |
| Frame temporaries | Dedicated | `ArenaAllocator` | 128 MB, reset each frame |
| Hotload temporaries | Dedicated | `ArenaAllocator` | 128 MB, reset each check |
| Box2D | Main arena via callbacks | `b2AllocFcn`/`b2FreeFcn` | — |
| Sound decoders | Local static buffers | Stack `ArenaAllocator` | 256 KB per decoder |

### Why ENet can't use the main ArenaAllocator directly

The main `ArenaAllocator` is a linear bump allocator. It supports `Dealloc()` only for the most recent allocation (LIFO). This works for modules that allocate at startup and never free (renderer, physics world setup), but ENet's allocation pattern is different:

- **Peer objects** are allocated when a client connects and freed when they disconnect — in arbitrary order.
- **Packet buffers** are allocated when a packet is created and freed after it's sent or delivered — short-lived but non-LIFO.
- **Internal protocol state** (acknowledgment lists, fragment reassembly buffers) has varied lifetimes.

This is a general-purpose malloc/free pattern, similar to what Lua does. The `ArenaAllocator` would never reclaim memory from disconnected peers or delivered packets — it would grow monotonically until it hits the arena ceiling.

### Recommended approach: MimallocAllocator for ENet

Give ENet its own `MimallocAllocator`, following the same pattern as Lua. With the modified ENet callbacks (see "Modifying ENet's allocator callbacks to pass size" above), the integration is a direct mapping to the engine's `Allocator` interface:

```cpp
// In EngineModules constructor, alongside lua_allocator:
MimallocAllocator network_allocator(
    allocator->Alloc(Megabytes(8), kMaxAlign),
    Megabytes(8));

// ENet callback wrappers — direct pass-through to Allocator
static Allocator* s_enet_allocator = nullptr;

void* ENetMalloc(size_t size) {
    return s_enet_allocator->Alloc(size, alignof(std::max_align_t));
}

void ENetFree(void* ptr, size_t size) {
    s_enet_allocator->Dealloc(ptr, size);
}

// During Network::Init():
s_enet_allocator = &network_allocator;
ENetCallbacks callbacks = { ENetMalloc, ENetFree, nullptr };
enet_initialize_with_callbacks(ENET_VERSION, &callbacks);
```

**8 MB** is generous for ENet. A typical ENet host with 32 peers uses well under 1 MB. The headroom accommodates packet buffers during traffic spikes.

### Modifying ENet's allocator callbacks to pass size

The engine's `Allocator` interface already follows the [nullprogram allocator checklist](https://nullprogram.com/blog/2023/12/17/) — free accepts the allocation size, realloc accepts both old and new sizes:

```cpp
virtual void* Alloc(size_t size, size_t align) = 0;
virtual void  Dealloc(void* p, size_t sz) = 0;
virtual void* Realloc(void* p, size_t old_size, size_t new_size, size_t align) = 0;
```

ENet's callbacks do not — its `free` takes only a pointer:

```c
typedef struct _ENetCallbacks {
    void *(ENET_CALLBACK *malloc) (size_t size);
    void (ENET_CALLBACK *free) (void *memory);           // no size
    void (ENET_CALLBACK *no_memory) (void);
} ENetCallbacks;
```

Since we vendor ENet, we modify it directly. The change is mechanical: add a `size` parameter to the free callback and thread it through all ~24 call sites. Every call site frees a known type with a known size — there is no guesswork.

**Callback struct change:**

```c
typedef struct _ENetCallbacks {
    void *(ENET_CALLBACK *malloc) (size_t size);
    void (ENET_CALLBACK *free) (void *memory, size_t size);  // added size
    void (ENET_CALLBACK *no_memory) (void);
} ENetCallbacks;
```

**`enet_free` change:**

```c
void enet_free(void *memory, size_t size) {
    callbacks.free(memory, size);
}
```

**Call site examples** (representative, not exhaustive):

```c
// Fixed-size struct frees (most call sites):
enet_free(outgoingCommand, sizeof(ENetOutgoingCommand));
enet_free(acknowledgement, sizeof(ENetAcknowledgement));
enet_free(incomingCommand, sizeof(ENetIncomingCommand));
enet_free(host, sizeof(ENetHost));

// Array frees (size stored on parent object):
enet_free(peer->channels, peer->channelCount * sizeof(ENetChannel));
enet_free(host->peers, host->peerCount * sizeof(ENetPeer));

// Packet frees (size stored on packet):
enet_free(packet, sizeof(ENetPacket) + packet->dataLength);

// Fragment bitmask frees (size derived from fragment count):
enet_free(incomingCommand->fragments,
          (incomingCommand->fragmentCount + 31) / 32 * sizeof(enet_uint32));
```

The default callback wraps system `free`, which ignores the size parameter:

```c
static void ENET_CALLBACK enet_default_free(void *memory, size_t size) {
    (void)size;
    free(memory);
}
```

This makes the modified ENet a clean fit for the engine's allocator interface — the callback maps directly to `Allocator::Dealloc` with no size headers or workarounds.

### Allocation budget

| Scenario | Estimated ENet memory use |
|---|---|
| Idle (host created, no peers) | ~10 KB |
| 4 peers, light traffic | ~50-100 KB |
| 32 peers, heavy traffic (60 packets/sec each) | ~500 KB - 1 MB |
| Worst case (32 peers, large fragmented packets, all channels) | ~2-4 MB |

8 MB provides comfortable headroom. If memory is tight, 4 MB is sufficient for most indie game scenarios (≤16 peers).

### Where ENet allocations happen in the frame

ENet allocations are driven by `enet_host_service()` (called during `poll()`). This means all ENet malloc/free activity happens once per frame, on the main thread, at a predictable point in the frame. There is no background-thread allocation concern — ENet is single-threaded and all calls happen from the main loop.

```
StartFrame()
  frame_allocator.Reset()     ← frame arena wiped
  input polling
  Network::Poll()             ← ENet allocations happen here (network_allocator)
  lua update()                ← Lua allocations happen here (lua_allocator)
  lua draw()
  BatchRenderer::Render()     ← frame_allocator used for meshes
EndFrame()
```

The network allocator is independent of the frame allocator — it persists across frames (connection state must survive) and is never reset.

### Summary

| Decision | Choice | Rationale |
|---|---|---|
| Allocator type | `MimallocAllocator` | ENet does malloc/free with varied lifetimes — needs a general-purpose allocator, not an arena |
| Budget | 8 MB | Generous for ≤32 peers; well under 1% of the 4 GB main arena |
| Integration | `enet_initialize_with_callbacks()` | Same pattern as Box2D (`b2SetAllocator`) and Lua (`lua_newstate` with custom alloc) |
| Sized free | Modify ENet's `free` callback to accept `size` | ~24 mechanical call sites; maps directly to `Allocator::Dealloc(ptr, size)` |
| Thread safety | Not needed | All ENet calls on main thread, single point in frame |

## Open Questions

1. **Should `G.network.poll()` be called automatically by the engine, or explicitly by the game script?** Automatic polling (in the main loop, before `update()`) is simpler and prevents the user from forgetting to poll. Explicit polling gives the game script more control over when network events are processed. Love2D's lua-enet requires explicit polling; Godot polls automatically.

2. **Should the API use callbacks or polling?** The proposed API uses polling (`G.network.poll()` returns a list of events). An alternative is callbacks (`G.network.on("connect", function(...) end)`). Polling is simpler, avoids reentrancy issues, and matches the engine's frame-based update model. Callbacks are more ergonomic for event-driven code. Both can coexist — poll internally, dispatch to registered callbacks.

3. **How should data serialization work?** ENet sends raw bytes. Games need to serialize Lua tables, numbers, strings into bytes and back. Options:
   - Use the existing `ByteBuffer` userdata for manual packing/unpacking
   - Add a simple Lua-side serialization library (like `binser` or `bitser`)
   - Build MessagePack or a custom binary format into the engine

4. **Thread safety.** ENet is not thread-safe. All ENet calls must happen on the same thread. Since the engine runs a single-threaded game loop, this is fine. If the engine ever moves networking to a background thread (for non-blocking DNS resolution, etc.), ENet calls would need to be marshaled back to the main thread.

5. **Multiple hosts.** Should a game be able to create multiple ENet hosts (e.g., a game server and a lobby client simultaneously)? ENet supports this, but the Lua API design would need to account for it.

## References

- [ENet documentation](http://enet.bespin.org/Features.html)
- [zpl-c/enet fork](https://github.com/zpl-c/enet)
- [Glenn Fiedler — Game Networking Articles](https://gafferongames.com/)
- [Gabriel Gambetta — Client-Side Prediction](https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html)
- [Godot multiplayer architecture](https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html)
- [Love2D lua-enet](https://love2d.org/wiki/lua-enet)
- [SnapNet — Netcode Architecture Series](https://www.snapnet.dev/blog/netcode-architectures-part-1-lockstep/)
- [Raylib + ENet examples](https://github.com/raylib-extras/networking_example)
- [enet-p2p (NAT punchthrough example)](https://github.com/codecat/enet-p2p)
