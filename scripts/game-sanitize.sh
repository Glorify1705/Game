#!/usr/bin/env bash
# game-sanitize: Build the Game target with ASan + UBSan enabled.
#
# Reuses the default `build/` directory but toggles
# -DENABLE_SANITIZERS=ON and forces Debug so sanitizer instrumentation
# is effective. Note that this will trigger a full rebuild if the
# previous configure had sanitizers off. Run the resulting binary
# manually; this script only builds.
#
# Arguments: none.
set -euo pipefail
cmake -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug -G Ninja -S . -B build
cmake --build build --target Game
