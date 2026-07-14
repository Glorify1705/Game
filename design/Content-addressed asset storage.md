---
status: implemented
tags: [assets, packaging, storage, sqlite]
---

# Content-Addressed Asset Storage

**Status: Implemented** (PR #92, July 2026). Supersedes the ZIP+index
proposals in [Asset system improvements](Asset%20system%20improvements.md)
and [Zip asset packs](Zip%20asset%20packs.md).

## Overview

Asset blobs no longer live as `contents BLOB` columns inside
`assets.sqlite3`. The database is metadata-only (name, type, size, source
hash, `blob_hash`, per-type parameters like image dimensions); the bytes
live in a content-addressed blob store keyed by rapidhash64, with entries
named as 16 lowercase hex characters.

- **Dev mode** (`game run`): loose files in
  `~/.cache/game/<project-hash>/blobs/`, written via temp-file + rename so
  concurrent readers never observe a partial blob. Identical content
  deduplicates for free. Unreferenced blobs are swept once at startup.
- **Packaged mode** (`game package`): a single `assets.zip` next to the
  binary, produced by a minimal STORE-only zip writer (`src/zip_writer.cc`,
  deterministic byte-for-byte output). PhysFS reads it at runtime.
- Both are PhysFS-mounted at `/blobs`; the runtime read path is a single
  `ReadBlob(hash, buffer, size)` (`src/blob_store.h`).

`asset_metadata.hash` remains the *source-file* checksum used by the
repack-skip and Lua bytecode-cache logic; `blob_hash` is the hash of the
*transcoded* content (QOI/QOI). The schema is versioned
(`PRAGMA user_version`, `kAssetDbSchemaVersion` in `src/packer.h`): dev
caches wipe and rebuild on mismatch; packaged games refuse to start with a
"re-run game package" error.

## Key files

`src/blob_store.{h,cc}`, `src/zip_writer.{h,cc}`, `src/assets.cc`,
`src/packer.cc`, `src/schema.sql(.h)`, `src/cmd_package.cc`
(two-phase packaging; blobs zipped in ascending-hash order for
reproducible archives).

## Why

Immutable, individually-addressable blobs enable: parallel/lazy asset
streaming without SQLite connection contention; HTTP-cacheable per-blob
delivery for the web target (content hash = perfect `Cache-Control:
immutable` semantics); delta patching (ship only new hashes); and
mmap-friendly packaged reads (STORE-only zip entries are contiguous
ranges).
