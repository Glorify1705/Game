#pragma once
#ifndef _GAME_PACKER_H
#define _GAME_PACKER_H

#include <stddef.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <vector>

#include "allocators.h"
#include "assets.h"
#include "image.h"
#include "libraries/sqlite3.h"
#include "logging.h"
#include "units.h"

namespace G {

DbAssets* ReadAssetsFromDb(sqlite3* db, Allocator* allocator);

void WriteAssetsToDb(const char* source_directory, sqlite3* db,
                     Allocator* allocator);

}  // namespace G

#endif  // _GAME_PACKER_H
