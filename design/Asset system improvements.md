---
status: in-design
tags: [assets, packaging]
---

# Asset System Improvements

## Current system

The engine stores all assets in a single SQLite database (`assets.sqlite3`).
Each asset type has its own table (`images`, `audios`, `scripts`, `shaders`,
`fonts`, `text_files`) with a `contents BLOB` column holding the raw data.
An `asset_metadata` table tracks names, types, hashes, and processing order.
At pack time, images are re-encoded as QOI, audio as QOA, and everything else
is stored verbatim. The packer uses `rapidhash` checksums for incremental
rebuilds. At runtime, `DbAssets::Load()` reads every asset from the database
into memory at startup.

This works well for small games but has several scaling problems.

## Problems

**Everything loads at startup.** `DbAssets::Load()` iterates `asset_metadata`
and pulls every BLOB into memory. A 200 MB game loads 200 MB of assets before
the first frame. There is no lazy loading, streaming, or on-demand access.

**Large BLOBs are expensive in SQLite.** SQLite stores BLOBs inline in
B-tree pages (overflow pages for anything over ~1 KB). Reading a 4 MB texture
requires traversing overflow page chains. This is fine for metadata but
suboptimal for bulk data. The SQLite documentation itself recommends that
BLOBs larger than ~100 KB be stored as external files with paths in the
database.

**No compression on stored data.** Scripts, fonts, shaders, and text files
are stored uncompressed. QOI and QOA are compact formats but are not
general-purpose compressed (QOI can be larger than deflated PNG for some
images). There is no archive-level or per-entry compression.

**Incremental packing rewrites the database.** Changing one asset requires
rewriting the corresponding BLOB in SQLite, which triggers page-level I/O
across the database file. This is acceptable for small projects but slows
down as the database grows.

**No asset dependencies.** The system has a coarse processing order
(images before spritesheets before everything else) but no explicit
dependency graph. Spritesheets reference images by name via SQL, but there
is no general mechanism for expressing "asset A depends on asset B."

**No streaming or partial loading.** Audio is fully decoded into memory.
There is no support for streaming audio from disk (despite QOA's frame-based
structure being designed for this). Large textures cannot be loaded on demand.

## How other engines handle this

### Love2D: ZIP + PhysFS

Love2D's `.love` files are standard ZIP archives. PhysFS provides the virtual
filesystem layer, mounting archives and directories with priority ordering.
Assets are loaded by path on demand (no upfront bulk load). The format is
universally understood and tooling-friendly. No asset database, no dependency
tracking, no hot-reload -- simplicity at the cost of features.

### Godot: Custom archive + import database

Godot's `.pck` format is a flat archive: a header, a file index (path +
offset + size per entry), and raw file data. Files are stored uncompressed
(individual resources may use GPU texture compression, but that is per-type).
An import database (`.import` sidecar files) tracks metadata, dependencies,
and UID-based references that survive file renames. Supports async background
loading with progress polling.

### Defold: LZ4-compressed archive + manifest

Three files: `game.arcd` (data), `game.arci` (index with hash-sorted
entries for binary search), `game.dmanifest` (dependency graph and
cryptographic checksums). Each resource is individually LZ4-compressed.
Supports dynamic loading via factories, collection proxies, and a live
update system that downloads resources from CDN at runtime.

### Bevy: Loose files + asset processor

Assets live as loose files in an `assets/` directory. An `AssetProcessor`
transforms source assets into processed forms stored in `.imported_assets/`.
Strong/weak handles with generational indices manage lifetime. Built-in
hot-reload via filesystem watcher. Full async loading with dependency tracking
(assets only reach "loaded" state when all dependencies are also loaded).

### MonoGame: Content pipeline + .xnb

A build-time content pipeline converts source assets (PNG, WAV, FBX) into
optimized `.xnb` binary files via importer-processor-writer stages. Each
asset is one file. LZ4 compression. No runtime hot-reload, no streaming.
The FNA maintainer's recommendation: skip `.xnb` entirely and use
ZIP + PhysFS for distribution, with texture atlases and runtime font loading.

### Common patterns

| Concern | Typical approach |
|---------|-----------------|
| Storage | ZIP or flat archive with index, not database BLOBs |
| Index | Separate index/manifest with hashed paths for O(1) lookup |
| Compression | Per-entry LZ4 (fast decompression) or deflate (better ratio) |
| Lazy loading | Load-on-first-access with handle/future pattern |
| Streaming | Frame-based audio streaming, texture mip streaming |
| Dependencies | Explicit graph in manifest or sidecar metadata |
| Hot-reload | Filesystem watcher + handle invalidation |
| Modding/patching | Layered mount points (higher-priority archive overrides base) |

