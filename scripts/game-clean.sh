#!/usr/bin/env bash
# game-clean: Wipe the contents of the default `build/` directory.
#
# Removes build artifacts but keeps the directory itself, so the next
# `game-build` starts from a clean CMake configure. Does not touch
# `build-profile/` (see game-samply).
#
# Arguments: none.
set -euo pipefail
rm -rf build/*
