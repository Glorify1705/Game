#pragma once
#ifndef _GAME_WEB_PLATFORM_H
#define _GAME_WEB_PLATFORM_H

// Web-only runtime services. web_platform.cc (compiled only on the web
// target) is the single translation unit that talks to the Emscripten API;
// desktop builds see inline no-ops so call sites need no guards.

namespace G {

#ifdef GAME_WEB
// Marks browser-persistent data (the IDBFS mount) dirty; MaybeSyncIdb
// flushes it to IndexedDB with a debounce, SyncIdbNow immediately.
void RequestIdbSync();
void MaybeSyncIdb();
void SyncIdbNow();

// Hands the per-frame callback to the browser (requestAnimationFrame
// pacing) and unwinds the current stack without running destructors —
// everything the callback touches must be heap-allocated.
[[noreturn]] void RunBrowserMainLoop(void (*frame)(void*), void* arg);

// Stops the browser main loop; no further frame callbacks fire.
void CancelBrowserMainLoop();
#else
inline void RequestIdbSync() {}
inline void MaybeSyncIdb() {}
inline void SyncIdbNow() {}
#endif

}  // namespace G

#endif  // _GAME_WEB_PLATFORM_H
