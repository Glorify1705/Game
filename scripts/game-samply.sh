#!/usr/bin/env bash
# game-samply: CPU sampling profiler for the testBenchmark scene.
#
# Builds a RelWithDebInfo binary in `build-profile/` (kept separate from
# the default Debug build so the two don't stomp on each other), runs
# `testBenchmark` under samply, writes the capture to
# `build-profile/samply.json.gz`, and opens it in Firefox Profiler.
#
# RelWithDebInfo is required: the Debug build uses -O1 -fno-inline, which
# makes trivial accessors (FVec ctor, FixedArray::Push, operator[]) show
# up as fake hotspots and buries the real ones.
#
# Arguments:
#   $1 — recording duration in seconds (default: 10). The benchmark is
#        killed via `timeout --signal=INT` after this elapses so samply
#        can flush its buffers cleanly.
set -euo pipefail
DURATION=${1:-10}
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -G Ninja -S . -B build-profile
cmake --build build-profile --target Game
echo "Recording for ${DURATION}s — running testBenchmark --clean (RelWithDebInfo)…"
samply record --save-only --unstable-presymbolicate -o build-profile/samply.json.gz -- \
  timeout --signal=INT --kill-after=3 "${DURATION}" ./build-profile/game run assets -- testBenchmark || true
echo "Opening capture in Firefox Profiler…"
samply load build-profile/samply.json.gz
