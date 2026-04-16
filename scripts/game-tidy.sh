#!/usr/bin/env bash
# game-tidy: Build the Game target with clang-tidy integrated into the
# compile step.
#
# Uses the "tidy" CMake preset (Debug + ENABLE_CLANG_TIDY=ON) which
# wires clang-tidy into CMake's CXX_CLANG_TIDY hook, so every compiled
# TU is also linted.
#
# Arguments: none.
set -euo pipefail
cmake --preset tidy
cmake --build --preset tidy
