# Game

A 2D game engine written in C++17 with Lua 5.1 and [Fennel](https://fennel-lang.org/) scripting. Uses a batched OpenGL renderer, Box2D physics, and a SQLite3-backed asset database. Game modules are written as Lua/Fennel scripts that export `init()`, `update(dt)`, and `draw()` callbacks.

## Building

The development environment is managed via [devenv](https://devenv.sh/) (Nix). Enter the shell with `devenv shell` or use direnv.

```
make all       # Build the Game executable (cmake + ninja)
make run       # Build and run the game
make test      # Build and run unit tests (with AddressSanitizer)
make format    # Format C++ (clang-format), Fennel (fnlfmt), Lua (stylua)
make clean     # Remove build directory
make debug     # Launch GDB debugger (gf2)
```

### Running a specific game module

The game loads `main.lua` which reads CLI args after `--` to select a module:

```
./build/Game assets ./build/assets.sqlite3 -- pong
```

Default module is `testgame1` if none specified. Available modules include `testgame1` (meteor shooter), `pong`, and `flappybird`.

### Asset database

Assets are stored in a SQLite3 database (`build/assets.sqlite3`). Schema is in `src/schema.sql`.

- `game-reset-db` — recreate the database from schema
- `game-open-db` — open database in SQLite browser

## Architecture

All C++ code lives in `namespace G`.

### Core systems (src/)

| File | Description |
|------|-------------|
| `game.cc` | Main loop: SDL init, window, game loop (init/update/draw), hot reload via inotify |
| `renderer.cc/h` | Batched 2D OpenGL renderer with command buffer and transform stack |
| `lua.cc/h` | Lua VM lifecycle, Fennel compilation with caching (XXH64 checksums) |
| `assets.cc/h` | SQLite3-backed asset loading (QOI/PNG, spritesheets, WAV/Vorbis, fonts) |
| `sound.cc/h` | Audio mixer via SDL callback, supports WAV (drwav) and Vorbis (stb_vorbis) |
| `physics.cc/h` | Box2D wrapper with 60 px/meter conversion, collision callbacks |
| `input.cc/h` | Keyboard/mouse state tracking and key mapping |

### Lua bindings (src/lua_*.cc)

Each `lua_*.cc` file exposes a C++ subsystem to Lua under the `G` global table (`G.graphics`, `G.physics`, `G.sound`, `G.input`, `G.math`).

### Custom data structures (src/)

- `allocators.h` — SystemAllocator, StaticAllocator\<N\>, ArenaAllocator
- `array.h` — FixedArray\<T,N\> and DynArray\<T\> (arena-friendly)
- `dictionary.h` — Hash map (arena-friendly, based on [nullprogram design](https://nullprogram.com/blog/2023/09/30/))
- `vec.h` / `mat.h` — Vector and matrix math
- `circular_buffer.h` — Ring buffer

## Libraries

SDL2, Lua 5.1, Box2D, PhysFS, xxHash, SQLite3, stb_image, stb_truetype, stb_vorbis, dr_wav, pugixml, GoogleTest

## Asset attributions

- `music.ogg` — Cyber City Detectives by Eric Matyas (www.soundimage.org)
- `sheet.xml` / `sheet.qoi` — [SpaceShooterRedux](https://www.kenney.nl/assets/space-shooter-redux) by Kenney
- `game-over.ogg` — [Game Over Arcade](https://pixabay.com/sound-effects/game-over-arcade-6435) (Pixabay)
- `pong-blip1.wav` / `pong-blip2.wav` — [NoiseCollector](https://freesound.org/people/NoiseCollector/packs/254/) (Freesound)
- `pong-score.wav` — [KSAplay](https://freesound.org/people/KSAplay/sounds/758958/) (Freesound)

## TODO

- [ ] Convert all vendored repos into Git submodules
- [ ] Compilable with Visual Studio
- [ ] Hot asset/code reloading
- [ ] Configure renderer allocator sizes with a header or environment variables
- [ ] Adapt PhysFS with custom allocators
- [ ] Use [gitstatus arena allocator](https://github.com/romkatv/gitstatus/blob/master/src/arena.h) as reference
