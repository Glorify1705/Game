#!/usr/bin/env bash
set -euo pipefail

# Run clang-tidy on staged C++ files under src/.
FILES=$(git diff --cached --name-only --diff-filter=d -- 'src/*.cc' 'src/*.h')

if [ -z "$FILES" ]; then
  exit 0
fi

if [ ! -f build/compile_commands.json ]; then
  echo "Error: build/compile_commands.json not found. Run a cmake configure first."
  exit 1
fi

# Nix clang wrapper injects C++ stdlib paths that clang-tidy doesn't see.
# Extract them and pass as --extra-arg (joined with -isystem, no space).
EXTRA_ARGS=()
for p in $(clang++ -v -x c++ /dev/null -fsyntax-only 2>&1 | grep -oP '(?<=-cxx-isystem )\S+'); do
  EXTRA_ARGS+=(--extra-arg="-isystem$p")
done
for p in $(clang++ -v -x c++ /dev/null -fsyntax-only 2>&1 | grep -oP '(?<=-idirafter )\S+'); do
  EXTRA_ARGS+=(--extra-arg="-isystem$p")
done

FAILED=0
for f in $FILES; do
  if ! clang-tidy -p build "${EXTRA_ARGS[@]}" "$f"; then
    FAILED=1
  fi
done

exit $FAILED
