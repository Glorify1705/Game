#!/usr/bin/env bash
set -euo pipefail

# Downloads the 7-Zip SFX module for creating self-extracting Windows archives.
# Only needs to be run once. The stub is stored in toolchains/7z-sfx/.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SFX_DIR="$ROOT/toolchains/7z-sfx"
SFX_STUB="$SFX_DIR/7zSD.sfx"

if [ -f "$SFX_STUB" ]; then
  echo "SFX stub already exists: $SFX_STUB"
  exit 0
fi

VERSION="26.00"
ARCHIVE="lzma2600.7z"
URL="https://github.com/ip7z/7zip/releases/download/$VERSION/$ARCHIVE"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "Downloading 7-Zip LZMA SDK $VERSION..."
curl -fSL -o "$TMPDIR/$ARCHIVE" "$URL"

echo "Extracting SFX modules..."
mkdir -p "$SFX_DIR"
7z e -o"$SFX_DIR" "$TMPDIR/$ARCHIVE" bin/7zSD.sfx >/dev/null

if [ ! -f "$SFX_STUB" ]; then
  echo "Error: SFX stub not found after extraction."
  exit 1
fi

echo "SFX stub installed: $SFX_STUB"
