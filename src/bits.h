#pragma once
#ifndef _GAME_BITS_H
#define _GAME_BITS_H

#include <cstdlib>

namespace G {

inline constexpr size_t NextPow2(size_t n) {
#if defined(__GNUC__) || defined(__clang__)
  return n < 2 ? 1 : (~size_t{0} >> __builtin_clzll(n - 1)) + 1;
#else
  return n < 2 ? 1 : (~size_t{0} >> __lzcnt64(n - 1)) + 1;
#endif
}

inline constexpr size_t Log2(size_t b) {
  // The clz builtins operate on 64-bit values regardless of the width of
  // size_t, so the bit count must match (size_t is 32-bit on wasm).
#if defined(__GNUC__) || defined(__clang__)
  return 8 * sizeof(unsigned long long) - __builtin_clzll(b);
#else
  return 8 * sizeof(unsigned long long) - __lzcnt64(b);
#endif
}

inline static constexpr size_t Align(size_t n, size_t m) {
  return (n + m - 1) & ~(m - 1);
};

}  // namespace G

#endif  // _GAME_BITS_H
