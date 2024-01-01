#pragma once
// Needed because the lack of POSIX strcasecmp confuses the compiler.

#ifdef _WIN32
#define stracasecmp _stricmp
#else
inline int _internal_tolower(char c) { return (int)c - 'A' + 'a'; }
inline int strcasecmp(const char *s1, const char *s2) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  int result;
  if (p1 == p2) return 0;
  while ((result = _internal_tolower(*p1) - _internal_tolower(*p2++)) == 0)
    if (*p1++ == '\0') break;
  return result;
}
#endif