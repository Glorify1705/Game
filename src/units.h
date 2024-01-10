#pragma once
#ifndef _GAME_MEMORY_UNITS_H
#define _GAME_MEMORY_UNITS_H

#include <cstdint>

namespace G {

inline constexpr std::size_t Kilobytes(std::size_t n) { return 1024 * n; }
inline constexpr std::size_t Megabytes(std::size_t n) {
  return 1024 * Kilobytes(n);
}
inline constexpr std::size_t Gigabytes(std::size_t n) {
  return 1024 * Megabytes(n);
}

}  // namespace G

#endif  // _GAME_MEMORY_UNITS_H