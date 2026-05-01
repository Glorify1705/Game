---
status: implemented
tags: [persistence, save, achievements, sqlite, lua-api]
---

# Save and Persistence

**Status: Implemented** (PR #84). Steps 1–4 shipped: platform save directory,
`Save` C++ module, Lua bindings (`G.save.*`), and JSON serialization via
yyjson. Debug UI panel included.

**Still pending:**
- Step 5: CLI `game save` subcommand (dump, get, set, delete, clear, reset)
- `set_bytes` / `get_bytes` raw binary escape hatch
- Achievements API (likely Lua-side on top of the KV store)
- Open questions 1–6 below (resolved where noted)

## Overview

This document proposes a persistent key-value store for game state, settings,
achievements, and any other data a game needs to survive across runs. The
engine currently has no save system of any kind — games can write files via
Lua's `io.open`, but there is no platform-aware save directory, no atomic write
guarantees, no achievements concept, and no structured query API.

The goal is to add a single, simple, namespaced KV store exposed to Lua as
`G.save.*`, backed by a second SQLite database stored at a per-user platform
save directory. SQLite is already vendored and linked for the asset pipeline,
so the new feature adds no new dependencies.

## Motivation

Every shippable game needs persistence for at least one of the following:

- **Save slots** — resume progress, multiple save files
- **Settings** — volume, key bindings, resolution, language
- **Achievements** — whether each achievement has been unlocked
- **Statistics** — playtime, deaths, enemies killed, levels completed
- **High scores** — per-level or global leaderboards
- **Unlock flags** — which characters/levels/modes have been unlocked
- **Tutorial state** — which tooltips the player has already seen

Our engine currently supports none of these. A Lua game would have to roll its
own file format, pick a directory (which varies by platform), handle
corruption on crash, and reinvent the same wheel every project. This is
exactly the kind of infrastructure the engine should provide once.

Of the five comparison engines in the [Engine comparison](Engine%20comparison.md) doc,
only **Carimbo** has built-in persistent saves (its `Cassette` type) and
achievements (`Achievement` type). Love2D provides a sandboxed save directory
(`love.filesystem.getSaveDirectory()`) but leaves serialization to the user.
Raylib exposes `SaveStorageValue(position, value)` / `LoadStorageValue(position)`
which is a simple int-indexed storage API — adequate for a couple of high scores
but not for structured game state. high_impact has a userdata API tied to its
level loader. Anchor has nothing.

Our planned approach borrows Carimbo's ergonomics (a global KV API available
everywhere) but adds what's missing: **namespaced keys** so the API can
enumerate "all achievements" or "all save slots" cheaply.

## Constraints

| Constraint | Implication |
|---|---|
| Asset DB is read-only at runtime | Save data must live in a separate SQLite file |
| Reuse vendored sqlite3 | No new library to vendor, link, or integrate with the allocator hierarchy |
| Engine uses explicit allocators | Save DB must take an `Allocator*` and route through it, not touch system malloc |
| Lua 5.1 scripting | API must map cleanly to a `luaL_Reg` table; values must be representable in Lua 5.1 (no bit64) |
| One JSON path | Save serialization, `G.filesystem.load_json`, REPL message parsing, and asset packer all share one vendored library (yyjson) |
| Hot reload friendly | Save state must survive Lua hot reload — handle lives in C++ |
| No exceptions, no RTTI | Error handling via `ErrorOr` / `CHECK` |
| Small in-memory footprint | Save DB rarely exceeds a few hundred KB even for large games |
| Atomic on crash | A crash mid-save must not corrupt existing data |

## Schema

A single table with a composite primary key:

```sql
CREATE TABLE IF NOT EXISTS kv (
  namespace  TEXT NOT NULL,
  key        TEXT NOT NULL,
  value      BLOB,
  updated_at INTEGER NOT NULL,
  PRIMARY KEY (namespace, key)
) WITHOUT ROWID;
```

**Why this shape:**

