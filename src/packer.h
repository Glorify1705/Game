#pragma once
#ifndef _GAME_PACKER_H
#define _GAME_PACKER_H

#include <stddef.h>

#include "allocators.h"
#include "assets.h"
#include "blob_store.h"
#include "error.h"
#include "executor.h"
#include "libraries/sqlite3.h"

namespace G {

// Version of the asset database schema. Bump when the schema changes
// incompatibly; dev caches are wiped and rebuilt, packaged games refuse to
// start until re-packaged.
inline constexpr int kAssetDbSchemaVersion = 2;

struct AssetWriteResult {
  size_t written_files = 0;
};

// Packs all assets found under source_directory: metadata rows go into db,
// asset contents go into the blob store keyed by content hash.
ErrorOr<AssetWriteResult> WriteAssetsToDb(const char* source_directory,
                                          sqlite3* db, BlobStore* blobs,
                                          Allocator* allocator,
                                          Executor* executor);

// Creates the asset database schema. If the database was created by a
// different schema version, drops all asset tables and rebuilds them.
void InitializeAssetDb(sqlite3* db);

}  // namespace G

#endif  // _GAME_PACKER_H
