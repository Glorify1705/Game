---
status: implemented
tags: [filesystem, lua-api, json, physfs]
---

# Filesystem API

**Status: Under consideration.**

## Overview

The engine exposes file I/O to Lua scripts via `G.filesystem.*`. Today this
module has six functions: `slurp`, `spit`, `load_json`, `save_json`,
`list_directory`, and `exists`. The JSON pair are stubs (return
`"Unimplemented"`). Now that yyjson is vendored, we can implement them and
finalize the filesystem surface.

This document proposes the final shape of two modules:

- **`G.filesystem`** — file I/O (read bytes, write bytes, query paths)
- **`G.json`** — encode and decode JSON strings

JSON is decoupled from file I/O. Loading a JSON file is
`G.json.decode(G.filesystem.slurp(path))` — two composable operations, not
a monolithic `load_json`. This means `G.json` is also usable on strings
from any source (network messages, embedded data, clipboard).

## Design goals

1. **Minimal surface.** Every function must justify its existence. If
   something can be done by composing two existing calls, it doesn't get its
   own function.
2. **No raw paths.** All paths go through PhysFS. Scripts never see or
   construct OS-native paths. This makes packaging and sandboxing work for
   free.
3. **Consistent error convention.** Every function that can fail returns
   `(error, result)` — `nil` error on success, string error on failure. Fire
   and forget functions (like `spit`) return just the error string or `nil`.
4. **JSON is the structured format.** No XML, no INI, no TOML. One format,
   one library, one code path.

## `G.filesystem`

Six functions total. Four exist today; two are new (`stat`, `delete`).

### Reading and writing

```lua
-- Read an entire file into a byte_buffer.
local err, buf = G.filesystem.slurp(path)

-- Write a string or byte_buffer to a file, overwriting any previous content.
local err = G.filesystem.spit(path, data)
```

There is no `append` — games that need append semantics (logging) can
accumulate in memory and `spit` on save.

### Querying

```lua
-- Check whether a path exists.
local ok = G.filesystem.exists(path)

-- Get file metadata: size, type ("file" or "directory"), modification time.
local err, info = G.filesystem.stat(path)
-- info.size      (number, bytes)
-- info.type      ("file" or "directory")
-- info.modtime   (number, seconds since epoch)
```

`stat` is new. It exposes the `Filesystem::Stat` C++ method that already
exists but is not yet bound to Lua. Use cases: checking if a path is a
file or directory before operating on it, displaying file sizes in a
level-select screen, comparing modification times for cache invalidation.

### Directory listing

```lua
-- Return an array of all entries in a directory.
local entries = G.filesystem.list_directory(path)
```

Returns file and directory names as strings. To distinguish files from
directories, call `stat` on individual entries. This keeps `list_directory`
simple and avoids duplicating stat information in the returned table.

### Deleting

```lua
-- Delete a file. Returns nil on success, error string on failure.
local err = G.filesystem.delete(path)
```

`delete` is new. Without it, games cannot clean up temp files, old save
exports, or stale cache entries. PhysFS supports `PHYSFS_delete`, so this
is a thin wrapper.

Only files in the write directory can be deleted (PhysFS enforces this).
Deleting a non-existent file is not an error — it's a no-op.

### Complete `G.filesystem` function list

| Function | Args | Returns | Status |
|---|---|---|---|
| `slurp` | `path` | `err, byte_buffer` | exists |
| `spit` | `path, data` | `err` | exists |
| `exists` | `path` | `boolean` | exists |
| `stat` | `path` | `err, info_table` | new |
| `list_directory` | `path` | `table` | exists |
| `delete` | `path` | `err` | new |

## `G.json`

Two functions. Both are new (replacing the `load_json`/`save_json` stubs
which are removed from `G.filesystem`).

```lua
-- Parse a JSON string into a Lua table.
local err, tbl = G.json.decode(str)

-- Serialize a Lua value to a JSON string.
local err, str = G.json.encode(tbl)
```

### Loading a JSON file

```lua
local err, buf = G.filesystem.slurp("levels/level1.json")
if err then error(err) end
local err, data = G.json.decode(tostring(buf))
```

### Saving a Lua table as JSON

```lua
local err, str = G.json.encode(game_state)
if err then error(err) end
G.filesystem.spit("saves/slot1.json", str)
```

### Memory allocation

Both `decode` and `encode` use the engine's **per-frame scratch arena** for
all yyjson work. The frame allocator (`ArenaAllocator`, 128 MB) is reset at
the top of each frame in `StartFrame()`. All Lua calls happen during the
frame, so yyjson docs are bump-allocated into the frame arena and abandoned
— reclaimed for free on the next frame reset. No per-call arena creation or
teardown.

The frame allocator is registered with the Lua state via
`lua.Register(&frame_allocator)` and retrieved in the binding via
`Registry<ArenaAllocator>::Retrieve(state)`.

**`decode`**: Creates a `yyjson_alc` backed by the frame allocator (via
`MakeYyjsonAlc`, same adapter as `config.cc`). Parses the input string with
`yyjson_read_opts`. Walks the resulting `yyjson_doc`, pushing Lua values
onto the stack. The yyjson doc remains in the frame arena until the next
frame reset. The resulting Lua table is owned by the Lua GC.