- **Composite PK `(namespace, key)`** — enables cheap range scans for
  enumerating a single namespace. `SELECT key, value FROM kv WHERE namespace = ?`
  is an index scan, not a table scan.
- **`WITHOUT ROWID`** — save data is mostly small string/blob values; skipping
  the rowid layer reduces on-disk size and speeds up lookups for this exact
  access pattern. SQLite recommends `WITHOUT ROWID` for tables where the natural
  key is not an integer and rows are small.
- **`value BLOB`** — the universal type. We store serialized Lua values as
  opaque bytes; SQLite does not care about the format.
- **`updated_at INTEGER`** — Unix timestamp in milliseconds. Cheap to populate
  (`sqlite3_bind_int64`), useful for sort order, last-save-wins merge logic,
  and debugging ("when did this achievement unlock?").

**Indices**: none needed beyond the primary key. All queries filter on
`namespace` (which is the PK prefix) or on `(namespace, key)` equality.

**Natural namespaces** a game might use:

| Namespace | Keys | Values |
|---|---|---|
| `save` | `slot1`, `slot2`, `slot3`, `autosave` | Serialized game state tables |
| `achievements` | `first_kill`, `beat_level_1`, … | Unlock timestamps (row exists iff unlocked) |
| `settings` | `volume`, `fullscreen`, `lang`, `keybind:jump` | Scalars or short serialized tables |
| `stats` | `playtime`, `deaths`, `enemies_killed` | Integers |
| `highscore` | `level1`, `level2`, `endless` | Integers |
| `unlocks` | `char:rogue`, `level:6`, `skin:gold` | Booleans (presence = unlocked) |
| `tutorial` | `seen:wasd`, `seen:shoot` | Booleans |

Games are not required to use these specific namespaces — they are just the
obvious ones. The engine imposes no vocabulary.

## Lua API

Exposed as `G.save.*`, following the convention of other engine modules:

```lua
-- Scalar set/get.
G.save.set("achievements", "first_kill", os.time())
local ts = G.save.get("achievements", "first_kill")       -- number, or nil

-- Existence check (cheaper than a full fetch for the "is unlocked?" pattern).
if G.save.has("achievements", "first_kill") then ... end

-- Delete a single key.
G.save.delete("settings", "deprecated_flag")

-- Enumerate a namespace.
for key, value in G.save.list("achievements") do
  print(key, value)
end

-- Just the keys (avoids deserializing values when you only need names).
local keys = G.save.keys("achievements")   -- array

-- Wipe a whole namespace (useful for "reset progress").
G.save.clear("save")

-- List all namespaces in the database.
local namespaces = G.save.namespaces()     -- array

-- Force a flush to disk. Normally not needed — writes use WAL and are
-- durable after the transaction commits — but useful before explicit quit.
G.save.flush()
```

### Value types

The set/get functions accept and return any Lua value representable on the
stack. Tables are encoded as JSON; scalars are stored in a form that
round-trips back to the same Lua type:

| Lua type | Wire encoding | Notes |
|---|---|---|
| `nil` | Same as deleting the row | `set(ns, key, nil)` ≡ `delete(ns, key)` |
| `boolean` | JSON `true` / `false` | |
| `number` | JSON number | IEEE 754 double, same as Lua 5.1's only number type |
| `string` | JSON string | UTF-8 only; not binary safe (use `set_bytes` for raw bytes) |
| `table` | JSON object or array | Keys must be strings or contiguous integer indices |

Tables are encoded with the engine's vendored JSON library (see
[Switching to yyjson](Switching%20to%20yyjson.md)). The same library handles
`G.filesystem.load_json` / `save_json` and the asset packer's Aseprite
import, so there is exactly one JSON path in the codebase.

**Hard requirements:**

- **Round-trip fidelity**: `get(set(x))` returns `x` for any supported type.
  Booleans stay booleans, numbers stay numbers, tables come back with the
  same shape.
- **Cycle rejection**: setting a table that contains itself is a `CHECK`
  failure, not silent infinite recursion. yyjson's writer detects cycles
  for us.

