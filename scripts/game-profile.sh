#!/usr/bin/env bash
# game-profile: Build with in-engine profiler instrumentation and run
# a scene.
#
# Uses the "profile" CMake preset (Debug + ENABLE_PROFILING=ON),
# then runs the scene with Chrome Tracing instrumentation.
# For CPU sampling profiles see game-samply instead.
#
# Arguments:
#   $1 — scene to run (default: testBenchmark).
set -euo pipefail
SCENE=${1:-testBenchmark}
cmake --preset profile
cmake --build --preset profile
./build/game run assets -- "${SCENE}"
