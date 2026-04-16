#!/usr/bin/env bash
# game-build: Configure (if needed) and build the main Game binary.
#
# Uses the "dev" CMake preset (Debug, Ninja, build/ directory).
# Safe to re-run; CMake reconfigures incrementally.
#
# Arguments: none.
set -euo pipefail
cmake --preset dev
cmake --build --preset dev
