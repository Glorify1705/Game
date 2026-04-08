#!/usr/bin/env bash
# game-run: Build and launch the engine via the `Run` CMake target.
#
# The `Run` target is defined in CMakeLists.txt and invokes the built
# binary against the example assets with hot-reload enabled.
#
# Arguments: none.
set -euo pipefail
cmake -G Ninja -S . -B build
cmake --build build --target Run
