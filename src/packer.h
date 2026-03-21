#pragma once
#ifndef _GAME_PACKER_H
#define _GAME_PACKER_H

#include <stddef.h>

#include "allocators.h"
#include "assets.h"
#include "libraries/sqlite3.h"

namespace G {

DbAssets* ReadAssetsFromDb(sqlite3* db, Allocator* allocator);

struct AssetWriteResult {
  size_t written_files = 0;
};

AssetWriteResult WriteAssetsToDb(const char* source_directory, sqlite3* db,
                                 Allocator* allocator);

void InitializeAssetDb(sqlite3* db);

}  // namespace G

#endif  // _GAME_PACKER_H
