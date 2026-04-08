#!/usr/bin/env bash
# game-open-db: Open the packed asset database in DB Browser for SQLite.
#
# Useful for inspecting what the packer wrote (textures, shaders, audio
# blobs, metadata tables). Requires an existing `build/assets.sqlite3`,
# which is created by running the engine or the packer at least once.
#
# Arguments: none.
set -euo pipefail
sqlitebrowser build/assets.sqlite3
