#pragma once
#ifndef _GAME_PACKER_H
#define _GAME_PACKER_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <vector>

#include "allocators.h"
#include "assets_generated.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/idl.h"
#include "image.h"
#include "logging.h"
#include "units.h"
#include "assets.h"

namespace G {

Assets* PackFiles(const char* source_directory, Allocator* allocator);

Assets* ReadAssets(const char* assets_file, Allocator* allocator);

DbAssets* ReadAssetsFromDb(const char* assets_file, Allocator* allocator);

bool WriteAssets(const Assets& assets, const char* output_file);

void WriteAssetsToDb(const char* source_directory, const char* db_file, Allocator* allocator);

}  // namespace G

#endif  // _GAME_PACKER_H
