#include "file_watcher.h"

#include <cstring>

#include "clock.h"
#include "logging.h"
#include "stringlib.h"

#if defined(__linux__)
#include <dirent.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(_WIN32)
// TODO: implement ReadDirectoryChangesW + IOCP backend.
#elif defined(__APPLE__)
// TODO: implement FSEvents backend.
#endif

namespace G {

#if defined(__linux__)

namespace {

// True if the filename has an extension we care about for asset changes.
// Ignores editor temp files and OS metadata.
bool IsRelevantFile(const char* name) {
  if (name[0] == '.') return false;
  if (name[0] == '#') return false;
  std::string_view sv(name);
  if (HasSuffix(sv, "~")) return false;
  if (HasSuffix(sv, ".swp")) return false;
  if (HasSuffix(sv, ".swx")) return false;
  if (HasSuffix(sv, ".tmp")) return false;
  if (HasSuffix(sv, ".bak")) return false;
  return true;
}

}  // namespace

struct FileWatcher::PlatformData {
  int inotify_fd = -1;

  // Map watch descriptor to its relative directory path so we can reconstruct
  // full relative paths from inotify events (which only provide the basename).
  struct WatchEntry {
    int wd;
    PathBuffer dir_path;
  };

  // Raw array because FixedStringBuffer has no safe copy constructor
  // (StringBuffer base stores a char* that doesn't rebind on copy).
  static constexpr uint32_t kMaxWatches = 1024;
  WatchEntry watches[kMaxWatches];
  uint32_t watch_count = 0;

  // Root directory being watched (absolute path for inotify_add_watch).
  CmdBuffer root_path;

  // Event read buffer. Sized for ~100 events.
  static constexpr size_t kEventBufSize =
      128 * (sizeof(inotify_event) + NAME_MAX + 1);
  alignas(inotify_event) char event_buf[kEventBufSize];

  // Look up the relative directory path for a watch descriptor.
  const char* DirForWd(int wd) const {
    for (uint32_t i = 0; i < watch_count; ++i) {
      if (watches[i].wd == wd) return watches[i].dir_path.str();
    }
    return nullptr;
  }

  // Add a watch for a directory and record its relative path.
  void AddWatch(int fd, const char* abs_path, const char* rel_path) {
    int wd = inotify_add_watch(fd, abs_path,
                               IN_CLOSE_WRITE | IN_CREATE | IN_DELETE |
                                   IN_MOVED_TO | IN_MOVE_SELF | IN_ONLYDIR);
    if (wd < 0) {
      LOG("inotify_add_watch failed for ", abs_path, ": ", strerror(errno));
      return;
    }
    if (watch_count >= kMaxWatches) {
      LOG("Too many inotify watches (max ", kMaxWatches, ")");
      return;
    }
    watches[watch_count].wd = wd;
    watches[watch_count].dir_path.Set(rel_path);
    watch_count++;
  }

  // Remove watches associated with a deleted directory.
  void RemoveWatch(int fd, int wd) {
    inotify_rm_watch(fd, wd);
    for (uint32_t i = 0; i < watch_count; ++i) {
      if (watches[i].wd == wd) {
        watches[i].wd = watches[watch_count - 1].wd;
        watches[i].dir_path.Set(watches[watch_count - 1].dir_path.str());
        watch_count--;
        return;
      }
    }
  }

  // Recursively add watches for all subdirectories.
  void WatchRecursive(int fd, const char* abs_dir, const char* rel_dir) {
    AddWatch(fd, abs_dir, rel_dir);
    DIR* dir = opendir(abs_dir);
    if (dir == nullptr) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (entry->d_name[0] == '.') continue;
      if (entry->d_type != DT_DIR) continue;
      CmdBuffer abs_child(abs_dir, "/", entry->d_name);
      PathBuffer rel_child;
      if (rel_dir[0] == '\0') {
        rel_child.Set(entry->d_name);
      } else {
        rel_child.Set(rel_dir, "/", entry->d_name);
      }
      WatchRecursive(fd, abs_child.str(), rel_child.str());
    }
    closedir(dir);
  }
};

FileWatcher::FileWatcher(Allocator* allocator) : allocator_(allocator) {
  platform_ = allocator_->New<PlatformData>();
  platform_->inotify_fd = inotify_init1(IN_NONBLOCK);
  CHECK(platform_->inotify_fd >= 0,
        "Failed to initialize inotify: ", strerror(errno));
}

