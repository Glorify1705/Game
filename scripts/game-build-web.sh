#!/usr/bin/env bash
# Builds the engine for the web (WebAssembly + WebGL2) via Emscripten.
# Output: build-web/game.js + build-web/game.wasm, consumed by
# `game package --target web`.
set -euo pipefail

ROOT="${DEVENV_ROOT:?DEVENV_ROOT not set — run inside devenv shell}"

if ! command -v emcc >/dev/null; then
  echo "emcc not found. Enter the devenv shell (it provides emscripten)."
  exit 1
fi

# Emscripten caches compiled system libraries; the nix store copy is
# read-only, so point the cache at a writable project-local directory.
export EM_CACHE="${EM_CACHE:-$ROOT/.emscripten-cache}"

cmake --preset web "$@"
cmake --build --preset web
