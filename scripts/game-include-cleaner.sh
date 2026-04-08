#!/usr/bin/env bash
# game-include-cleaner: Report suggested #include removals across src/.
#
# Ensures CMake has generated `build/compile_commands.json` and
# `build/implicit_include_dirs.txt` (written by CMakeLists.txt), then
# runs clang-include-cleaner over every .cc file in src/. Only
# deletions are reported (--print=changes --disable-insert) — insertions
# are suppressed because they tend to be noisy against our PCH/umbrella
# headers.
#
# The implicit include dirs file is needed because the Nix clang wrapper
# injects -isystem paths that standalone clang-include-cleaner cannot
# discover on its own; we forward them explicitly via --extra-arg.
#
# Arguments: none.
set -euo pipefail
cmake -G Ninja -S . -B build > /dev/null 2>&1
EXTRA_ARGS=()
while IFS= read -r dir; do
  [ -n "$dir" ] && EXTRA_ARGS+=(--extra-arg="-isystem$dir")
done < build/implicit_include_dirs.txt
for f in src/*.cc; do
  clang-include-cleaner --print=changes --disable-insert -p build "${EXTRA_ARGS[@]}" "$f" 2>/dev/null
done