Functions, userdata, threads, and metatables are explicitly not serializable
and produce a `CHECK` failure at `set` time. This matches Lua's own
expectations around persistence.

#### Raw bytes escape hatch

JSON strings are not binary-safe — embedded NULs and non-UTF-8 sequences
break the encoding. Games that need to store raw bytes (a screenshot
thumbnail, a packed struct, a network message) use a separate API:

```lua
G.save.set_bytes("save", "thumbnail", screenshot_bytes)
local bytes = G.save.get_bytes("save", "thumbnail")  -- byte_buffer or nil
```

`set_bytes` / `get_bytes` write the blob column directly with no encoding
wrapper, so the value is opaque to the JSON path. The blob is tagged with a
single leading byte (`0x00` for "JSON-encoded value", `0x01` for "raw bytes")
so a `get` against a `set_bytes` row returns an error rather than trying to
parse binary as JSON.

### Why JSON

The earlier draft of this doc proposed an engine-internal binary serializer
(~200 lines). Folding that work into the existing JSON dependency is a
better trade for several reasons:

1. **One serialization format in the engine, not two.** The asset packer
   already needs JSON for Aseprite spritesheets, and `G.filesystem.load_json`
   / `save_json` are already declared (just unimplemented). Adding a custom
   binary format would mean shipping, testing, and documenting two
   serializers when one suffices.
2. **Inspectable saves for free.** `game save dump` becomes a
   one-liner: read the blob, print as JSON. No custom decoder, no hex view,
   no format-tag parsing. `game save diff` between two save files becomes
   feasible without writing engine-side tooling.
3. **No migration story to invent.** A custom format needs a versioned
   header and forward-migration logic. JSON is self-describing — schema
   changes are handled in game code where they belong.
4. **Web export compatibility.** A game that ships both desktop and web
   builds gets save-file portability for free. Browser JS can read the
   exact same blob with `JSON.parse`.
5. **The common case stays cheap.** Scalars (achievements, settings,
   booleans) encode to a handful of bytes either way. JSON's overhead
   compared to a packed binary format only matters for large nested tables,
   and the design explicitly says large saves are out of scope.

What we give up by choosing JSON:

- **Binary-safe strings.** Mitigated by the `set_bytes` / `get_bytes`
  escape hatch above.
- **Numeric table keys other than 1..N.** JSON object keys are strings.
  Sparse integer-keyed tables would have to stringify their keys; in
  practice the only common case (sequential arrays) maps to JSON arrays
  and works as expected.
- **Float bit-exactness.** yyjson uses round-trip-safe float formatting
  (same as the printf `%.17g` family), so `get(set(x)) == x` holds for
  finite doubles. NaN and infinities are not representable in JSON and
  produce a `CHECK` failure at `set` time.

## Platform save directory

Prerequisite: a `GetUserSaveDir(const char* app_name, char* out, size_t
out_size)` function in `src/platform.h`, modeled on the existing
`GetUserCacheDir`. Resolves to:

| Platform | Path |
|---|---|
| Linux | `$XDG_DATA_HOME/<app_name>/` (default `$HOME/.local/share/<app_name>/`) |
| Windows | `%APPDATA%\<app_name>\` |
| macOS | `$HOME/Library/Application Support/<app_name>/` |
| Web (WASM) | IndexedDB-backed `/save/` mount (Emscripten `FS.syncfs`) |

The directory is created (`MakeDirs`) on first open if it does not exist. The
save database lives at `<save_dir>/save.sqlite3`.

**App name resolution**: comes from `conf.json` via the existing config
module. If the game does not specify a name, we fall back to the binary stem.
Two games on the same machine never share a save directory.

## C++ module

A new `Save` module in `src/save.cc` / `src/save.h`, added to `EngineModules`
in `src/game.cc` alongside `Assets`, `Sound`, `Physics`, etc.

```cpp
// src/save.h
#pragma once
#include <sqlite3.h>
#include "allocators.h"
#include "error.h"
#include "strings.h"

