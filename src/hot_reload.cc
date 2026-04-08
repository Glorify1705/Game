#include "hot_reload.h"

#include <string_view>

#include "clock.h"
#include "logging.h"
#include "packer.h"
#include "stringlib.h"
#include "thread.h"
#include "units.h"

namespace G {

namespace {

constexpr size_t kHotReloadMemory = Megabytes(128);

// Audio file extensions checked during hot-reload to decide whether
// sound.StopAll() is necessary.
constexpr std::string_view kAudioExtensions[] = {
    ".qoa",
    ".ogg",
    ".wav",
};

// Script file extensions checked during hot-reload to decide whether
// lua.LoadMain() + lua.Init() must re-run.
constexpr std::string_view kScriptExtensions[] = {
    ".lua",
    ".fnl",
};

bool HasAudioExtension(const char* path) {
  std::string_view sv(path);
  for (auto ext : kAudioExtensions) {
    if (HasSuffix(sv, ext)) return true;
  }
  return false;
}

bool HasScriptExtension(const char* path) {
  std::string_view sv(path);
  for (auto ext : kScriptExtensions) {
    if (HasSuffix(sv, ext)) return true;
  }
  return false;
}

// Classifies which asset categories (scripts, audio) are affected by a
// batch of file-watcher events, so the main thread knows what to reset
// on reload. A full rescan invalidates everything.
HotReloadChanges DescribePendingReload(
    const FileWatcher::ChangedFiles& changes) {
  HotReloadChanges result;
  result.file_count = changes.count;
  if (changes.needs_full_rescan) {
    result.has_script_changes = true;
    result.has_audio_changes = true;
    return result;
  }
  for (uint32_t i = 0; i < changes.count; ++i) {
    if (HasScriptExtension(changes.paths[i])) {
      result.has_script_changes = true;
    }
    if (HasAudioExtension(changes.paths[i])) {
      result.has_audio_changes = true;
    }
  }
  return result;
}

void LogChanges(const FileWatcher::ChangedFiles& changes) {
  if (changes.needs_full_rescan) {
    LOG("[hotload] Full rescan requested");
    return;
  }
  for (uint32_t i = 0; i < changes.count; ++i) {
    LOG("[hotload] File changed: ", changes.paths[i]);
  }
}

}  // namespace

HotReloadManager::HotReloadManager(const char* source_directory, sqlite3* db,
                                   ThreadPoolExecutor* pool,
                                   Allocator* allocator)
    : source_directory_(source_directory),
      db_(db),
      pool_(pool),
      hotload_allocator_(allocator, kHotReloadMemory),
      watcher_(allocator) {
  pending_changes_.store(0);
}

void HotReloadManager::Start() {
  if (source_directory_ != nullptr) {
    watcher_.Watch(source_directory_);
  }
  watcher_task_.fn = StaticCheckChangedFiles;
  watcher_task_.userdata = this;
  watcher_task_.cleanup = nullptr;
  pool_->Submit(&watcher_task_);
}

void HotReloadManager::Stop() {
  LockMutex l(mu_);
  stopped_ = true;
}

HotReloadChanges HotReloadManager::ConsumePendingChanges() {
  LockMutex l(mu_);
  HotReloadChanges result = pending_reload_;
  pending_reload_ = {};
  pending_changes_.store(0);
  return result;
}

bool HotReloadManager::StaticCheckChangedFiles(void* ctx) {
  auto* self = static_cast<HotReloadManager*>(ctx);
  self->CheckChangedFiles();
  return true;
}

void HotReloadManager::CheckChangedFiles() {
  SetCurrentThreadName("file-watcher");
  LOG("Background file watcher started");
  auto is_stopped = [this] {
    LockMutex l(mu_);
    return stopped_;
  };
  while (!is_stopped()) {
    if (source_directory_ == nullptr) {
      SleepMs(100);
      continue;
    }

    watcher_.CheckForEvents();
    auto changes = watcher_.DrainChanges();
    if (!changes.needs_full_rescan && changes.count == 0) {
      SleepMs(50);
      continue;
    }
    LogChanges(changes);

    hotload_allocator_.Reset();
    auto result =
        WriteAssetsToDb(source_directory_, db_, &hotload_allocator_, pool_);
    if (result.is_error()) {
      LOG("[hotload] WriteAssetsToDb failed: ", result.error().message());
      SleepMs(50);
      continue;
    }
    const size_t written = result.release_value().written_files;
    LOG("[hotload] WriteAssetsToDb wrote ", written, " file(s)");
    if (written == 0) {
      SleepMs(10);
      continue;
    }

    {
      LockMutex l(mu_);
      pending_reload_ = DescribePendingReload(changes);
      pending_changes_.store(static_cast<int>(written));
    }
    SleepMs(10);
  }
}

}  // namespace G
