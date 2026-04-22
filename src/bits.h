#pragma once
#ifndef _GAME_BITS_H
#define _GAME_BITS_H

#include <cstdlib>

namespace G {

inline constexpr size_t NextPow2(size_t n) {
#ifdef __GNUC__
  return n < 2 ? 1 : (~size_t{0} >> __builtin_clzll(n - 1)) + 1;
#else
  return n < 2 ? 1 : (~size_t{0} >> _lzcnt_u64(n - 1)) + 1;
#endif
}

inline constexpr size_t Log2(size_t b) {
#ifdef __GNUC__
  return 8 * sizeof(size_t) - __builtin_clzll(b);
#else
  return 8 * sizeof(size_t) - _lzcnt_u64(b);
#endif
}

inline static constexpr size_t Align(size_t n, size_t m) {
  return (n + m - 1) & ~(m - 1);
};

}  // namespace G

#endif  // _GAME_BITS_H
