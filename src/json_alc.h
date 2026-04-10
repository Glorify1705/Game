#pragma once
#ifndef _GAME_JSON_ALC_H
#define _GAME_JSON_ALC_H

#include <string_view>

#include "allocators.h"
#include "libraries/yyjson.h"

namespace G {

extern "C" inline void* YyjsonArenaMalloc(void* ctx, size_t size) {
  return static_cast<ArenaAllocator*>(ctx)->Alloc(size, kMaxAlign);
}

extern "C" inline void* YyjsonArenaRealloc(void* ctx, void* ptr,
                                           size_t old_size, size_t size) {
  return static_cast<ArenaAllocator*>(ctx)->Realloc(ptr, old_size, size,
                                                    kMaxAlign);
}

extern "C" inline void YyjsonArenaFree(void* /*ctx*/, void* /*ptr*/) {}

// Adapter that lets yyjson allocate from one of the engine's arenas. Every
// call site uses an arena that is reset wholesale after the parse, so the
// `free` callback is a no-op: yyjson never holds memory across the reset,
// and arenas do not reuse individual deallocations.
inline yyjson_alc MakeYyjsonAlc(ArenaAllocator* arena) {
  yyjson_alc alc;
  alc.malloc = YyjsonArenaMalloc;
  alc.realloc = YyjsonArenaRealloc;
  alc.free = YyjsonArenaFree;
  alc.ctx = arena;
  return alc;
}

// Parse a JSON string using the given arena allocator. Returns the root value
// on success, or nullptr on failure (with err populated).
inline yyjson_doc* ReadJson(ArenaAllocator* arena, const char* data, size_t len,
                            yyjson_read_err* err) {
  yyjson_alc alc = MakeYyjsonAlc(arena);
  return yyjson_read_opts(const_cast<char*>(data), len, YYJSON_READ_NOFLAG,
                          &alc, err);
}

inline yyjson_doc* ReadJson(ArenaAllocator* arena, std::string_view str,
                            yyjson_read_err* err) {
  return ReadJson(arena, str.data(), str.size(), err);
}

// View the string payload of a yyjson value as a std::string_view.
// The view is valid for as long as the owning yyjson_doc (and its arena)
// lives.
inline std::string_view YyjsonStrView(yyjson_val* v) {
  return std::string_view(yyjson_get_str(v), yyjson_get_len(v));
}

}  // namespace G

#endif  // _GAME_JSON_ALC_H
