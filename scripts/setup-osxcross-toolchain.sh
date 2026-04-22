#!/usr/bin/env bash
# Builds osxcross from source for cross-compiling to macOS from Linux.
#
# Prerequisites:
#   - A macOS SDK tarball (e.g. MacOSX14.0.sdk.tar.xz) placed in the
#     toolchains/ directory. Extract from Xcode on a Mac:
#       ./toolchains/osxcross/tools/gen_sdk_package.sh
#     Or from an Xcode.xip on Linux:
#       ./toolchains/osxcross/tools/gen_sdk_package_pbzx.sh <xcode>.xip
#   - clang, cmake, git, libssl-dev, libxml2-dev, xz-utils
#
# The toolchain is built to toolchains/osxcross/ in the project root.
set -euo pipefail

ROOT="${DEVENV_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
TOOLCHAIN_DIR="$ROOT/toolchains/osxcross"

# Check if already built.
if compgen -G "$TOOLCHAIN_DIR/bin/*-apple-darwin*-clang++" > /dev/null 2>&1; then
  echo "osxcross already installed at $TOOLCHAIN_DIR"
  ls "$TOOLCHAIN_DIR"/bin/*-apple-darwin*-clang++ 2>/dev/null | head -1
  exit 0
fi

# Check for SDK tarball.
SDK_TARBALL=""
for f in "$ROOT"/toolchains/MacOSX*.sdk.tar.* "$TOOLCHAIN_DIR"/tarballs/MacOSX*.sdk.tar.*; do
  if [ -f "$f" ]; then
    SDK_TARBALL="$f"
    break
  fi
done

if [ -z "$SDK_TARBALL" ]; then
  echo "Error: No macOS SDK tarball found."
  echo ""
  echo "Place a MacOSX*.sdk.tar.xz file in toolchains/ or"
  echo "toolchains/osxcross/tarballs/"
  echo ""
  echo "To create one on a Mac with Xcode installed:"
  echo "  1. Clone osxcross: git clone https://github.com/tpoechtrager/osxcross"
  echo "  2. Run: osxcross/tools/gen_sdk_package.sh"
  echo "  3. Copy the resulting tarball to this machine"
  exit 1
fi

# Clone osxcross if not present.
if [ ! -d "$TOOLCHAIN_DIR/build" ]; then
  echo "Cloning osxcross..."
  git clone https://github.com/tpoechtrager/osxcross "$TOOLCHAIN_DIR"
fi

# Move SDK tarball to tarballs/ if needed.
mkdir -p "$TOOLCHAIN_DIR/tarballs"
if [ "$(dirname "$SDK_TARBALL")" != "$TOOLCHAIN_DIR/tarballs" ]; then
  cp "$SDK_TARBALL" "$TOOLCHAIN_DIR/tarballs/"
fi

# Build osxcross.
echo "Building osxcross (this may take a few minutes)..."
cd "$TOOLCHAIN_DIR"
UNATTENDED=1 ./build.sh

echo ""
echo "osxcross installed at $TOOLCHAIN_DIR"
echo "Available compilers:"
ls "$TOOLCHAIN_DIR"/bin/*-apple-darwin*-clang++ 2>/dev/null
echo ""
echo "Build for macOS arm64:"
echo "  cmake --preset macos-arm64 && cmake --build --preset macos-arm64"
echo ""
echo "Build for macOS x86_64:"
echo "  cmake --preset macos-x86_64 && cmake --build --preset macos-x86_64"
