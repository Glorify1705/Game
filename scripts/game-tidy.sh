#!/usr/bin/env bash
# game-tidy: Build the Game target with clang-tidy integrated into the
# compile step.
#
# Sets -DENABLE_CLANG_TIDY=ON which wires clang-tidy into CMake's
# CXX_CLANG_TIDY hook, so every compiled TU is also linted. Uses the
# Nix-wrapped clang-tidy defined in devenv.nix so the Nix glibc/libstdc++
# include paths are discoverable.
#
# Arguments: none.
set -euo pipefail
cmake -DENABLE_CLANG_TIDY=ON -G Ninja -S . -B build
cmake --build build --target Game
