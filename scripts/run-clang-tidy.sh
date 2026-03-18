#!/usr/bin/env bash
set -uo pipefail

# Run clang-tidy on staged C++ files under src/.
FILES=$(git diff --cached --name-only --diff-filter=d -- 'src/*.cc' 'src/*.h')

if [ -z "$FILES" ]; then
  exit 0
fi

if [ ! -f build/compile_commands.json ]; then
  echo "Error: build/compile_commands.json not found. Run a cmake configure first."
  exit 1
fi

# The nix clang wrapper injects include paths that clang-tidy doesn't see.
# Extract resource-dir, C++ isystem, package isystem, and glibc idirafter paths.
EXTRA_ARGS=()
CLANG_INFO=$(clang++ -v -x c++ /dev/null -fsyntax-only 2>&1)
RESOURCE_DIR=$(echo "$CLANG_INFO" | grep -oP '(?<=-resource-dir )\S+')
if [ -n "$RESOURCE_DIR" ]; then
  EXTRA_ARGS+=(--extra-arg="-resource-dir=$RESOURCE_DIR")
fi
for p in $(echo "$CLANG_INFO" | grep -oP '(?<=-cxx-isystem )\S+'); do
  EXTRA_ARGS+=(--extra-arg="-cxx-isystem" --extra-arg="$p")
done
for p in $(echo "$CLANG_INFO" | grep -oP '(?<=-isystem )\S+' | sort -u); do
  EXTRA_ARGS+=(--extra-arg="-isystem$p")
done
for p in $(echo "$CLANG_INFO" | grep -oP '(?<=-idirafter )\S+'); do
  EXTRA_ARGS+=(--extra-arg="-idirafter" --extra-arg="$p")
done

FAILED=0
for f in $FILES; do
  OUTPUT=$(clang-tidy -p build "${EXTRA_ARGS[@]}" "$f" 2>&1)
  # Only fail on warnings-as-errors from our src/ files, not vendored headers.
  if echo "$OUTPUT" | grep -qP '/src/[^/]+\.(cc|h):\d+:\d+: error:.*\[-warnings-as-errors\]'; then
    echo "$OUTPUT" | grep -v '/libraries/'
    FAILED=1
  fi
done

exit $FAILED
