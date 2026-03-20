#!/usr/bin/env bash
set -uo pipefail

# Run clang-tidy on staged C++ files under src/ via CMake integration.
FILES=$(git diff --cached --name-only --diff-filter=d -- 'src/*.cc' 'src/*.h')

if [ -z "$FILES" ]; then
  exit 0
fi

# Use CMake's clang-tidy integration which handles Nix include paths correctly.
# Only rebuild the staged .cc files (cmake will only re-analyze changed files).
cmake -DENABLE_CLANG_TIDY=ON -G Ninja -S . -B build > /dev/null 2>&1

FAILED=0
for f in $FILES; do
  # Only .cc files have compilation commands
  if [[ "$f" == *.cc ]]; then
    OBJ="CMakeFiles/Game.dir/${f}.o"
    if ninja -C build "$OBJ" 2>&1 | grep -qP '\[-Werror,.*\]'; then
      ninja -C build "$OBJ" 2>&1 | grep -v '/libraries/'
      FAILED=1
    fi
  fi
done

# Reconfigure without clang-tidy for normal builds
cmake -DENABLE_CLANG_TIDY=OFF -G Ninja -S . -B build > /dev/null 2>&1

exit $FAILED
