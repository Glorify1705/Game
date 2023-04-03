#pragma once
#ifndef _GAME_MEMORY_UNITS_H
#define _GAME_MEMORY_UNITS_H

#include <cstdint>

namespace G {

inline constexpr size_t Kilobytes(size_t n) { return 1024 * n; }
inline constexpr size_t Megabytes(size_t n) { return 1024 * Kilobytes(n); }
inline constexpr size_t Gigabytes(size_t n) { return 1024 * Megabytes(n); }

}  // namespace G

#endif  // _GAME_MEMORY_UNITS_H