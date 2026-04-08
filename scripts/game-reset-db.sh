#!/usr/bin/env bash
# game-reset-db: Delete and recreate the packed asset database.
#
# Use when the asset schema changes or the packed DB has gotten into a
# bad state. Drops `build/assets.sqlite3` and re-applies `src/schema.sql`
# to produce an empty DB with the current schema. The next engine run
# will re-pack assets into it.
#
# Arguments: none.
set -euo pipefail
rm -rf build/assets.sqlite3
sqlite3 build/assets.sqlite3 < src/schema.sql