No mature engine stores bulk asset data as SQLite BLOBs. SQLite is
sometimes used as a metadata/index layer (Android's asset manager, some
proprietary engines) but the actual bytes live in flat files or archives.

## Proposed design: ZIP archive + SQLite index

Split the current single-database approach into two complementary parts:

1. **ZIP archive** (`assets.zip`) stores the actual file contents.
2. **SQLite database** (`assets.db`) stores metadata, the path index, and
   processed properties (sprite rects, audio sample counts, checksums, etc.).

This plays to each format's strength. ZIP is an efficient, well-understood
container for bulk data with per-entry deflate compression and random access
via the central directory. SQLite is an excellent structured metadata store
with transactional updates and query capability.

### Archive layout

```
assets.zip
├── images/
│   ├── player.qoi
│   ├── tileset.qoi
│   └── ...
├── audio/
│   ├── music.qoa
│   ├── jump.qoa
│   └── ...
├── scripts/
│   ├── main.lua
│   ├── game.lua
│   └── ...
├── shaders/
│   ├── default.vert
│   ├── default.frag
│   └── ...
├── fonts/
│   └── main.ttf
└── text/
    └── dialogue.json
```

### Database schema

```sql
-- Master asset index.
CREATE TABLE assets (
    id            INTEGER PRIMARY KEY,
    name          TEXT NOT NULL UNIQUE,
    type          TEXT NOT NULL,  -- 'image', 'audio', 'script', etc.
    archive_path  TEXT NOT NULL,  -- path inside the ZIP
    size          INTEGER NOT NULL,
    hash          INTEGER NOT NULL,  -- rapidhash for change detection
    processing_order INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_assets_name ON assets(name);
CREATE INDEX idx_assets_type ON assets(type);

-- Per-type metadata (no BLOBs).
CREATE TABLE image_meta (
    asset_id    INTEGER PRIMARY KEY REFERENCES assets(id),
    width       INTEGER NOT NULL,
    height      INTEGER NOT NULL,
    components  INTEGER NOT NULL
);

CREATE TABLE audio_meta (
    asset_id    INTEGER PRIMARY KEY REFERENCES assets(id),
    channels    INTEGER NOT NULL,
    samplerate  INTEGER NOT NULL,
    samples     INTEGER NOT NULL
);

CREATE TABLE sprite_meta (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    spritesheet TEXT NOT NULL,
    x           INTEGER NOT NULL,
    y           INTEGER NOT NULL,
    width       INTEGER NOT NULL,
    height      INTEGER NOT NULL
);

CREATE TABLE spritesheet_meta (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    image       TEXT NOT NULL,
    sprites     INTEGER NOT NULL,
    width       INTEGER NOT NULL,
    height      INTEGER NOT NULL
);

-- Development-only caches (not shipped in packaged builds).
-- The packer materializes these into ZIP files at package time.
CREATE TABLE compilation_cache (
    id          INTEGER PRIMARY KEY,
    source_name TEXT NOT NULL UNIQUE,
    source_hash INTEGER NOT NULL,
    compiled    BLOB
);

CREATE TABLE sdf_cache (
    id           INTEGER PRIMARY KEY,
    font_name    TEXT NOT NULL UNIQUE,
    font_hash    INTEGER NOT NULL,
    atlas_width  INTEGER NOT NULL,
    atlas_height INTEGER NOT NULL,
    glyph_metrics BLOB,
    atlas_bitmap  BLOB
);
```

### Processed asset lifecycle

Every asset type has a source format (what the developer provides), a
processed format (what the packer produces), and a runtime format (what the
engine actually consumes). The database stores metadata and development-time
caches. The ZIP stores final-form processed data ready for runtime
consumption.

**Images (PNG → QOI → GL texture)**

| Stage | Format | Where |
|-------|--------|-------|
| Source | PNG | Developer's `assets/` directory |
| Processed | QOI (lossless, ~3-4 bytes/pixel) | ZIP: `images/player.qoi` |
| Runtime | RGBA pixel buffer → GL_RGBA texture + mipmaps | GPU memory |

The packer decodes PNG with stb_image, re-encodes as QOI, and writes the
QOI file into the ZIP. The database stores only width, height, and
component count in `image_meta`. At runtime, the engine reads the QOI file
from the ZIP via PhysFS, decodes to RGBA, uploads to OpenGL, and frees the
CPU-side buffer. No PNG decoder needed at runtime.

