# CLI Workflow

The engine currently has an ad-hoc CLI interface: the `Game` binary takes positional arguments that are either a SQLite database path, a source directory + database path pair, or nothing. This is confusing, hard to discover, and tightly couples development and distribution into a single code path. This document proposes restructuring the engine into a proper CLI tool with explicit subcommands.

## Goals

1. A game developer can go from zero to running game in under a minute.
2. The development loop (edit → see change) should be as fast as possible.
3. Packaging a game for distribution should be a single command.
4. A player who receives a packaged game should be able to run it without compiling anything.
5. The CLI should be self-documenting (`--help` on every subcommand).

## Non-goals

- Cross-compilation (package for Windows from Linux). One platform at a time.
- Package manager integration (apt, brew, etc). Manual install is fine for now.
- Editor/IDE integration beyond what LuaLS stubs already provide.

## Proposed CLI

The binary is called `game`. All functionality is accessed through subcommands.

```
game <subcommand> [options]
```

### `game init`

Creates a new game project in the current directory (or a named directory).

```
game init [directory]
game init my-game
game init            # uses current directory
```

**What it does:**

1. Creates the directory if it doesn't exist.
2. Writes a scaffold of files:

```
my-game/
├── conf.json              # Window size, title, org/app name, version
├── main.lua               # Entry point that returns the game module
├── game.lua               # Starter game module with init/update/draw stubs
├── .luarc.json            # LuaLS config pointing to local definitions
└── definitions/
    └── game.lua           # LuaLS type stubs (auto-generated)
```

3. Prints a message telling the user to `cd my-game && game run`.

**Scaffold contents:**

`conf.json`:
```json
{
  "width": 800,
  "height": 600,
  "title": "my-game",
  "org_name": "MyOrg",
  "app_name": "my-game",
  "version": "0.1"
}
```

`main.lua`:
```lua
return require("game")
```

`game.lua`:
```lua
local Game = {}

function Game:init()
end

function Game:update(t, dt)
end

function Game:draw()
  G.graphics.clear()
  G.graphics.set_color("white")
  G.graphics.print("Hello, world!", 10, 10)
end

return Game
```

`.luarc.json`:
```json
{
  "workspace.library": ["definitions"],
  "runtime.version": "Lua 5.1",
  "diagnostics.globals": ["G"]
}
```

`game init` also generates a `definitions/game.lua` file directly in the project directory containing the full LuaLS type stubs. The game API is small enough that this is practical, and it avoids any external path resolution. The `.luarc.json` points to this local `definitions/` directory. If the engine is updated, running `game stubs --output my-game/definitions` refreshes the definitions.

**Options:**
- `--fennel` — scaffold with `.fnl` files instead of `.lua`, include `fennel.lua` compiler.

**Errors:**
- If the directory already contains a `conf.json`, refuse and print a message saying the project already exists.

---

### `game run`

Runs the game in the current directory with hot-reloading enabled.

```
game run [directory] [-- game-args...]
game run                         # run current directory
game run my-game                 # run specific project
game run -- pong                 # pass "pong" as game argument
game run my-game -- --debug      # both
```

**What it does:**

1. Looks for `conf.json` in the target directory. If not found, error: `"No game project found in <dir>. Run 'game init' first."`
2. Creates a temporary SQLite database (in a cache directory, not in the project) for the asset pipeline.
3. Packs all assets from the project directory into the database.
4. Launches the engine window, loads assets, starts the game loop.
5. Starts the file watcher for hot-reloading.
6. Any arguments after `--` are forwarded to `G.system.cli_arguments()` in Lua.

This is equivalent to the current `./Game /path/to/assets /path/to/db` invocation, but the user never has to think about the database.

**Cache directory:**

The asset database is ephemeral and should not live in the project directory. Location:

- Linux: `~/.cache/game/<project-hash>/assets.sqlite3`
- macOS: `~/Library/Caches/game/<project-hash>/assets.sqlite3`
- Windows: `%LOCALAPPDATA%\game\<project-hash>\assets.sqlite3`

The `<project-hash>` is derived from the absolute path of the project directory. This way multiple projects don't collide, and the cache survives across runs (for checksum-based skip optimization).

**Options:**
- `--clean` — delete the cached database and repack from scratch.
- `--no-hotreload` — disable the file watcher (useful for performance profiling).
- `--generate-stubs` — regenerate `definitions/game.lua` and exit (current `--generate-stubs` behavior, moved here).

Console output and logging behavior remain unchanged from the current engine.

---

### `game package`

Packages the game into a self-contained distributable.

```
game package [directory] [options]
game package                      # package current directory
game package my-game -o dist/     # package into dist/
```

**What it does:**

1. Validates the project (conf.json exists, main.lua/fnl exists).
2. Packs all assets into a SQLite database (same as `run`, but without hot-reload overhead).
3. Bundles the engine binary + asset database + shared libraries into a distributable format.
4. Writes the output to the specified directory.

**Output format:**

The default output is a directory containing everything needed to run the game:

```
dist/
├── my-game              # Engine binary (renamed to game title)
├── assets.sqlite3       # Packed asset database
└── lib/                 # Shared libraries (platform-dependent)
    └── libSDL2.so       # (or SDL2.dll on Windows, etc.)
```

The `lib/` directory contains any dynamically linked libraries the engine depends on. On Linux, the engine binary should be launched with `LD_LIBRARY_PATH` or use `RPATH` set to `$ORIGIN/lib` at link time. On Windows, DLLs placed next to the `.exe` are found automatically. On macOS, `@rpath` or `@executable_path/lib` can be used. If the engine is statically linked against SDL2 (a build option), the `lib/` directory is unnecessary.

The `game package` command discovers which shared libraries are needed by inspecting the engine binary (e.g., `ldd` on Linux, `otool -L` on macOS) and copies them into `lib/`. Libraries that are part of the base OS (libc, libm, libpthread, libGL) are excluded.

**Future direction:** The SQLite database currently stores asset contents inline as BLOBs. In the future, the database should only contain metadata (names, checksums, dimensions, sprite coordinates) while the actual asset data lives in zip archives that are streamed via PhysFS. This would make the package structure:

```
dist/
├── my-game
├── assets.sqlite3       # Metadata only
├── assets.zip           # Images, audio, fonts, shaders
└── lib/
```

See [[Asset system]] for more on this direction.

On each platform, this is what the player receives. They run `./my-game` and it picks up `assets.sqlite3` from the same directory (the current 0-argument `LoadDb` path).

**Single-file mode (stretch goal):**

Optionally, append the SQLite database to the end of the binary itself. The engine checks if its own executable has a SQLite trailer; if so, it reads assets from itself. This produces a single file that is the entire game. See [[Single-file packaging]] for the full design.

**Options:**
- `-o, --output <dir>` — output directory. Default: `./dist`.
- `--name <name>` — override the binary name. Default: derived from `conf.json` `app_name`.
- `--zip` — produce a `.zip` archive instead of a loose directory.
- `--strip` — strip debug symbols from the engine binary (smaller size).

**Platform considerations:**

- `game package` produces a package for the *current* platform only.
- The engine binary in the package is a copy of the `game` binary itself. It detects "packaged game" mode by the presence of `assets.sqlite3` next to it.
- Shared libraries (SDL2, etc.) are discovered and copied into `lib/`. The engine binary must be linked with `RPATH=$ORIGIN/lib` (Linux), `@executable_path/lib` (macOS), or rely on DLL search order (Windows).
- On macOS, a `.app` bundle could be produced instead of a loose directory (future work).
- On Windows, the binary gets a `.exe` extension and DLLs go next to the `.exe` directly (no `lib/` subdirectory needed).

**Errors:**
- Missing `conf.json` → `"No game project found."`
- Missing `main.lua` or `main.fnl` → `"No entry point script found. Create main.lua or main.fnl."`
- Asset packing failure → print which file failed and why.

---

### `game stubs`

Generates LuaLS type stubs for IDE autocomplete.

```
game stubs [--output <path>]
game stubs                         # writes to definitions/game.lua next to binary
game stubs --output my-game/types  # writes to custom location
```

This replaces the current `--generate-stubs` flag. It doesn't need a running game window — it just needs the Lua API metadata, which is compiled into the binary.

---

### `game version`

Prints the engine version and exits.

```
game version
```

Output:
```
game engine v0.1 (built Mar 12 2026)
```

---

### `game help`

Prints usage information.

```
game help
game help run
game help package
```

Also available as `game --help` and `game <subcommand> --help`.

---

## Detailed workflows

### Workflow 1: New developer, first game

```bash
# Install the engine (build from source for now)
cd /path/to/Game
cmake -G Ninja -S . -B build && cmake --build build
sudo cp build/game /usr/local/bin/game

# Create a new project
game init my-first-game
cd my-first-game

# Open in editor — LuaLS autocomplete works immediately
code .

# Edit game.lua, add some drawing code

# Run it
game run

# See changes live — edit game.lua in the editor, save, game hot-reloads
```

### Workflow 2: Iterating on an existing game

```bash
cd my-game

# Start the game with hot-reloading
game run

# In another terminal / editor:
# - Edit Lua scripts → game reloads automatically
# - Add new sprites / sounds → game picks them up
# - Edit shaders → reloaded on the fly

# If something breaks, the error shows in the game window
# Fix the script, save, game recovers automatically
```

### Workflow 3: Sending the game to a friend

```bash
cd my-game

# Package it
game package -o dist/ --zip

# This creates dist/my-game.zip containing:
#   my-game (or my-game.exe on Windows)
#   assets.sqlite3

# Send dist/my-game.zip to friend
# Friend unzips, runs ./my-game — that's it
```

### Workflow 4: Passing arguments to the game

Arguments after `--` are forwarded to `G.system.cli_arguments()` in Lua. This is a general-purpose mechanism — games can use it for anything (debug flags, level selection, etc). The default `main.lua` scaffold does **not** use CLI arguments; it simply returns a single game module.

The current `assets/main.lua` in the engine repository uses arguments to select between multiple test games (testgame1, pong, etc). This is a development convenience for the engine itself, not part of the scaffolded project structure.

