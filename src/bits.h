#pragma once
#ifndef _GAME_BITS_H
#define _GAME_BITS_H

#include <cstdlib>

namespace G {

inline constexpr size_t NextPow2(size_t n) {
#ifdef _MSC_VER
  return n < 2 ? 1 : (~size_t{0} >> _lzcnt_u64(n - 1)) + 1;
#else
  return n < 2 ? 1 : (~size_t{0} >> __builtin_clzll(n - 1)) + 1;
#endif
}

inline constexpr size_t Log2(size_t b) {
  return 8 * sizeof(size_t) - __builtin_clzll(b);
}

inline static constexpr size_t Align(size_t n, size_t m) {
  return (n + m - 1) & ~(m - 1);
};

}  // namespace G

#endif  // _GAME_BITS_H
