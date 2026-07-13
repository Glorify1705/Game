#pragma once
#ifndef _GAME_HOT_RELOAD_H
#define _GAME_HOT_RELOAD_H

#include <atomic>
#include <cstdint>
#include <mutex>

#include "allocators.h"
#include "blob_store.h"
#include "executor.h"
#include "file_watcher.h"
#include "libraries/sqlite3.h"

namespace G {

// Describes what kinds of assets changed during a hot-reload cycle.
struct HotReloadChanges {
  bool has_script_changes = false;
  bool has_audio_changes = false;
  uint32_t file_count = 0;
  // First few changed file paths for logging.
  static constexpr uint32_t kMaxLogFiles = 8;
  const char* files[kMaxLogFiles] = {};
};

// Owns the file-watcher background task, change detection, and the
// hot-reload arena. The main loop polls PendingChanges() each frame and
// drains the description via ConsumePendingChanges(). Actual subsystem
// reload (sound.StopAll, physics.Clear, assets->Load) stays in the
// Engine for now — this class is responsible only for *detecting* that
// a reload is needed.
class HotReloadManager {
 public:
  // blobs receives repacked asset contents; non-owning, may be null when
  // source_directory is null (packaged mode, no watching).
  HotReloadManager(const char* source_directory, sqlite3* db, BlobStore* blobs,
                   ThreadPoolExecutor* pool, Allocator* allocator);

  // Begin watching the source directory (if non-null) and submit the
  // watcher task to the pool. The pool must already be started.
  void Start();

  // Signal the watcher loop to exit. The thread is joined when the
  // caller shuts down the pool.
  void Stop();

  // Returns non-zero when there are pending changes ready to be
  // consumed from the main thread.
  int PendingChanges() const { return pending_changes_.load(); }

  // Reads and clears the pending reload description. Call only from
  // the main thread after PendingChanges() returns non-zero.
  HotReloadChanges ConsumePendingChanges();

 private:
  static bool StaticCheckChangedFiles(void* ctx);
  void CheckChangedFiles();

  const char* source_directory_;
  sqlite3* db_;
  BlobStore* blobs_;
  ThreadPoolExecutor* pool_;
  ArenaAllocator hotload_allocator_;
  FileWatcher watcher_;
  std::mutex mu_;
  bool stopped_ = false;
  std::atomic<int> pending_changes_{0};
  HotReloadChanges pending_reload_;
  Task watcher_task_;
};

}  // namespace G

#endif  // _GAME_HOT_RELOAD_H
