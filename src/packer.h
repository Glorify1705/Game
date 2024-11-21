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
#include "assets_generated.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/idl.h"
#include "image.h"
#include "logging.h"
#include "units.h"

namespace G {

DbAssets* ReadAssetsFromDb(const char* assets_file, Allocator* allocator);

void WriteAssetsToDb(const char* source_directory, const char* db_file,
                     Allocator* allocator);

}  // namespace G

#endif  // _GAME_PACKER_H