**Audio (OGG/WAV → QOA)**

| Stage | Format | Where |
|-------|--------|-------|
| Source | OGG Vorbis or WAV (16-bit PCM) | Developer's `assets/` directory |
| Processed | QOA (3.2 bits/sample, frame-based) | ZIP: `audio/music.qoa` |
| Runtime | PCM float samples (decoded per-frame for streaming) | Audio buffer |

Same as images: the packer decodes to PCM and re-encodes as QOA. The
database stores channels, samplerate, and sample count in `audio_meta`.
QOA's frame structure (5120 samples/frame) means the runtime can stream
directly from the ZIP without loading the entire file.

**Fennel scripts (Fennel → Lua source)**

| Stage | Format | Where |
|-------|--------|-------|
| Source | `.fnl` (Fennel) | Developer's `assets/` directory |
| Dev cache | Compiled Lua source text | SQLite `compilation_cache` table |
| Packaged | Compiled Lua source text | ZIP: `scripts/game.lua` |
| Runtime | Lua VM bytecode (compiled by `luaL_loadbuffer`) | Lua VM |

During development, the Fennel compiler translates `.fnl` to Lua source
and caches the result in `compilation_cache` keyed by source hash. This
avoids re-running the Fennel compiler on every hot-reload when the source
hasn't changed. At package time, the packer compiles all Fennel scripts
and writes the resulting Lua source directly into the ZIP. The shipped game
has no Fennel compiler and no compilation cache -- only pre-compiled Lua
source files. Plain `.lua` scripts bypass compilation entirely in both modes.

**SDF font atlases (TTF → SDF atlas + metrics)**

| Stage | Format | Where |
|-------|--------|-------|
| Source | TTF/OTF | Developer's `assets/` directory |
| Dev cache | Atlas bitmap (R8) + glyph metrics (9 floats × 95 glyphs) | SQLite `sdf_cache` table |
| Packaged | Atlas bitmap file + metrics file | ZIP: `fonts/main.sdf_atlas`, `fonts/main.sdf_metrics` |
| Runtime | GL_R8 texture + glyph lookup table | GPU memory + CPU memory |

SDF generation is expensive (~100ms per font). During development, the
renderer generates the atlas on first use and caches it in `sdf_cache`
keyed by `rapidhash(font_data) ^ kSDFCacheVersion`. On subsequent runs,
the cached atlas loads directly. At package time, the packer either
generates the atlas itself or reads it from the development cache, then
writes the atlas bitmap and glyph metrics as separate files in the ZIP.
The shipped game never runs SDF generation -- it loads the pre-built atlas
and metrics directly from the archive.

The source TTF is still included in the ZIP for games that need runtime
font operations (e.g., rendering characters outside the cached ASCII
32-126 range). If only the pre-built atlas is needed, the TTF can be
omitted to save space.

**Shaders (GLSL source → GPU program)**

| Stage | Format | Where |
|-------|--------|-------|
| Source | `.vert` / `.frag` (GLSL text) | Developer's `assets/` directory |
| Packaged | GLSL text (same as source) | ZIP: `shaders/default.frag` |
| Runtime | GPU-compiled program | GPU memory |

Shaders are stored as-is. GPU shader compilation is driver-specific and
cannot be meaningfully cached across machines. The engine compiles shaders
from GLSL source at load time with `glCompileShader` / `glLinkProgram`.
This takes <1ms per shader and happens once per session, so there is no
benefit to caching.

**Spritesheets (JSON/XML metadata → sprite lookup table)**

| Stage | Format | Where |
|-------|--------|-------|
| Source | `.sprites.json` or `.sprites.xml` + atlas PNG | Developer's `assets/` directory |
| Packaged | Atlas image as QOI + sprite rects in database | ZIP: `images/atlas.qoi`, DB: `sprite_meta` + `spritesheet_meta` |
| Runtime | GL texture + sprite rect lookup | GPU + CPU memory |

The atlas image follows the normal image pipeline (PNG → QOI in ZIP). The
sprite rectangles are pure metadata (name, x, y, width, height) and stay
in the database's `sprite_meta` table. This is a good fit for SQLite:
the data is small, structured, and queried by name. No BLOBs involved.

### Development vs packaged mode

```
Development:
  Source files → Packer → SQLite DB (metadata + caches) → Runtime
                              ↑
                     Hot-reload re-packs changed files

Packaged:
  Source files → Packer → ZIP (processed data) + SQLite DB (metadata only)
                              ↓
                          Runtime reads from ZIP via PhysFS
```

