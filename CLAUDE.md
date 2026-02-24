# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Development

Development environment is managed via [devenv](https://devenv.sh/) (Nix). Enter the shell with `devenv shell` or use direnv.

### Common Commands (via Makefile, which delegates to devenv scripts)

```
make all       # Build the Game executable (cmake + ninja)
make run       # Build and run the game
make test      # Build and run unit tests (with AddressSanitizer)
make format    # Format C++ (clang-format), Fennel (fnlfmt), Lua (stylua)
make clean     # Remove build directory contents
make debug     # Launch GDB debugger (gf2)
```

### Running the game with a specific module

The game loads `main.lua` which reads CLI args after `--` to select a game module:
```
./build/Game assets ./build/assets.sqlite3 -- pong
```
Default module is `testgame1` if none specified.

### Asset Database

Assets are stored in a SQLite3 database (`build/assets.sqlite3`). Schema is in `src/schema.sql`.
- `game-reset-db` — recreate the database from schema
- `game-open-db` — open database in SQLite browser

### Running a single test

Tests use Google Test. After `make test` builds the binary:
```
./build/Tests --gtest_filter='TestName.SubTest'
```

## Architecture

**Language**: C++17 with Lua 5.1 / Fennel scripting. All C++ code is in `namespace G`.

### Core Systems (src/)

- **game.cc** — Main loop: SDL init, window creation, game loop (init → update → draw), hot reload via inotify
- **renderer.cc/h** — Batched 2D renderer using OpenGL. Command buffer pattern with transform stack. Draws sprites, images, text, shapes
- **lua.cc/h** — Lua VM lifecycle, script loading, Fennel compilation with caching (checksums via XXH64), custom allocator
- **assets.cc/h** — SQLite3-backed asset database. Loads images (QOI/PNG), spritesheets (XML), audio (WAV/Vorbis), scripts, shaders, fonts
- **sound.cc/h** — Audio mixer with SDL audio callback. Supports WAV (drwav) and Vorbis (stb_vorbis). Distinguishes effects vs music
- **physics.cc/h** — Box2D wrapper. 60 pixels/meter conversion. Dynamic bodies, collision callbacks
- **input.cc/h** — Keyboard/mouse state tracking with key mapping

### Lua Bindings (src/lua_*.cc)

Each `lua_*.cc` file exposes a C++ subsystem to Lua under the `G` global table (e.g., `G.graphics`, `G.physics`, `G.sound`, `G.input`, `G.math`). Uses Lua registry pattern and userdata wrappers for C++ types.

### Custom Data Structures (src/)

- **allocators.h** — SystemAllocator, StaticAllocator<N>, ArenaAllocator
- **array.h** — FixedArray<T,N> and DynArray<T> (arena-friendly)
- **dictionary.h** — Hash map (arena-friendly, based on nullprogram design)
- **vec.h / mat.h** — Vector (FVec2-4, IVec2) and matrix (FMat2x2-4x4) math
- **circular_buffer.h** — Ring buffer

### Asset Scripts (assets/)

Game modules in Lua or Fennel. Each module exports `init()`, `update(dt)`, `draw()` callbacks. Examples: `testgame1.lua` (meteor shooter), `pong.fnl`, `flappybird.fnl`.

## Code Style

- Google-based clang-format (80 column limit, 2-space indent)
- snake_case for functions, PascalCase for types, UPPER_CASE for constants
- Pre-commit hooks: clang-format + DONOTSUBMIT checker
- Asserts via `CHECK` and `DIE` macros; logging via `Log()`
- Compiler flags: `-Wall -Wextra -Werror` — all warnings are errors

## Key Libraries (libraries/)

SDL2 (windowing/audio/input), Lua 5.1, Box2D (physics), PhysFS (virtual FS), xxHash (hashing), SQLite3 (asset DB), stb_image/stb_truetype/stb_vorbis/dr_wav (media loading), pugixml (XML), GoogleTest (testing)
