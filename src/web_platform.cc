#include "web_platform.h"

#include <emscripten.h>

namespace G {

namespace {

// Set when save data changed since the last IndexedDB flush.
bool g_idb_dirty = false;
double g_last_idb_sync_ms = 0;

}  // namespace

void RequestIdbSync() { g_idb_dirty = true; }

void SyncIdbNow() {
  g_idb_dirty = false;
  // Asynchronous persist of the IDBFS mount; errors only mean the data
  // stays in memory (e.g. private browsing), which the shell logs.
  EM_ASM({
    Module.FS.syncfs(
        false, function(err) {
          if (err) console.warn('Save sync failed:', err);
        });
  });
}

void MaybeSyncIdb() {
  if (!g_idb_dirty) return;
  const double now = emscripten_get_now();
  // Debounce so bursts of writes (e.g. saving every frame) become one
  // IndexedDB transaction every half second.
  if (now - g_last_idb_sync_ms < 500.0) return;
  g_last_idb_sync_ms = now;
  SyncIdbNow();
}

void RunBrowserMainLoop(void (*frame)(void*), void* arg) {
  emscripten_set_main_loop_arg(frame, arg, /*fps=*/0,
                               /*simulate_infinite_loop=*/true);
  // simulate_infinite_loop unwinds the stack; execution never gets here.
  __builtin_unreachable();
}

void CancelBrowserMainLoop() { emscripten_cancel_main_loop(); }

}  // namespace G
