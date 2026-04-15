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

# Copy MinGW runtime DLLs next to the executable.
SYSROOT="$TOOLCHAIN/x86_64-w64-mingw32"
for dll in "$SYSROOT/lib/libgcc_s_seh-1.dll" \
           "$SYSROOT/lib/libstdc++-6.dll" \
           "$SYSROOT/bin/libwinpthread-1.dll"; do
  [ -f "$dll" ] && cp "$dll" "$ROOT/build-win64/"
done
