#!/usr/bin/env bash
# game-build: Configure (if needed) and build the main Game binary.
#
# Uses the default Debug-ish build directory `build/` with the Ninja
# generator. Safe to re-run; CMake reconfigures incrementally.
#
# Arguments: none.
set -euo pipefail
cmake -G Ninja -S . -B build
cmake --build build --target Game
