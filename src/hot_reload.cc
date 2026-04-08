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

    bool should_process = false;
    if (changes.needs_full_rescan) {
      LOG("[hotload] Full rescan requested");
      should_process = true;
    } else if (changes.count > 0) {
      for (uint32_t i = 0; i < changes.count; ++i) {
        LOG("[hotload] File changed: ", changes.paths[i]);
      }
      should_process = true;
    }

    if (should_process) {
      hotload_allocator_.Reset();
      auto result =
          WriteAssetsToDb(source_directory_, db_, &hotload_allocator_, pool_);
      if (result.is_error()) {
        LOG("[hotload] WriteAssetsToDb failed: ", result.error().message());
        SleepMs(50);
        continue;
      }
      size_t written = result.release_value().written_files;
      LOG("[hotload] WriteAssetsToDb wrote ", written, " file(s)");
      if (written > 0) {
        LockMutex l(mu_);
        pending_reload_.file_count = changes.count;
        pending_reload_.has_script_changes = false;
        pending_reload_.has_audio_changes = false;
        if (changes.needs_full_rescan) {
          pending_reload_.has_script_changes = true;
          pending_reload_.has_audio_changes = true;
        } else {
          for (uint32_t i = 0; i < changes.count; ++i) {
            if (HasScriptExtension(changes.paths[i])) {
              pending_reload_.has_script_changes = true;
            }
            if (HasAudioExtension(changes.paths[i])) {
              pending_reload_.has_audio_changes = true;
            }
          }
        }
        pending_changes_.store(static_cast<int>(written));
      }
    }

    // Sleep longer when idle (no events), shorter when active.
    SleepMs(changes.count > 0 ? 10 : 50);
  }
}

}  // namespace G