```bash
# Pass arbitrary arguments to the game
game run my-game -- --debug --level=3
```

### Workflow 5: CI/CD pipeline

```bash
# In a GitHub Actions / GitLab CI job:
game package my-game -o artifacts/ --strip --zip
# Upload artifacts/my-game.zip as build artifact
```

### Workflow 6: Using Fennel instead of Lua

```bash
game init my-fennel-game --fennel
cd my-fennel-game

# Project now has:
#   main.fnl
#   game.fnl
#   fennel.lua (compiler)

game run
```

---

## Implementation plan

### Phase 1: Subcommand dispatch

Restructure `main()` in `game.cc` to parse the first argument as a subcommand. If no subcommand is given and `assets.sqlite3` exists next to the binary, run in "packaged game" mode (for `game package` output).

```
argv[0] = "game"
argv[1] = subcommand or legacy path

if argv[1] is a known subcommand → dispatch
else if assets.sqlite3 exists beside the binary → run as packaged game
else → print help
```

This preserves backward compatibility: a packaged game (just the binary + assets.sqlite3) still works. The subcommands are only used during development.

**Subcommand dispatch table:**
| argv[1]     | Handler            |
|-------------|--------------------|
| `init`      | `CmdInit()`        |
| `run`       | `CmdRun()`         |
| `package`   | `CmdPackage()`     |
| `stubs`     | `CmdStubs()`       |
| `version`   | `CmdVersion()`     |
| `help`      | `CmdHelp()`        |
| (missing)   | packaged or help   |

### Phase 2: `game init`

This is pure file I/O — no SDL, no OpenGL, no audio. The implementation:

1. Parse arguments (directory name, options).
2. Create directory.
3. Write scaffold files from string literals compiled into the binary.
4. Print instructions.

The scaffold templates can be `constexpr const char*` strings in a `src/templates.h` or similar. No runtime file reads needed.

### Phase 3: `game run`

This is mostly a reorganization of the existing `Game` constructor and `LoadDb`:

1. Resolve project directory (default: `.`).
2. Verify `conf.json` exists.
3. Compute cache directory path.
4. Create cache dir if needed, open SQLite database there.
5. Call existing `InitializeAssetDb`, `WriteAssetsToDb`, `ReadAssetsFromDb`.
6. Proceed with existing `Init()` and `Run()` paths.
7. Pass remaining args (after `--`) to Lua.

The key change: the database path is computed, not provided by the user.

### Phase 4: `game package`

1. Parse arguments (project dir, output dir, name, options).
2. Create a temporary SQLite database.
3. Pack all assets (same path as `run`).
4. Copy the engine binary to the output directory, renamed.
5. Copy the asset database next to it.
6. Optionally create a zip archive.
7. Optionally strip the binary.

The engine binary copy is just a file copy of `/proc/self/exe` (Linux) or equivalent. The binary must already support the "no arguments, assets.sqlite3 next to me" mode (which it does today via the 0-argument `LoadDb` path).

### Phase 5: `game stubs`

Extract the existing `--generate-stubs` code path into a standalone command that doesn't initialize SDL or open a window. It only needs the Lua API metadata arrays, which are compiled into the binary.

---

## Changes from the current architecture

| Aspect                | Current                              | Proposed                              |
| --------------------- | ------------------------------------ | ------------------------------------- |
| Invocation            | `./Game assets/ assets.sqlite3`      | `game run`                            |
| Database location     | User-specified or `./assets.sqlite3` | `~/.cache/game/<hash>/assets.sqlite3` |
| Project creation      | Manual (copy files, write conf.json) | `game init my-game`                   |
| Distribution          | Copy binary + database manually      | `game package`                        |
| IDE setup             | Manual `.luarc.json`                 | Generated by `game init`              |
| Stub generation       | `./Game --generate-stubs assets/ db` | `game stubs`                          |
| Game argument passing | `./Game assets/ db -- pong`          | `game run -- pong`                    |
| Packaged game run     | `./Game assets.sqlite3`              | `./my-game` (assets.sqlite3 adjacent) |

## Open questions

1. **Engine installation**: Where does `game` get installed? For now, users build from source and manually copy. A `make install` CMake target could handle this. Since definitions are generated inline into each project, there is no shared data directory to install — just the single binary.

2. **Project-local engine version**: Should a project pin to a specific engine version? `conf.json` already has a `version` field that is checked. This might be enough. A more sophisticated approach (bundling the engine per-project) is over-engineering for now.

3. **Asset database as implementation detail**: The SQLite database should be invisible to the user in development mode. It's a cache, not a source of truth. The source directory is the truth. This is already how it works; the CLI just makes it explicit.

4. **`game package` binary size**: The engine binary includes SDL2, Box2D, Lua, PhysFS, OpenGL loader, etc. This is unavoidable for a self-contained distributable. Stripping symbols helps. Static linking of SDL2 (instead of dynamic) would remove the DLL dependency on Windows. Current binary size should be documented and tracked.

5. **Single-file packaging**: See [[Single-file packaging]] for the full design. This is a nice-to-have, not a blocker for the CLI rework.
