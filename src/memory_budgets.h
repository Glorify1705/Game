#pragma once
#ifndef _GAME_MEMORY_BUDGETS_H
#define _GAME_MEMORY_BUDGETS_H

#include <cstddef>

#include "units.h"

namespace G {

// Fixed memory budgets for every subsystem, per platform. The engine is
// memory-deterministic: each budget is reserved once at startup and never
// grows. The web target runs in a wasm32 address space with a fixed
// 512 MB linear memory (see -sINITIAL_MEMORY in CMakeLists.txt), so its
// budgets are scaled down accordingly.
//
// The Lua, frame, and renderer budgets are sub-allocations of the engine
// arena; the third-party heap, CLI arena, and SQLite heap are separate.
#ifdef GAME_WEB
inline constexpr size_t kEngineArenaSize = Megabytes(256);
inline constexpr size_t kLuaArenaSize = Megabytes(96);
inline constexpr size_t kFrameArenaSize = Megabytes(64);
inline constexpr size_t kRenderCommandMemory = Megabytes(24);
inline constexpr size_t kRenderScratchSize = Megabytes(24);
inline constexpr size_t kThirdPartyHeapSize = Megabytes(32);
inline constexpr size_t kCliArenaSize = Megabytes(32);
inline constexpr size_t kSqliteHeapSize = Megabytes(16);
#else
inline constexpr size_t kEngineArenaSize = Gigabytes(4);
inline constexpr size_t kLuaArenaSize = Megabytes(256);
inline constexpr size_t kFrameArenaSize = Megabytes(128);
inline constexpr size_t kRenderCommandMemory = Megabytes(64);
inline constexpr size_t kRenderScratchSize = Megabytes(64);
inline constexpr size_t kThirdPartyHeapSize = Megabytes(64);
inline constexpr size_t kCliArenaSize = Gigabytes(1);
inline constexpr size_t kSqliteHeapSize = Megabytes(32);
#endif

}  // namespace G

#endif  // _GAME_MEMORY_BUDGETS_H
