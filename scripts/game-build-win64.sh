#!/usr/bin/env bash
set -euo pipefail

ROOT="${DEVENV_ROOT:?DEVENV_ROOT not set — run inside devenv shell}"

TOOLCHAIN="$ROOT/toolchains/mingw-w64"
if [ ! -x "$TOOLCHAIN/bin/x86_64-w64-mingw32-g++" ]; then
  echo "MinGW toolchain not found. Run: scripts/setup-mingw-toolchain.sh"
  exit 1
fi

cmake -G Ninja \
  -S "$ROOT" \
  -B "$ROOT/build-win64" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64-x86_64.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  "$@"

ninja -C "$ROOT/build-win64"
