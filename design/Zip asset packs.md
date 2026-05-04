---
status: in-design
tags: [assets, packaging, tooling]
---

# Zip Asset Packs

Let the asset packer scan `.zip` files in the project directory and pack their
contents into the asset database alongside loose files. This eliminates the
unzip-and-copy step when using third-party asset packs (Kenney, itch.io
bundles, etc.).

## Motivation

Current workflow for using a downloaded asset pack:
1. Download `kenney_pixel-platformer.zip`
2. Unzip to a temporary directory
3. Copy the files you need into `assets/`
4. Delete the temporary directory

Proposed workflow:
1. Download `kenney_pixel-platformer.zip`
2. Drop it into `assets/`
3. Reference files by name in your Lua scripts

The engine already vendors PhysFS, which has native zip reading support.

## Design

### Scanning

During `game run` (or `game package`), the asset packer already walks the
project directory to find files matching known extensions (`.png`, `.qoi`,
`.lua`, `.fnl`, `.wav`, `.sprites.json`, `.sprites.xml`, etc.).

Add a second pass: for every `.zip` file found, open it with PhysFS and
enumerate its contents. Any file inside the zip that matches a known asset
extension is eligible for packing.

### Path flattening

Zips often have nested directory structures:
```
kenney_pixel-platformer.zip/
  Tilemap/
    tilemap_packed.png
    tilemap-characters_packed.png
  Tiles/
    tile_0000.png
    tile_0001.png
  Tiled/
    tilemap-example-a.tmx
```

The asset database uses flat names (no directory hierarchy). The packer
should flatten zip paths to just the filename: `Tilemap/tilemap_packed.png`
becomes `tilemap_packed.png` in the database.

### Name collision resolution

With multiple zips and loose files, name collisions are inevitable. Two
`player.png` files from different packs must not silently overwrite each
other.

**Resolution order** (highest priority first):
1. **Loose files** in the project directory always win. If `assets/player.png`
   exists as a loose file, any `player.png` inside a zip is ignored.
2. **Zips are processed alphabetically by filename.** If two zips both contain
   `player.png`, the one from the alphabetically-first zip wins.
3. **On collision, log a warning**: `"asset 'player.png' from 'pack-b.zip'
   shadowed by 'pack-a.zip'"`. This makes conflicts visible without crashing.

### Namespacing (optional, deferred)

A future enhancement could prefix zip assets with the zip filename:
`kenney_pixel-platformer/tilemap_packed.png`. This eliminates all collisions
but makes asset names longer and couples them to the zip filename. Defer
until the flat approach proves insufficient.

### Filtering

Not all files in a zip are useful. The packer should only extract files with
known asset extensions. Unknown extensions (`.url`, `.txt`, `.md`, `.c3p`,
etc.) are silently skipped. This prevents Kenney's `Visit Kenney.url` and
`Readme.txt` from polluting the asset database.

### Hot-reload

The file watcher should watch `.zip` files for modification (re-download or
replacement). When a zip changes, re-scan it and update the asset database.
This is lower priority than initial loading.

## Implementation

### Using PhysFS

PhysFS (`libraries/physfs/`) is already vendored and linked. It supports zip
reading natively via `PHYSFS_mount` and `PHYSFS_openRead`.

```cpp
// Mount the zip as a virtual directory.
PHYSFS_mount("assets/kenney.zip", /*mountPoint=*/nullptr, /*append=*/1);

// Enumerate files.
PHYSFS_enumerate("/", callback, userdata);

// Read a file from the zip.
PHYSFS_File* f = PHYSFS_openRead("Tilemap/tilemap_packed.png");
PHYSFS_sint64 len = PHYSFS_fileLength(f);
uint8_t* buf = allocator->Alloc(len, 1);
PHYSFS_readBytes(f, buf, len);
PHYSFS_close(f);

// Unmount when done.
PHYSFS_unmount("assets/kenney.zip");
```

### Changes to the packer

The asset packer lives in `src/packer.cc` and `src/cmd_run.cc`. The main
loop walks the directory, categorizes files by extension, and inserts them
into the SQLite database.

Add a step between directory walk and database insertion:

1. Collect all `.zip` files found during the walk.
2. For each zip, mount it with PhysFS.
3. Recursively enumerate contents, filtering by known extensions.
4. For each eligible file, check if a loose file with the same name already
   exists. If so, skip (loose files win).
5. Check if a file from a previously-processed zip has the same name.
   If so, skip and log a warning.
6. Read the file contents via PhysFS into a buffer.
7. Insert into the database using the same path the loose-file packer uses.
8. Unmount the zip.

### Phases

**Phase 1: Basic zip scanning**
- Scan for `.zip` files in the project directory
- Mount with PhysFS, enumerate contents
- Pack eligible files with flat names
- Loose files take priority over zip contents
- Log warnings on zip-to-zip collisions

**Phase 2: Hot-reload support**
- Watch `.zip` files for changes
- Re-scan changed zips and update the database

**Phase 3: Namespacing (if needed)**
- Optional `--namespace-zips` flag that prefixes asset names with the zip
  filename

## Decisions

1. **Flat names, not paths.** Asset names in the database are filenames only,
   not full paths. This matches how the engine already works for loose files
   and keeps Lua references short (`"tilemap_packed.png"` not
   `"Tilemap/tilemap_packed.png"`).

2. **Loose files always win.** This lets you override a zip asset by placing
   a file with the same name in the project directory. Useful for
   customizing one sprite from a pack without unpacking the whole thing.

3. **Alphabetical zip ordering for determinism.** When two zips collide,
   the result is deterministic (alphabetical filename order). No need for
   a priority config file.

4. **PhysFS over minizip/miniz.** PhysFS is already vendored, battle-tested,
   and provides a clean API. No new dependency needed.
