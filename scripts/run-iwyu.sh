#!/usr/bin/env bash
set -euo pipefail

# Run include-what-you-use on staged .cc files under src/.
# Only .cc files are checked since IWYU needs a compilation command.
FILES=$(git diff --cached --name-only --diff-filter=d -- 'src/*.cc')

if [ -z "$FILES" ]; then
  exit 0
fi

if [ ! -f build/compile_commands.json ]; then
  echo "Error: build/compile_commands.json not found. Run a cmake configure first."
  exit 1
fi

FAILED=0
for f in $FILES; do
  # IWYU returns non-zero when it has suggestions. Capture output and check.
  OUTPUT=$(include-what-you-use -Xiwyu --mapping_file=iwyu.imp \
    $(python3 -c "
import json, sys
db = json.load(open('build/compile_commands.json'))
for entry in db:
    if entry['file'].endswith('$f'):
        # Split the command but skip the compiler and the source file
        import shlex
        args = shlex.split(entry['command'])
        # Print flags only (skip compiler [0] and source file [-1])
        print(' '.join(args[1:-1]))
        sys.exit(0)
print('', file=sys.stderr)
sys.exit(1)
") "$f" 2>&1) || true

  if echo "$OUTPUT" | grep -q "should add these lines\|should remove these lines"; then
    echo "=== IWYU suggestions for $f ==="
    echo "$OUTPUT"
    FAILED=1
  fi
done

exit $FAILED