**`encode`**: Creates a `yyjson_mut_doc` on the frame arena. Walks the Lua
table on the stack, building yyjson nodes. Calls `yyjson_mut_write_opts` to
produce a C string. Pushes the string to Lua (Lua copies it). The yyjson
doc and output buffer remain in the frame arena until reset.

### Supported types

| Lua type | JSON type | Notes |
|---|---|---|
| `nil` | `null` | |
| `boolean` | `true` / `false` | |
| `number` | number | NaN/Inf produce an error |
| `string` | string | Must be valid UTF-8 |
| `table` (array) | array | Sequential integer keys starting at 1 |
| `table` (map) | object | String keys only |

Functions, userdata, threads, and cyclic tables produce an error.

### Complete `G.json` function list

| Function | Args | Returns | Status |
|---|---|---|---|
| `decode` | `string` | `err, table` | new |
| `encode` | `table` | `err, string` | new |

## What was considered and rejected

### `mkdir`

PhysFS creates intermediate directories automatically when writing a file.
`G.filesystem.spit("saves/slot1/data.json", d)` creates `saves/slot1/` if
needed. An explicit `mkdir` would only be useful for creating empty
directories, which serves no purpose in a game filesystem.

### `rename` / `move`

PhysFS does not support rename. The workaround (`slurp` + `spit` + `delete`)
is adequate for the rare cases a game needs this.

### `append`

Append-mode file writing adds complexity to the PhysFS handle caching in
`Filesystem`. The only realistic use case is logging, which the engine
already handles in C++. Games that accumulate data can buffer in Lua and
write once.

### `glob` / `find` / recursive listing

A recursive `list_directory` or pattern-matching search adds API surface for
something scripts rarely need. Games that need to discover files (e.g., "all
`.json` files in `levels/`") can call `list_directory` and filter in Lua.

### `read_text` / `write_text` (separate from `slurp` / `spit`)

Having both byte-oriented and text-oriented read/write doubles the surface
for no practical benefit. Lua strings are byte sequences; `slurp` already
returns a `byte_buffer` that converts to string.

### `load_json` / `save_json` as filesystem methods

The original stubs coupled JSON parsing with file I/O in a single call.
Splitting into `G.json.decode`/`encode` is better: JSON becomes usable on
strings from any source, and `G.filesystem` stays a pure I/O module. The
composition `G.json.decode(tostring(G.filesystem.slurp(path)))` is one line
and makes the data flow explicit.

### `watch` / `on_change`

File watching is an engine-internal concern (hot reload). Exposing it to Lua
would require a callback or event system that doesn't exist yet, and the use
cases (mod development, live editing) are niche. Not worth the complexity.

## Implementation plan

### Step 1: `G.json` module (`lua_json.cc` / `lua_json.h`)

New files: `src/lua_json.cc`, `src/lua_json.h`.

Implement `G.json.decode` and `G.json.encode` using yyjson. Both use the
per-frame scratch arena via `Registry<ArenaAllocator>::Retrieve(state)` —
no per-call allocation. Register the frame allocator with the Lua state in
`game.cc`: `lua.Register(&frame_allocator)`.

The core helpers — `LuaPushJsonValue` (yyjson → Lua stack) and
`LuaToJsonValue` (Lua stack → yyjson mut node) — are exposed in the header
so the save system can reuse them later without going through Lua strings.

Remove the `load_json` and `save_json` stubs from `lua_filesystem.cc`.

### Step 2: Add `stat` to `G.filesystem`

Bind `Filesystem::Stat` to Lua. Returns a table with `size`, `type`, and
`modtime` fields. The C++ method already exists — this is just wiring.

### Step 3: Add `delete` to `G.filesystem`

Add `Filesystem::Delete` wrapping `PHYSFS_delete`. Bind to Lua. Deleting a
non-existent file returns `nil` (success), not an error.

### Step 4: Tests

Add tests in `tests/test.cc` for:

- `G.json.decode` / `encode` round-trip all supported types
- `G.json.decode` on malformed JSON returns an error, not a crash
- `G.json.encode` on unsupported types (function, userdata, cycle) errors
- `stat` returns correct type for files and directories
- `delete` removes a file, second delete is a no-op
- `delete` on a read-only (mounted archive) path returns an error

Lua-side tests in the test game exercising the full API from scripts.

## Interaction with the save system

The save system (`G.save.*`) will reuse the same Lua↔JSON conversion helpers
from `lua_json.h` for serializing values into SQLite blobs. It calls the C++
helpers directly — it does not go through `G.json.decode`/`encode` or
`G.filesystem`. The shared code is the conversion logic, not the Lua
bindings or the file I/O layer.

This gives us three cleanly separated modules:

- **`G.filesystem`** — file I/O (PhysFS)
- **`G.json`** — JSON ↔ Lua conversion (yyjson)
- **`G.save`** — persistent KV store (SQLite, uses json helpers internally)

## Interaction with the asset system

Assets loaded via `G.assets` come from the read-only asset database, not
through `G.filesystem`. The filesystem module operates on the PhysFS virtual
filesystem (mounted directories and archives). These are separate systems:

- `G.assets` — packed, indexed, read-only game assets
- `G.filesystem` — sandboxed read/write access to the user's app directory

A game script uses `G.assets` for sprites and sounds, and `G.filesystem`
for config files, save exports, screenshots, and user-created content.
