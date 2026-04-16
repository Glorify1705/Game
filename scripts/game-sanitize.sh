#!/usr/bin/env bash
# game-sanitize: Build the Game target with ASan + UBSan enabled.
#
# Uses the "sanitize" CMake preset (Debug + ENABLE_SANITIZERS=ON).
# Note that toggling sanitizers on/off will trigger a full rebuild.
# Run the resulting binary manually; this script only builds.
#
# Arguments: none.
set -euo pipefail
cmake --preset sanitize
cmake --build --preset sanitize