FileWatcher::~FileWatcher() {
  Stop();
  if (platform_->inotify_fd >= 0) {
    for (uint32_t i = 0; i < platform_->watch_count; ++i) {
      inotify_rm_watch(platform_->inotify_fd, platform_->watches[i].wd);
    }
    close(platform_->inotify_fd);
  }
  allocator_->Destroy(platform_);
}

void FileWatcher::Watch(const char* directory) {
  platform_->root_path.Set(directory);
  platform_->WatchRecursive(platform_->inotify_fd, directory, "");
  watching_ = true;
  LOG("FileWatcher: watching ", directory, " (", platform_->watch_count,
      " directories)");
}

void FileWatcher::CheckForEvents() {
  if (!watching_ || stopped_) return;

  ssize_t length = read(platform_->inotify_fd, platform_->event_buf,
                        sizeof(platform_->event_buf));
  if (length <= 0) {
    if (length < 0 && errno != EAGAIN) {
      LOG("[inotify] read error: ", strerror(errno));
    }
    return;
  }

  char* ptr = platform_->event_buf;
  while (ptr < platform_->event_buf + length) {
    auto* event = reinterpret_cast<const inotify_event*>(ptr);
    ptr += sizeof(inotify_event) + event->len;

    // Queue overflow — must do a full rescan.
    if (event->mask & IN_Q_OVERFLOW) {
      RequestFullRescan();
      continue;
    }

    // Skip events with no name (shouldn't happen for directory watches).
    if (event->len == 0) continue;

    const char* name = event->name;

    // A new subdirectory was created — add a recursive watch.
    if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
      const char* parent_rel = platform_->DirForWd(event->wd);
      if (parent_rel == nullptr) continue;
      CmdBuffer abs_path(platform_->root_path.str(), "/");
      if (parent_rel[0] != '\0') {
        abs_path.Append(parent_rel, "/");
      }
      abs_path.Append(name);
      PathBuffer rel_path;
      if (parent_rel[0] != '\0') {
        rel_path.Set(parent_rel, "/", name);
      } else {
        rel_path.Set(name);
      }
      platform_->WatchRecursive(platform_->inotify_fd, abs_path.str(),
                                rel_path.str());
      continue;
    }

    // A watched directory was moved/deleted — remove the watch.
    if (event->mask & IN_MOVE_SELF) {
      platform_->RemoveWatch(platform_->inotify_fd, event->wd);
      continue;
    }

    // Skip directory events that aren't creates (already handled above).
    if (event->mask & IN_ISDIR) continue;

    // Filter out editor temp files.
    if (!IsRelevantFile(name)) continue;

    // Build the relative path.
    const char* parent_rel = platform_->DirForWd(event->wd);
    if (parent_rel == nullptr) continue;

    PathBuffer rel_path;
    if (parent_rel[0] != '\0') {
      rel_path.Set(parent_rel, "/", name);
    } else {
      rel_path.Set(name);
    }

    PushToDebounce(rel_path.str());
  }
}

void FileWatcher::Stop() {
  stopped_ = true;
  watching_ = false;
}

#elif defined(_WIN32)

// TODO: Implement ReadDirectoryChangesW + IOCP backend.
//
// Implementation outline:
// - Open directory with CreateFileW(FILE_FLAG_BACKUP_SEMANTICS |
//   FILE_FLAG_OVERLAPPED)
// - Create I/O Completion Port with CreateIoCompletionPort
// - Issue ReadDirectoryChangesW with bWatchSubtree = TRUE (native recursive)
// - CheckForEvents: GetQueuedCompletionStatus with timeout 0 (non-blocking)
// - Parse FILE_NOTIFY_INFORMATION structs, convert wide filenames to UTF-8
// - On buffer overflow (lpBytesReturned == 0), set needs_full_rescan_
// - Filter: FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
//   FILE_NOTIFY_CHANGE_SIZE
//
// See design/File watching and hot reload.md for detailed pseudocode.

struct FileWatcher::PlatformData {};

FileWatcher::FileWatcher(Allocator* allocator) : allocator_(allocator) {
  platform_ = allocator_->New<PlatformData>();
}

