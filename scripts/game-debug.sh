#!/usr/bin/env bash
# game-debug: Launch the Game binary under the gf2 graphical debugger.
#
# Assumes `./build/Game` has already been built (run `game-build` first).
# Passes `assets` and `./build/assets.sqlite3` as the engine's asset
# directory and packed asset DB arguments.
#
# Arguments: none.
set -euo pipefail
gf2 --args ./build/Game assets ./build/assets.sqlite3