In development, the SQLite database is the primary store. It holds both
metadata and caches (SDF atlases, compiled Fennel). The runtime loads
from the database as it does today.

At package time, the packer materializes all caches into files:
- Reads QOI image BLOBs from `images` table → writes to ZIP
- Reads QOA audio BLOBs from `audios` table → writes to ZIP
- Compiles all Fennel scripts → writes Lua source to ZIP
- Generates (or reads cached) SDF atlases → writes atlas + metrics to ZIP
- Copies shaders, fonts, text files → writes to ZIP
- Strips all BLOB columns and cache tables from the shipped database

The shipped database contains only the `assets`, `*_meta`, `sprite_meta`,
and `spritesheet_meta` tables. No BLOBs, no caches. Total size: a few KB
regardless of game size.

### PhysFS integration

PhysFS already supports mounting ZIP archives directly:

```cpp
PHYSFS_mount("assets.zip", "/assets", /*append=*/1);
```

After mounting, any asset can be read by virtual path:

```cpp
PHYSFS_File* f = PHYSFS_openRead("/assets/images/player.qoi");
int64_t len = PHYSFS_fileLength(f);
PHYSFS_readBytes(f, buffer, len);
PHYSFS_close(f);
```

This gives us the layered mount system for free. Development mode mounts
the source directory at higher priority than any archive, so loose files
override packed ones without any code changes:

```cpp
// Development: source files take priority.
PHYSFS_mount("assets/", "/assets", /*append=*/0);
// Packaged: archive only.
PHYSFS_mount("assets.zip", "/assets", /*append=*/1);
```

### Lazy loading

Replace the current "load everything at startup" with load-on-demand:

```cpp
// Returns a handle immediately. Actual data loads on first use.
AssetHandle<Image> handle = assets.Get<Image>("player");

// First access triggers the load from ZIP via PhysFS.
const Image* img = handle.Resolve();
```

The asset system maintains a table of loaded assets and their states
(unloaded, loading, loaded, failed). `Resolve()` checks the state and
loads from the archive if needed. This naturally spreads I/O across frames
instead of stalling on a loading screen.

For cases where upfront loading is wanted (e.g., loading screens), provide
an explicit preload API:

```cpp
assets.Preload({"player", "tileset", "music"});
assets.WaitForAll();
```

### Audio streaming

QOA's frame-based structure (5120 samples per frame, ~16 KB per stereo
frame) is designed for streaming. Instead of decoding the entire file into
memory, read and decode frames on demand:

```cpp
// Open a streaming audio source backed by the ZIP archive.
StreamHandle stream = audio.OpenStream("music");

// The audio callback reads frames as needed.
void AudioCallback(float* output, int frames) {
    stream.ReadFrames(output, frames);  // Decodes from ZIP on the fly.
}
```

This is particularly important for music tracks, which can be several MB
when fully decoded but only need ~32 KB of buffer for double-buffered
streaming.

### Compression strategy

ZIP supports per-entry compression method selection:

| Asset type | Compression | Rationale |
|-----------|-------------|-----------|
| QOI images | Store (no compression) | Already compact, decompresses fast |
| QOA audio | Store | Already compressed, streaming needs random access |
| Lua scripts | Deflate | Text compresses very well, small files |
| GLSL shaders | Deflate | Text, small files |
| Fonts (TTF) | Deflate | Binary, compresses ~30-40% |
| JSON/text | Deflate | Text compresses very well |

QOI and QOA are already in compact formats and don't benefit much from
deflate. Compressing them would also prevent efficient streaming (need to
decompress the entire entry to access any part). Text-based assets compress
very well with deflate and are small enough that decompression overhead is
negligible.

### Packing pipeline changes

The packer currently writes BLOBs into SQLite. The new pipeline:

1. Process source assets as before (PNG to QOI, OGG/WAV to QOA, etc.).
2. Write processed files into the ZIP archive with appropriate compression.
3. Write metadata into the SQLite database (paths, dimensions, checksums).
4. For incremental rebuilds, compare checksums and only update changed
   entries in both the ZIP and the database.

ZIP files support in-place updates (add/replace/remove entries) via
libraries like `miniz` or `libzip`. The central directory is rewritten
but file data can be appended without rewriting unchanged entries.

### Hot-reload changes

The current hot-reload system re-runs `WriteAssetsToDb()` on a background
thread when source files change. With the new system:

