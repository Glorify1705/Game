#!/usr/bin/env bash
# game-profile: Build with in-engine profiler instrumentation and run
# the benchmark scene.
#
# Configures the default `build/` directory with -DENABLE_PROFILING=ON,
# rebuilds `Game`, then runs `./build/game run assets -- testBenchmark`.
# Use this for the engine's built-in stats/profiler output; for CPU
# sampling profiles see game-samply instead.
#
# Arguments: none.
set -euo pipefail
cmake -DENABLE_PROFILING=ON -G Ninja -S . -B build
cmake --build build --target Game
./build/game run assets -- testBenchmark
