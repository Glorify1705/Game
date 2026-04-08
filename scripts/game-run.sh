#!/usr/bin/env bash
# game-run: Build the engine and launch it against the example assets.
#
# Configures `build/`, builds the `Game` target, then invokes the
# resulting CLI directly: `./build/game run assets`. Previously this
# went through a custom CMake `Run` target, but the CLI now exposes
# the same workflow (with hot-reload), so the indirection is no
# longer needed.
#
# Arguments: none.
set -euo pipefail
cmake -G Ninja -S . -B build
cmake --build build --target Game
./build/game run assets