FileWatcher::~FileWatcher() { allocator_->Destroy(platform_); }

void FileWatcher::Watch(const char* directory) {
  LOG("FileWatcher: Windows backend not implemented, using polling fallback");
  watching_ = true;
  needs_full_rescan_ = true;
}

void FileWatcher::CheckForEvents() {
  // Polling fallback: always request a full rescan. The background thread
  // will do a complete WriteAssetsToDb pass each cycle.
  if (watching_ && !stopped_) {
    needs_full_rescan_ = true;
  }
}

void FileWatcher::Stop() {
  stopped_ = true;
  watching_ = false;
}

#elif defined(__APPLE__)

// TODO: Implement FSEvents backend.
//
// Implementation outline:
// - Create FSEventStream with FSEventStreamCreate
// - Use flags: kFSEventStreamCreateFlagFileEvents |
//   kFSEventStreamCreateFlagNoDefer
// - Set latency to 0.1 seconds for natural debouncing
// - Schedule with CFRunLoop on the background thread, or use a dispatch queue
// - Events are advisory: verify with checksum before acting
// - Handle kFSEventStreamEventFlagMustScanSubDirs with full rescan
//
// See design/File watching and hot reload.md for detailed design.

struct FileWatcher::PlatformData {};

FileWatcher::FileWatcher(Allocator* allocator) : allocator_(allocator) {
  platform_ = allocator_->New<PlatformData>();
}

FileWatcher::~FileWatcher() { allocator_->Destroy(platform_); }

void FileWatcher::Watch(const char* directory) {
  LOG("FileWatcher: macOS backend not implemented, using polling fallback");
  watching_ = true;
  needs_full_rescan_ = true;
}

void FileWatcher::CheckForEvents() {
  // Polling fallback: always request a full rescan.
  if (watching_ && !stopped_) {
    needs_full_rescan_ = true;
  }
}

void FileWatcher::Stop() {
  stopped_ = true;
  watching_ = false;
}

#else
#error "Unsupported platform for FileWatcher"
#endif

void FileWatcher::PushToDebounce(const char* relative_path) {
  const Time now = Now();
  const uint32_t handle = StringIntern(relative_path);

  // Check if this file is already in the debounce queue.
  for (uint32_t i = 0; i < debounce_count_; ++i) {
    if (debounce_entries_[i].path_handle == handle) {
      debounce_entries_[i].last_event_time = now;
      return;
    }
  }

  // Add a new entry.
  if (debounce_count_ >= kMaxDebounceEntries) {
    LOG("[debounce] Queue full, requesting full rescan");
    RequestFullRescan();
    return;
  }

  debounce_entries_[debounce_count_].path_handle = handle;
  debounce_entries_[debounce_count_].first_event_time = now;
  debounce_entries_[debounce_count_].last_event_time = now;
  debounce_count_++;
}

void FileWatcher::RequestFullRescan() {
  needs_full_rescan_ = true;
  debounce_count_ = 0;
}

FileWatcher::ChangedFiles FileWatcher::DrainChanges() {
  ChangedFiles result = {};
  result.paths = drain_results_;

  // If a full rescan was requested, return that and clear.
  if (needs_full_rescan_) {
    needs_full_rescan_ = false;
    result.needs_full_rescan = true;
    debounce_count_ = 0;
    return result;
  }

  const Time now = Now();
  uint32_t remaining = 0;

  for (uint32_t i = 0; i < debounce_count_; ++i) {
    const Duration since_last = now - debounce_entries_[i].last_event_time;
    const Duration since_first = now - debounce_entries_[i].first_event_time;

    // File has settled (no events for settle period) or has exceeded the
    // maximum delay from its first event.
    if (since_last >= kSettlePeriod || since_first >= kMaxDelay) {
      if (result.count < kMaxDrainResults) {
        // Interned strings have stable pointers — no copy needed.
        auto sv = StringByHandle(debounce_entries_[i].path_handle);
        drain_results_[result.count] = sv.data();
        result.count++;
      }
    } else {
      // Not yet settled — keep in the queue.
      if (remaining != i) {
        debounce_entries_[remaining] = debounce_entries_[i];
      }
      remaining++;
    }
  }

  debounce_count_ = remaining;
  return result;
}

}  // namespace G
