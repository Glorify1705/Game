#!/usr/bin/env bash
# game-profile: Build with in-engine profiler instrumentation and run
# the benchmark scene.
#
# Uses the "profile" CMake preset (Debug + ENABLE_PROFILING=ON),
# then runs `./build/game run assets -- testBenchmark`.
# For CPU sampling profiles see game-samply instead.
#
# Arguments: none.
set -euo pipefail
cmake --preset profile
cmake --build --preset profile
./build/game run assets -- testBenchmark