1. Background thread detects file changes (same as now).
2. Re-processes changed assets and updates the ZIP + database.
3. Sets `pending_changes_` (same as now).
4. Main thread invalidates affected asset handles and reloads from the
   updated archive.

In development mode, hot-reload can skip the ZIP entirely and read directly
from the mounted source directory (since loose files have higher mount
priority). This makes iteration faster: change a Lua script, the engine
picks it up directly from disk.

### Single-file packaging

The existing single-file design (append data to the binary) adapts cleanly.
Instead of appending a SQLite database, append both files:

```
[ engine binary ][ assets.zip ][ assets.db ][ zip_size: u64 ][ db_size: u64 ][ GAMEPAK\0 ]
```

At startup, extract offsets from the footer and mount the embedded ZIP
region using PhysFS's `PHYSFS_mountMemory` (or a file-offset-based reader)
and open the embedded SQLite via the custom VFS already described in the
single-file packaging design doc.

Alternatively, embed the SQLite data as a table inside the ZIP itself
(`meta/assets.db`), reducing the footer to a single archive.

### Modding and patching

The layered PhysFS mount system enables modding for free:

```cpp
// Base game.
PHYSFS_mount("assets.zip", "/assets", /*append=*/1);
// Patch (searched first).
PHYSFS_mount("patch_01.zip", "/assets", /*append=*/0);
// User mod (searched first).
PHYSFS_mount("mods/cool_mod.zip", "/assets", /*append=*/0);
```

A mod that replaces `images/player.qoi` just needs to include that file in
its own ZIP. PhysFS returns the first match in mount order. No engine code
changes needed. This is how Quake, Love2D, and many other engines support
mods.

## Migration path

The change can be made incrementally:

1. **Add ZIP writing to the packer** alongside the current SQLite BLOB
   writes. Both outputs are produced; the database is unchanged. This is
   a pure addition with no breaking changes.

2. **Add PhysFS-based asset loading** that reads from the ZIP archive for
   bulk data and from SQLite for metadata only. Gate behind a flag or
   detect automatically (if ZIP exists, use it; otherwise fall back to
   database BLOBs).

3. **Remove BLOB columns** from the asset tables once the ZIP path is
   stable. The database shrinks to a pure metadata store.

4. **Add lazy loading** now that assets are individually addressable in
   the archive rather than bulk-loaded from a database query.

5. **Add audio streaming** using PhysFS file handles for QOA frame-by-frame
   reads.

Each step is independently useful and testable. The old system keeps
working until the migration is complete.

## Rejected alternatives

**Custom flat archive (like Godot PCK or Quake PAK).** A custom format gives
maximum control but requires writing and maintaining an archiver, an
extractor, and documentation. ZIP is universally understood, inspectable
with standard tools, and PhysFS already supports it. The marginal
performance benefit of a custom format does not justify the maintenance
cost for a small engine.

**SQLite with `sqlar` table format.** SQLite's built-in archive format
uses a `sqlar` table with deflate-compressed BLOBs. This keeps everything
in one file but does not solve the fundamental problems: still no streaming,
still bulk BLOB reads through SQLite's pager, still no layered mounts.
Moves complexity sideways rather than reducing it.

**Drop SQLite entirely, use only ZIP.** Possible but loses the structured
query capability for metadata (sprite lookups, asset enumeration by type,
checksum queries) and the transactional cache tables. The hybrid approach
uses each tool where it is strongest.

**Per-file LZ4 instead of ZIP deflate.** LZ4 decompresses ~10x faster than
deflate but produces ~20% larger output. For the small text files that
benefit from compression in this engine, decompression speed is irrelevant
(files are < 100 KB). Deflate's better ratio is more useful here. LZ4 would
make sense for large assets that need fast runtime decompression, but QOI
and QOA already handle that for images and audio respectively.

## Decisions

- **ZIP writing library:** Use `miniz` (single-header, fits the vendoring
  approach). Its allocator calls need to be replaced with arena allocators
  to match the engine's memory discipline.

- **Caches live in SQLite during development, materialize to ZIP at package
  time.** The compilation cache (Fennel → Lua) and SDF cache (TTF → atlas)
  stay in the database for transactional updates and hash-based invalidation
  during development. The packer reads these caches and writes the results
  as files into the ZIP. The shipped database has no BLOBs and no cache
  tables. See "Processed asset lifecycle" for per-type details.

- **Development mode: always pack.** Keep the current approach: the packer
  always processes source assets into the database, and hot-reload re-runs
  the packer on changed files. No raw source mounting, no dual-format
  handling at runtime.