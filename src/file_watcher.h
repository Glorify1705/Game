#pragma once
#ifndef _GAME_FILE_WATCHER_H
#define _GAME_FILE_WATCHER_H

#include <cstdint>

#include "allocators.h"
#include "string_table.h"
#include "stringlib.h"

namespace G {

// Watches a directory tree for file changes and reports which files were
// modified. Platform-specific backends (inotify on Linux, ReadDirectoryChangesW
// on Windows, FSEvents on macOS) detect changes; a debounce layer coalesces
// rapid event bursts before reporting settled filenames.
//
// Usage:
//   FileWatcher watcher(allocator);
//   watcher.Watch("/path/to/assets");
//   // On background thread:
//   watcher.CheckForEvents();       // non-blocking, reads OS events
//   auto changes = watcher.DrainChanges();  // settled filenames
class FileWatcher {
 public:
  explicit FileWatcher(Allocator* allocator);
  ~FileWatcher();

  FileWatcher(const FileWatcher&) = delete;
  FileWatcher& operator=(const FileWatcher&) = delete;

  // Start watching a directory and all its subdirectories.
  void Watch(const char* directory);

  // Non-blocking: read OS events and feed them into the debounce queue.
  // Call from the background thread.
  void CheckForEvents();

  // Result of draining the debounce queue.
  struct ChangedFiles {
    // Relative paths of files that have settled (no events for the settle
    // period). Pointers are valid until the next call to DrainChanges().
    const char** paths;
    // Number of settled file paths.
    uint32_t count;
    // True if the watcher overflowed and a full rescan is needed.
    bool needs_full_rescan;
  };

  // Drain the debounce queue. Returns filenames that have settled (no events
  // for 100ms) or a full-rescan flag on overflow. The returned paths are
  // relative to the watched directory. Caller verifies via checksum.
  ChangedFiles DrainChanges();

  // Signal that watching should stop.
  void Stop();

  // True if the watcher experienced an overflow and a full directory rescan
  // should replace event-driven processing for this cycle.
  bool needs_full_rescan() const { return needs_full_rescan_; }

 private:
  struct DebounceEntry {
    // Interned handle for the relative path of the changed file.
    uint32_t path_handle;
    // Time of the first event for this file (seconds).
    double first_event_time;
    // Time of the most recent event for this file (seconds).
    double last_event_time;
  };

  // Debounce parameters.
  static constexpr double kSettlePeriodSec = 0.1;  // 100ms
  static constexpr double kMaxDelaySec = 0.5;      // 500ms
  static constexpr uint32_t kMaxDebounceEntries = 256;
  static constexpr uint32_t kMaxDrainResults = 64;

  // Add a file path to the debounce queue.
  void PushToDebounce(const char* relative_path);

  // Mark that a full rescan is needed (e.g. on overflow).
  void RequestFullRescan();

  Allocator* allocator_;
  bool watching_ = false;
  bool stopped_ = false;
  bool needs_full_rescan_ = false;

  // Debounce queue: files awaiting the settle period.
  DebounceEntry debounce_entries_[kMaxDebounceEntries];
  uint32_t debounce_count_ = 0;

  // Output buffer for DrainChanges(). Interned string pointers are stable,
  // so we only need to collect the const char* for each settled path.
  const char* drain_results_[kMaxDrainResults];

  // Platform-specific data. Defined in the .cc file per platform.
  struct PlatformData;
  PlatformData* platform_ = nullptr;
};

}  // namespace G

#endif  // _GAME_FILE_WATCHER_H
