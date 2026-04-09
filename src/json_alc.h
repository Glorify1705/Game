#pragma once
#ifndef _GAME_JSON_ALC_H
#define _GAME_JSON_ALC_H

#include "allocators.h"
#include "libraries/yyjson.h"

namespace G {

namespace json_alc_detail {

extern "C" inline void* YyjsonArenaMalloc(void* ctx, size_t size) {
  return static_cast<Allocator*>(ctx)->Alloc(size, kMaxAlign);
}

extern "C" inline void* YyjsonArenaRealloc(void* ctx, void* ptr,
                                           size_t old_size, size_t size) {
  return static_cast<Allocator*>(ctx)->Realloc(ptr, old_size, size, kMaxAlign);
}

extern "C" inline void YyjsonArenaFree(void* /*ctx*/, void* /*ptr*/) {}

}  // namespace json_alc_detail

// Adapter that lets yyjson allocate from one of the engine's arenas. Every
// call site uses an arena that is reset wholesale after the parse, so the
// `free` callback is a no-op: yyjson never holds memory across the reset,
// and arenas do not reuse individual deallocations.
inline yyjson_alc MakeYyjsonAlc(Allocator* arena) {
  yyjson_alc alc;
  alc.malloc = json_alc_detail::YyjsonArenaMalloc;
  alc.realloc = json_alc_detail::YyjsonArenaRealloc;
  alc.free = json_alc_detail::YyjsonArenaFree;
  alc.ctx = arena;
  return alc;
}

}  // namespace G

#endif  // _GAME_JSON_ALC_H
