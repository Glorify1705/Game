#!/usr/bin/env bash
set -uo pipefail

# Run include-what-you-use on staged .cc files under src/ via CMake integration.
FILES=$(git diff --cached --name-only --diff-filter=d -- 'src/*.cc')

if [ -z "$FILES" ]; then
  exit 0
fi

# Use CMake's IWYU integration which handles Nix include paths correctly.
cmake -DENABLE_IWYU=ON -G Ninja -S . -B build > /dev/null 2>&1

FAILED=0
for f in $FILES; do
  OBJ="CMakeFiles/Game.dir/${f}.o"
  OUTPUT=$(ninja -C build "$OBJ" 2>&1)
  if echo "$OUTPUT" | grep -q "should add these lines\|should remove these lines"; then
    echo "=== IWYU suggestions for $f ==="
    echo "$OUTPUT" | grep -A 100 "should add these lines\|should remove these lines"
    FAILED=1
  fi
done

# Reconfigure without IWYU for normal builds
cmake -DENABLE_IWYU=OFF -G Ninja -S . -B build > /dev/null 2>&1

exit $FAILED