namespace G {

class Save {
 public:
  // Opens or creates the save database at <save_dir>/save.sqlite3.
  // Does not take ownership of the allocator.
  ErrorOr<void> Open(const char* save_dir, Allocator* allocator);

  // Flushes pending writes and closes the database.
  void Close();

  // Atomic set: INSERT OR REPLACE with updated_at bound to now.
  ErrorOr<void> Set(StringView ns, StringView key,
                    const void* value, size_t len);

  // Fetch a single value. Returns Error::NotFound if the row does not exist.
  // The returned slice is valid until the next Save call on this instance.
  ErrorOr<Slice<const uint8_t>> Get(StringView ns, StringView key);

  // Cheap existence check — does not load the value blob.
  bool Has(StringView ns, StringView key);

  // Delete one key. No error if the row does not exist.
  ErrorOr<void> Delete(StringView ns, StringView key);

  // Enumerate keys in a namespace. Calls `visit` once per row.
  // `visit` must not mutate the Save instance during iteration.
  ErrorOr<void> List(StringView ns,
                     void (*visit)(StringView key,
                                   Slice<const uint8_t> value,
                                   void* user_data),
                     void* user_data);

  // Wipe all rows in a namespace.
  ErrorOr<void> Clear(StringView ns);

  // Enumerate distinct namespace names.
  ErrorOr<void> Namespaces(void (*visit)(StringView name, void* user_data),
                           void* user_data);

  // Force a WAL checkpoint. Optional — WAL is durable after commit anyway.
  ErrorOr<void> Flush();

 private:
  sqlite3* db_ = nullptr;
  Allocator* allocator_ = nullptr;

  // Prepared statements, created lazily and cached for the life of the DB.
  sqlite3_stmt* set_stmt_ = nullptr;
  sqlite3_stmt* get_stmt_ = nullptr;
  sqlite3_stmt* has_stmt_ = nullptr;
  sqlite3_stmt* delete_stmt_ = nullptr;
  sqlite3_stmt* list_stmt_ = nullptr;
  sqlite3_stmt* clear_stmt_ = nullptr;
  sqlite3_stmt* namespaces_stmt_ = nullptr;

