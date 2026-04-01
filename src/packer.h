#pragma once
#ifndef _GAME_PACKER_H
#define _GAME_PACKER_H

#include <stddef.h>

#include "allocators.h"
#include "assets.h"
#include "error.h"
#include "executor.h"
#include "libraries/sqlite3.h"

namespace G {

ErrorOr<DbAssets*> ReadAssetsFromDb(sqlite3* db, Allocator* allocator,
                                    Allocator* asset_allocator);

struct AssetWriteResult {
  size_t written_files = 0;
};

ErrorOr<AssetWriteResult> WriteAssetsToDb(const char* source_directory,
                                          sqlite3* db, Allocator* allocator,
                                          Executor* executor);

void InitializeAssetDb(sqlite3* db);

}  // namespace G

#endif  // _GAME_PACKER_H
