#!/usr/bin/env bash
# game-format: Format all first-party source files in place.
#
# Runs clang-format over `src/` and `tests/`, fnlfmt over every Fennel
# script under `assets/`, and stylua over every Lua script under
# `assets/`. Vendored code in `libraries/` is intentionally skipped.
#
# Arguments: none.
set -euo pipefail
clang-format -i src/* tests/*
for f in assets/*.fnl; do fnlfmt --fix "$f"; done
stylua assets/*.lua