  // Scratch buffer for the last fetched blob.
  DynArray<uint8_t> fetch_buf_;
};

}  // namespace G
```

### Prepared statements

All writes and queries use prepared statements, cached on the `Save` instance
and `sqlite3_reset()` between calls. This is the normal SQLite perf pattern
and avoids re-parsing SQL on every save operation. Seven statements cover the
entire API.

### WAL mode

The first thing `Open` does after `sqlite3_open_v2` is:

```sql
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
```

WAL gives us:

- **Atomic writes without fsync on every commit** (`synchronous = NORMAL`
  fsyncs only at checkpoint time, not per transaction — good enough for save
  data and roughly an order of magnitude faster than `FULL`)
- **Crash safety** — a crash mid-write leaves the previous committed state
  intact; in-progress writes are discarded on the next open
- **Concurrent read + write** — not strictly needed since we're single-threaded,
  but future-proof

### Allocator integration

The save DB shares the same `SQLITE_CONFIG_HEAP` memsys5 configuration that
the asset DB already uses. Both databases live in the same SQLite heap —
which is fine because SQLite's heap is global, not per-database. No changes
to `cmd_run.cc`'s existing heap setup are needed.

**Budget check**: the existing 16 MB SQLite arena already covers asset DB
open/query traffic. Adding a second small database (save data is rarely more
than a few hundred KB) increases peak usage by maybe 1-2 MB. If that ever
becomes tight, we can grow the arena in one line.

## Hot reload interaction

The `Save` module owns a long-lived `sqlite3*` handle. Like `Physics` and
`Renderer`, this handle **must survive hot reload** — otherwise every time
the user edits a Lua file, we'd close and reopen the save DB, which is both
wasteful and a potential source of subtle bugs (what if a write is in flight
when the reload triggers?).

This means:

1. `Save::Open` is called once at engine startup, not from Lua `init()`.
2. The Lua binding (`lua_save.cc`) holds a non-owning pointer to the engine's
   `Save` instance. On hot reload the Lua state is re-initialized and the
   binding re-registered, but the underlying C++ object is untouched.
3. There is no teardown needed on reload. The sqlite3 handle, prepared
   statements, and WAL state all continue as-is.

This matches the pattern used by `Physics` (Box2D world survives reload),
`Sound` (streams survive — but playback stops), and `Renderer` (GPU state
survives).

## CLI tooling

The `game` CLI gets a new `save` subcommand for inspecting and editing save
data without running the game:

```
game save path                    # print the resolved save dir path
game save dump                    # dump all rows as TSV (namespace, key, value)
game save dump --ns achievements  # dump one namespace
game save get <ns> <key>          # print a single value (decoded)
game save set <ns> <key> <value>  # set a value (strings only from CLI)
game save delete <ns> <key>       # delete a single key
game save clear <ns>              # wipe a namespace
game save reset                   # delete the whole save file (with --yes)
```

This is the kind of tooling that makes save data debuggable in a way
Carimbo's Cassette is not — because we own the format, we can inspect it.

## Interaction with packaging

`game package` produces a single-file binary with the asset DB appended. The
save DB is separate and lives in the user's save directory, not inside the
packaged binary. This is the correct behavior:

- Saves must survive re-packaging (a game update shouldn't wipe progress).
- Saves are per-user, not per-binary.
- Packaging a binary with embedded saves would mean the game ships with the
  developer's save file, which is nonsense.

## What this does NOT do

Scope control matters. These are explicitly out of scope:

- **Save slot management UI** — the engine provides the KV store, not the
  main-menu "load game" screen. That's game-side Lua.
- **Save file encryption / obfuscation** — save data is plain SQLite. Anyone
  with the file can read or edit it. If a game needs anti-tampering (e.g.
  for online leaderboards), it can encrypt blobs before calling `set`.
- **Cloud sync** — no Steam Cloud, no iCloud, no Google Play Saves. These
  are per-platform SDKs that belong in separate integrations.
- **Schema migration** — the engine does not understand game save schemas
  and cannot migrate them. If a game changes its save format between
  versions, the game's Lua code must read the old format, transform it, and
  write the new format. JSON's self-describing nature makes this easier
  than a binary format would have, but it's still game-side work.
- **Multi-process locking** — SQLite handles intra-process concurrency via
  WAL. If two instances of the game run simultaneously (e.g. user launches
  the game twice), SQLite's file locking will serialize their writes but we
  do not guarantee semantic correctness under that scenario.
- **Backup / rollback** — no automatic "previous version" of saves. Games
  that want multiple backup slots can write to `save:slot1_backup`
  themselves.

## Integration plan

### Step 1: Platform save directory resolver

Add `GetUserSaveDir` to `src/platform.h` and implement per-OS in the existing
`platform.cc`. This is the smallest prerequisite and the one most likely to
hit platform-specific quirks (especially macOS and Windows). Cover it with a
test that just calls the function and verifies the returned path is
absolute and writable.

### Step 2: `Save` module + C++ tests

Implement `src/save.cc` / `src/save.h` with the API above. Unit tests in
`tests/test.cc`:

- Round-trip all scalar types (nil, bool, int, float, short/long string,
  empty blob)
- Round-trip nested tables (array, map, mixed)
- Enumerate a namespace with `List`, verify order matches insertion
- `Clear` wipes a namespace but leaves others intact
- Reopening a closed DB preserves data
- Concurrent reads during writes (WAL sanity check)
- Corruption-safety: simulate a crash mid-transaction, reopen, verify
  previous committed state intact

### Step 3: Lua binding

`src/lua_save.cc` exposing `G.save.*`. The binding follows the newer
`LuaApiFunction` pattern (not the older `luaL_Reg`) so the functions show up
in generated type stubs with parameter names and docs.

Lua tests in `assets/test_save.lua`, exercised by the existing Lua test
harness.

### Step 4: Lua ↔ JSON glue

The encode and decode helpers live in `src/lua_save.cc` (or a private
helper file shared with `lua_filesystem.cc`'s `load_json`/`save_json`). The
encoder walks a Lua table and emits yyjson nodes; the decoder walks a
yyjson document and pushes Lua values. Both routes use the engine's arena
allocator via `yyjson_alc` — see
[Switching to yyjson](Switching%20to%20yyjson.md) for the wiring details.

This step depends on yyjson being vendored first. If the yyjson migration
is not yet done, this step blocks until it is — there is intentionally no
fallback to a hand-written serializer.

Tests: every supported type round-trips; every unsupported type (function,
userdata, thread, cyclic table) fails with a clear error message;
`set_bytes` / `get_bytes` round-trip raw binary; mixing `set` and
`set_bytes` on the same key returns the right error.

### Step 5: CLI `save` subcommand

`src/cmd_save.cc` implementing the subcommands listed above. Reuses the
`Save` module directly (no running engine needed).

### Step 6: Example game using saves

Update the testgame1 demo to use `G.save` for at least one of: high score,
volume setting, tutorial-seen flag. This validates the API end-to-end and
gives new users a copy-paste example.

## Open questions

1. **Should `G.save.list(ns)` return an iterator or a materialized table?**
   The Lua API sketch above uses iterator syntax (`for k, v in
   G.save.list(ns) do`). The alternative is `local t = G.save.list(ns)` which
   returns a full table. Iterator is cheaper for large namespaces; table is
   more idiomatic Lua. Probably: provide both, call the table version
   `G.save.all(ns)`.

2. **Do we expose transactions to Lua?** SQLite supports `BEGIN` / `COMMIT`
   for batching writes into a single fsync. For the save-on-quit case this
   matters (saving 50 achievement unlocks in one transaction vs 50
   transactions). Possible API: `G.save.transaction(function() ... end)`.
   Not strictly needed v1 — the defaults are fast enough — but worth
   considering.

3. **Should `has()` and `get()` be separate functions, or should `get()`
   return `nil` for missing keys and the caller uses `if x == nil`?** The
   latter is more Lua-idiomatic but loses the ability to store `nil` as a
   distinct value (which we already don't support, so this is moot — lean
   toward merging `has()` into `get()` returning `nil`).

4. **What about atomic read-modify-write?** Patterns like "increment death
   counter" are common. Without transactions, that's three API calls
   (`get`, `+1`, `set`) which is fine for single-threaded code but awkward.
   Possible helpers: `G.save.increment(ns, key, delta)` or
   `G.save.update(ns, key, function(old) return new end)`.

5. **Save file location override for testing.** Tests should use a temp
   directory, not the real user save dir. Need an `Open(explicit_path)`
   overload or an env var override (`GAME_SAVE_DIR`). Env var is simpler.

6. **Achievements API on top of KV, or standalone?** One option is to build
   a higher-level `G.achievements.*` API in Lua on top of `G.save`, with
   functions like `unlock(id)` / `is_unlocked(id)` / `list_unlocked()`. This
   would keep the engine surface area small. Alternatively, expose
   achievements as a first-class engine concept like Carimbo does. Lean
   toward building it in Lua — there's nothing achievements need that the
   KV store doesn't already provide, and game-specific UI (popups,
   icons, progress bars) belongs in user code anyway.

## References

- [Carimbo Cassette docs](https://github.com/willtobyte/carimbo) — the feature
  this is modeled after
- [SQLite WITHOUT ROWID](https://www.sqlite.org/withoutrowid.html)
- [SQLite WAL mode](https://www.sqlite.org/wal.html)
- [Memory allocators for third-party libraries](Memory%20allocators%20for%20third-party%20libraries.md) — existing memsys5 setup we reuse
- [Switching to yyjson](Switching%20to%20yyjson.md) — the JSON library that powers save serialization
- [Single-file packaging](Single-file%20packaging.md) — why save data lives outside the packaged binary
- [Engine comparison](Engine%20comparison.md) — section 14 (Save / Persistence / Achievements)
