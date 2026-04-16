#!/usr/bin/env bash
# game-test: Build and run the GoogleTest suite.
#
# Uses the "dev" CMake preset (Debug) and builds the Tests target.
# Sanitizers (ASan/UBSan) are always enabled for the test binary.
#
# Extra arguments are forwarded to ctest (e.g. -R <regex> to filter tests).
set -euo pipefail
cmake --preset dev
cmake --build --preset test
ctest --preset dev "$@"
