#!/usr/bin/env bash
set -euo pipefail

# Creates a single self-extracting Windows .exe from a packaged game directory.
#
# Usage: game-sfx-win64 <dist-dir> [output.exe] [-- game-args...]
#
# The dist directory must contain the game .exe, assets.sqlite3, and DLLs.
# The resulting .exe extracts to a temp folder and runs the game.

ROOT="${DEVENV_ROOT:?DEVENV_ROOT not set — run inside devenv shell}"
SFX_STUB="$ROOT/toolchains/7z-sfx/7zSD.sfx"

if [ ! -f "$SFX_STUB" ]; then
  echo "SFX stub not found. Run: scripts/setup-7z-sfx.sh"
  exit 1
fi

if [ $# -lt 1 ]; then
  echo "Usage: game-sfx-win64 <dist-dir> [output.exe] [-- game-args...]"
  exit 1
fi

DIST_DIR="$1"
shift

# Parse optional output path and game arguments.
OUTPUT=""
GAME_ARGS=""
while [ $# -gt 0 ]; do
  case "$1" in
    --)
      shift
      GAME_ARGS="$*"
      break
      ;;
    *)
      OUTPUT="$1"
      shift
      ;;
  esac
done

if [ ! -d "$DIST_DIR" ]; then
  echo "Error: directory not found: $DIST_DIR"
  exit 1
fi

# Find the game executable in the dist directory.
GAME_EXE=$(find "$DIST_DIR" -maxdepth 1 -name "*.exe" ! -name "SDL3.dll" | head -1)
if [ -z "$GAME_EXE" ]; then
  echo "Error: no .exe found in $DIST_DIR"
  exit 1
fi
GAME_EXE_NAME=$(basename "$GAME_EXE")

# Default output name: same as the game exe, in current directory.
if [ -z "$OUTPUT" ]; then
  OUTPUT="./$GAME_EXE_NAME"
fi

# Build the RunProgram command.
RUN_CMD="$GAME_EXE_NAME"
if [ -n "$GAME_ARGS" ]; then
  RUN_CMD="$GAME_EXE_NAME -- $GAME_ARGS"
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# Create SFX config.
cat > "$TMPDIR/config.txt" << EOF
;!@Install@!UTF-8!
RunProgram="$RUN_CMD"
;!@InstallEnd@!
EOF

# Create 7z archive of the dist contents.
echo "Creating archive..."
(cd "$DIST_DIR" && 7z a -mx=3 "$TMPDIR/archive.7z" . >/dev/null)

# Concatenate: SFX stub + config + archive = self-extracting exe.
echo "Building SFX executable..."
cat "$SFX_STUB" "$TMPDIR/config.txt" "$TMPDIR/archive.7z" > "$OUTPUT"
chmod +x "$OUTPUT"

SIZE=$(du -h "$OUTPUT" | cut -f1)
echo "Created $OUTPUT ($SIZE)"
