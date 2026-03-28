
# File Watching and Hot Reload

This document analyzes the engine's current hot-reload architecture, proposes a
Windows implementation using `ReadDirectoryChangesW`, and outlines improvements
to the file watching system on all platforms.

## Current architecture

### Overview

Hot-reload works via a background thread that polls the asset directory every
10ms, computing rapidhash checksums for every file and comparing against
stored values in the SQLite asset database. When changes are detected, an
atomic integer signals the main thread, which reloads assets and re-executes
Lua scripts.

The inotify `Filewatcher` class exists on Linux but its events are read and
**discarded**. Change detection happens entirely through checksum comparison.
The file watcher is vestigial.

### Key files

```
src/game.cc:110-155    Filewatcher class (inotify on Linux, stub on Windows)
src/game.cc:199-221    CheckChangedFiles() background thread loop
src/game.cc:392-396    Reload() — clears timers, stops audio, loads assets
src/game.cc:536-543    Main loop hot-reload check point
src/packer.cc:698-740  Checksum comparison and asset change detection
src/assets.cc:220-282  DbAssets::Load() — reload only changed assets
src/lua.cc:799-825     LoadScript() — clears package.loaded, re-requires
src/lua.cc:898-925     LoadMain() — re-executes main.lua, calls init()
```

### Flow

```
[Background thread — every 10ms]
  hotload_allocator_.Reset()
  WriteAssetsToDb(source_directory, db, &hotload_allocator_)
    PHYSFS_mount(source_directory)
    DbPacker::LoadChecksums() — load old hashes from asset_metadata
    HandleFiles() — enumerate all files in /assets
      for each file:
        read contents, compute rapidhash
        compare with stored hash
        if different: encode image/audio, write to SQLite
    return count of written files
  SDL_SetAtomicInt(&pending_changes_, written_files)

[Main thread — checked every frame]
  if PendingChanges() > 0:
    lua.ClearError()
    Reload()
      timers.Clear()
      sound.StopAll()
      assets->Load()        — reload only changed assets
    lua.LoadMain()           — re-execute main.lua
    lua.Init()               — call _Game.init()
    MarkChangesAsProcessed() — reset atomic to 0
```

### What the Filewatcher does today

```cpp
// Linux only — stub on Windows
class Filewatcher {
  Filewatcher(sqlite3*) {
    file_descriptor_ = inotify_init1(IN_NONBLOCK);
  }
  void Watch(const char* directory) {
    inotify_add_watch(fd, directory, IN_MODIFY | IN_CREATE | IN_DELETE);
  }
  void CheckForEvents() {
    read(fd, events_, sizeof(events_));
    // iterate events but DO NOTHING with them
  }
};
```

The watcher is created in `Init()` (game.cc:520-522) when `hotreload` is
enabled, but `CheckForEvents()` is never called from the background thread.
The background thread uses `WriteAssetsToDb()` (pure checksum comparison)
instead.

### Why the current approach works but is wasteful

The checksum-based polling approach is **correct and cross-platform**: it works
on Linux, macOS, and Windows without platform-specific code. However, it:

1. **Re-reads every file every 10ms.** For a project with 200 asset files
   totaling 50 MB, this means ~5 GB/sec of reads from the OS page cache. The
   CPU cost of hashing is low (rapidhash is fast), but the I/O syscall
   overhead and cache pressure are not.

2. **Cannot detect which file changed.** `WriteAssetsToDb` returns a count of
   changed files, not which ones. The asset system must re-scan the entire
   database to find what changed.

3. **Has a fixed 10ms poll interval.** This is fast enough for interactive
   development but wastes CPU when the developer is not actively editing.

4. **Processes all assets even when only Lua files change.** A one-line Lua
   edit triggers image and audio re-encoding checks for every asset.

## Platform APIs

### Linux: inotify

**API**: `inotify_init1()`, `inotify_add_watch()`, `read()` on the fd.

**Event types**: `IN_MODIFY`, `IN_CREATE`, `IN_DELETE`, `IN_MOVED_FROM`,
`IN_MOVED_TO`, `IN_CLOSE_WRITE`, `IN_ATTRIB`, `IN_DELETE_SELF`,
`IN_MOVE_SELF`.

**Recursive watching**: Not supported natively. Must manually add a watch for
every subdirectory and handle `IN_CREATE | IN_ISDIR` to watch new
subdirectories. Race condition: files may appear in a new directory before the
watch is established.

**Scalability**: ~1 KB kernel memory per watch. Default `max_user_watches` is
8,192 (modern kernels auto-scale up to ~1M). Shared across all processes for
a user — IDEs like VS Code and IntelliJ consume watches too.

**Gotchas**:
- Watches are inode-based. Atomic saves (write temp + rename) cause the watch
  to follow the old inode, not the new file at the same path. Fix: watch the
  directory, not individual files.
- The kernel coalesces identical consecutive events (same wd, mask, cookie,
  name).
- Does not work on network filesystems (NFS, CIFS).
- Queue overflow (`IN_Q_OVERFLOW`) means events are lost; must rescan.

**Preferred event**: `IN_CLOSE_WRITE` is better than `IN_MODIFY` for
detecting completed file saves. `IN_MODIFY` fires on every `write()` syscall
during a save; `IN_CLOSE_WRITE` fires once when the file is closed after
writing. This provides natural debouncing for large file writes.

### Windows: ReadDirectoryChangesW

**API**: Open directory with `CreateFileW(FILE_FLAG_BACKUP_SEMANTICS |
FILE_FLAG_OVERLAPPED)`, then `ReadDirectoryChangesW()` with an OVERLAPPED
structure. Results arrive as `FILE_NOTIFY_INFORMATION` structs (linked list in
a buffer).

**Completion models** (4 options):
1. Synchronous (blocking) — impractical for multiple directories.
2. `WaitForMultipleObjects` + `GetOverlappedResult` — limited to 64 handles.
3. Completion routines (APCs) — requires alertable wait states.
4. **I/O Completion Ports (IOCP)** — recommended. Single thread, single port,
   scales to hundreds of directories.

**Filter flags**: `FILE_NOTIFY_CHANGE_FILE_NAME`, `FILE_NOTIFY_CHANGE_SIZE`,
`FILE_NOTIFY_CHANGE_LAST_WRITE`. Use the minimum set to reduce noise.

**Action types**: `FILE_ACTION_ADDED`, `FILE_ACTION_REMOVED`,
`FILE_ACTION_MODIFIED`, `FILE_ACTION_RENAMED_OLD_NAME`,
`FILE_ACTION_RENAMED_NEW_NAME`.

**Recursive watching**: Natively supported via `bWatchSubtree = TRUE`. This is
a major advantage over inotify.

**Gotchas**:
- Buffer overflow: if the buffer is too small, `ReadDirectoryChangesW` returns
  TRUE but `lpBytesReturned == 0` and all events are lost. Must rescan.
- Network paths: buffer must be <= 64 KB due to SMB protocol limitations.
- Large buffers consume non-paged kernel memory.
- May return 8.3 short filenames unpredictably. `GetLongPathNameW()` needed
  for normalization, but fails for deleted files.
- Closing: notifications arrive asynchronously after `CloseHandle()` — buffers
  cannot be freed until the final notification arrives.

### macOS: FSEvents

**API**: `FSEventStreamCreate()` + `FSEventStreamScheduleWithRunLoop()` +
`FSEventStreamStart()`.

**Recursive watching**: Natively recursive. Watch a path, get events for the
entire subtree.

**Latency parameter**: Controls how long FSEvents waits before delivering
events (e.g. 0.1 seconds). Multiple rapid changes are coalesced.
`kFSEventStreamCreateFlagNoDefer` delivers the first event immediately.

**File-level granularity**: Requires `kFSEventStreamCreateFlagFileEvents`
(macOS 10.7+). Without it, events are directory-level only.

**Gotchas**:
- Events are advisory, not definitive — must verify by checking actual file
  state.
- `kFSEventStreamEventFlagMustScanSubDirs` forces expensive recursive rescans
  when events are coalesced or dropped.
- Requires CFRunLoop integration (or dispatch queue on newer macOS).

### macOS: kqueue

**API**: `kqueue()` + `kevent()`. Watch files opened with `O_EVTONLY`.

**Not recommended** for directory trees: requires one fd per watched file,
hits system `maxfiles` limit (~10,240 on macOS). FSEvents is strongly
preferred.

### Summary

| Feature | inotify | ReadDirectoryChangesW | FSEvents |
|---|---|---|---|
| Platform | Linux | Windows | macOS |
| Recursive watching | Manual | Native (`bWatchSubtree`) | Native |
| Granularity | File-level | File-level | File-level (with flag) |
| Completion model | poll/epoll on fd | IOCP | CFRunLoop/dispatch |
| Overflow recovery | `IN_Q_OVERFLOW` event | `lpBytesReturned == 0` | `MustScanSubDirs` flag |
| Coalescing | Identical consecutive events | None (manual) | Latency-based |
| Atomic saves | Watch directory, not file | Works correctly | Works correctly |
| Network FS | Does not work | Works (64 KB buffer limit) | Works |

## Editor interaction and atomic saves

Editors save files in different ways, which affects what events fire:

| Editor | Save strategy | Events |
|---|---|---|
| Vim (default) | Rename original to backup, write new file | Breaks inode-based watches |
| Vim (backupcopy=yes) | Copy to backup, overwrite original | `MODIFY`, `CLOSE_WRITE` |
| Emacs | Often rename-based | Can break watches |
| VS Code | Direct write (usually) | `MODIFY`, `CLOSE_WRITE` |
| Sublime Text 3 | Write to temp, rename | `MOVED_TO` only |
| Kate | Write to `.new` temp, rename | `CREATE`, `MOVED_TO` |
| Nano | Direct write | `MODIFY`, `CLOSE_WRITE` |

**Design implication**: Always watch directories, never individual files. A
directory watch sees `IN_MOVED_TO`/`IN_CREATE` when a file is replaced via
rename, regardless of editor strategy.

## Proposed design

### Architecture: event-driven with checksum verification

Replace the pure-polling model with an event-driven model where the file
watcher tells the background thread *which files changed*. The background
thread then verifies the change via checksum and processes only the affected
files.

```
[File watcher thread]
  Platform-specific watcher detects change to "sprites/player.png"
  Push filename into lock-free queue

[Background thread]
  Drain queue → get set of changed filenames
  For each changed file:
    read file, compute rapidhash
    compare with stored hash
    if different: process and write to SQLite
  SDL_SetAtomicInt(&pending_changes_, count)

[Main thread — unchanged]
  if PendingChanges() > 0: reload
```

The checksum verification step is retained because:
1. File watchers may report spurious events (editor temp files, metadata
   changes, incomplete writes).
2. A file may be written multiple times in quick succession — the checksum
   confirms the final state differs from the stored version.
3. It provides a fallback correctness guarantee regardless of platform quirks.

### Debouncing

Editors generate bursts of events for a single logical save (e.g. Vim:
attrib + move_self + delete_self + create + modify). The watcher should
debounce before notifying the background thread:

- **Settle period**: 100ms after the last event for a given file before
  pushing it to the queue. This handles multi-event editor saves without
  adding perceptible latency.
- **Maximum delay**: 500ms from the first event. Ensures changes are
  processed even during continuous event streams (e.g. `rsync` copying many
  files).
- **Implementation**: Per-file timestamp of last event in a dictionary. A
  timer thread (or the watcher thread itself) sweeps the dictionary every
  50ms, pushing settled files to the queue.

### Platform implementations

#### Linux: inotify (fix existing code)

The existing `Filewatcher` class needs three changes:

1. **Actually use the events.** Parse `inotify_event` structs and extract
   filenames. Push changed filenames into the debounce queue.

2. **Watch subdirectories recursively.** Walk the asset directory tree at
   startup and `inotify_add_watch` each subdirectory. Handle `IN_CREATE |
   IN_ISDIR` to add watches on new subdirectories. Handle `IN_DELETE_SELF` to
   clean up watches on removed directories.

3. **Use `IN_CLOSE_WRITE` instead of `IN_MODIFY`.** `IN_CLOSE_WRITE` fires
   once per save instead of once per `write()` syscall, providing natural
   debouncing. Keep `IN_CREATE`, `IN_DELETE`, `IN_MOVED_TO` for new files,
   deletions, and atomic saves.

   New mask: `IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_ISDIR`

4. **Handle overflow.** On `IN_Q_OVERFLOW`, fall back to a full directory
   rescan (push all filenames into the queue).

#### Windows: ReadDirectoryChangesW

New `Filewatcher` implementation for `_WIN32`:

```cpp
class Filewatcher {
 public:
  explicit Filewatcher(sqlite3* db);
  ~Filewatcher();

  void Watch(const char* directory);
  void CheckForEvents();
  void Stop();

 private:
  // IOCP-based watching
  HANDLE directory_handle_ = INVALID_HANDLE_VALUE;
  HANDLE iocp_ = nullptr;
  OVERLAPPED overlapped_ = {};
  alignas(DWORD) char buffer_[32768];  // 32 KB notification buffer
  bool watching_ = false;

  void IssueRead();
  void ProcessNotifications(DWORD bytes_transferred);
};
```

**Implementation outline**:

```cpp
Filewatcher::Filewatcher(sqlite3* db) {
  // IOCP will be created in Watch()
}

void Filewatcher::Watch(const char* directory) {
  // Convert UTF-8 path to wide string
  wchar_t wide_path[MAX_PATH];
  MultiByteToWideChar(CP_UTF8, 0, directory, -1, wide_path, MAX_PATH);

  // Open directory handle
  directory_handle_ = CreateFileW(
      wide_path,
      FILE_LIST_DIRECTORY,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
      nullptr);
  CHECK(directory_handle_ != INVALID_HANDLE_VALUE,
        "Failed to open directory for watching: ", directory);

  // Create IOCP and associate directory handle
  iocp_ = CreateIoCompletionPort(directory_handle_, nullptr, 0, 1);
  CHECK(iocp_ != nullptr, "Failed to create IOCP");

  IssueRead();
  watching_ = true;
}

void Filewatcher::IssueRead() {
  memset(&overlapped_, 0, sizeof(overlapped_));
  BOOL ok = ReadDirectoryChangesW(
      directory_handle_,
      buffer_,
      sizeof(buffer_),
      TRUE,   // bWatchSubtree — recursive
      FILE_NOTIFY_CHANGE_FILE_NAME |
      FILE_NOTIFY_CHANGE_LAST_WRITE |
      FILE_NOTIFY_CHANGE_SIZE,
      nullptr,
      &overlapped_,
      nullptr);
  CHECK(ok, "ReadDirectoryChangesW failed");
}

void Filewatcher::CheckForEvents() {
  DWORD bytes_transferred = 0;
  ULONG_PTR key = 0;
  LPOVERLAPPED ov = nullptr;

  // Non-blocking check: timeout = 0
  BOOL ok = GetQueuedCompletionStatus(
      iocp_, &bytes_transferred, &key, &ov, /*timeout_ms=*/0);

  if (!ok || ov == nullptr) return;  // No events

  if (bytes_transferred == 0) {
    // Buffer overflow — must rescan everything
    // Push sentinel to debounce queue indicating full rescan needed
    IssueRead();
    return;
  }

  ProcessNotifications(bytes_transferred);
  IssueRead();  // Re-arm for next batch
}

void Filewatcher::ProcessNotifications(DWORD bytes_transferred) {
  char* ptr = buffer_;
  for (;;) {
    auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(ptr);

    // Convert wide filename to UTF-8
    char utf8_name[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0,
                        info->FileName,
                        info->FileNameLength / sizeof(WCHAR),
                        utf8_name, MAX_PATH,
                        nullptr, nullptr);

    // Push to debounce queue based on action type
    switch (info->Action) {
      case FILE_ACTION_ADDED:
      case FILE_ACTION_MODIFIED:
      case FILE_ACTION_RENAMED_NEW_NAME:
        // Push utf8_name to changed-files queue
        break;
      case FILE_ACTION_REMOVED:
      case FILE_ACTION_RENAMED_OLD_NAME:
        // Push utf8_name to removed-files queue (if we track deletions)
        break;
    }

    if (info->NextEntryOffset == 0) break;
    ptr += info->NextEntryOffset;
  }
}

Filewatcher::~Filewatcher() {
  if (watching_) {
    CancelIo(directory_handle_);
    // Drain any pending completion
    DWORD bytes; ULONG_PTR key; LPOVERLAPPED ov;
    GetQueuedCompletionStatus(iocp_, &bytes, &key, &ov, 100);
  }
  if (iocp_) CloseHandle(iocp_);
  if (directory_handle_ != INVALID_HANDLE_VALUE)
    CloseHandle(directory_handle_);
}
```

**Key decisions**:
- `bWatchSubtree = TRUE` — no manual recursive watch management needed
  (unlike inotify).
- 32 KB buffer — enough for ~200 change notifications. If overflow occurs,
  fall back to full rescan.
- IOCP with 0ms timeout — non-blocking, called from background thread.
- Filter: `FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
  FILE_NOTIFY_CHANGE_SIZE` — minimizes noise from metadata-only changes.

#### macOS: FSEvents (future, optional)

The current checksum polling works on macOS. FSEvents would be a nice-to-have
but is lower priority than Windows (which is completely unimplemented).

If implemented, use `FSEventStreamCreate` with
`kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer` and a
latency of 0.1 seconds. Integrate with a CFRunLoop on the background thread,
or use a dispatch queue.

### Shared interface

```cpp
// Platform-agnostic interface
class Filewatcher {
 public:
  explicit Filewatcher(sqlite3* db);
  ~Filewatcher();

  // Start watching a directory (and subdirectories).
  void Watch(const char* directory);

  // Non-blocking: check for events, push changed filenames to queue.
  // Called from the background thread.
  void CheckForEvents();

  // Drain the debounced change queue. Returns filenames that have
  // settled (no events for 100ms). Caller verifies via checksum.
  // Returns count of filenames written to `out`, up to `max_count`.
  uint32_t DrainChanges(const char** out, uint32_t max_count);

  // Signal that watching should stop (called from destructor or
  // engine shutdown).
  void Stop();
};
```

## Improvements to the reload pipeline

### Problem 1: Full asset re-scan on every poll

**Current**: `WriteAssetsToDb()` enumerates and hashes every file every 10ms.

**Proposed**: The background thread only processes files reported by the
watcher. The full enumeration becomes a startup-only operation and a fallback
for watcher overflow.

```cpp
void CheckChangedFiles() {
  while (!is_stopped()) {
    watcher_.CheckForEvents();

    const char* changed[64];
    uint32_t count = watcher_.DrainChanges(changed, 64);

    if (count == 0) {
      SDL_Delay(50);  // Sleep longer when idle (was 10ms)
      continue;
    }

    hotload_allocator_.Reset();
    int written = 0;
    for (uint32_t i = 0; i < count; ++i) {
      if (ProcessSingleFile(changed[i], db, &hotload_allocator_)) {
        ++written;
      }
    }
    SDL_SetAtomicInt(&pending_changes_, written);
  }
}
```

**Benefits**:
- CPU usage drops to near zero when the developer is not editing.
- Only changed files are read and hashed, not the entire asset directory.
- Sleep interval increases from 10ms to 50ms (or longer) when idle.

### Problem 2: StopAll is a nuclear option

**Current**: `sound.StopAll()` silences every sound on any file change, even
Lua-only edits.

**Already documented** in `design/Sound hot reload.md` with a 3-step
improvement plan. The file watcher improvement enables this: if the watcher
reports that only `main.lua` changed, the reload path can skip
`sound.StopAll()` entirely.

The changed-file set from `DrainChanges()` should be propagated to `Reload()`
so it can make per-asset decisions:

```cpp
void Reload(const char** changed_files, uint32_t count) {
  timers.Clear();
  bool audio_changed = false;
  for (uint32_t i = 0; i < count; ++i) {
    if (HasAudioExtension(changed_files[i])) {
      audio_changed = true;
      break;
    }
  }
  if (audio_changed) sound.StopAll();  // Only when needed
  assets->Load();
}
```

### Problem 3: Physics bodies leak on reload

When `init()` re-runs, old Box2D bodies and collision world colliders are
never cleaned up. New bodies accumulate alongside old ones.

**Fix**: Add `Physics::Clear()` and `CollisionWorld::Clear()` methods, called
from `Reload()` before `assets->Load()`. This matches how `timers.Clear()`
already works.

### Problem 4: No feedback on what reloaded

The developer sees the game flicker but gets no information about what changed
or whether the reload succeeded. Add a brief console log:

```
[hotload] 2 files changed: main.lua, sprites/player.png (took 12ms)
```

This is trivial to implement once the watcher provides filenames.

### Problem 5: init() always re-runs

Every hot-reload re-executes `init()`, which resets all game state. For rapid
iteration on visual tweaks (colors, positions, sprite changes), this is
disruptive — the developer loses their position in the game.

**Improvement**: If only non-script assets changed (images, audio), skip
`lua.LoadMain()` and `lua.Init()`. The asset system already updates textures
and sound buffers in place.

```cpp
if (e_->PendingChanges()) {
  auto changes = e_->GetChangedFiles();
  e_->lua.ClearError();
  e_->Reload(changes);
  if (changes.has_script_changes) {
    e_->lua.LoadMain();
    e_->lua.Init();
  }
  e_->MarkChangesAsProcessed();
}
```

## Implementation plan

### Phase 1: Make inotify useful (Linux)

1. Parse inotify events and extract filenames.
2. Add recursive subdirectory watching with `IN_ISDIR` handling.
3. Switch from `IN_MODIFY` to `IN_CLOSE_WRITE | IN_CREATE | IN_DELETE |
   IN_MOVED_TO`.
4. Add debounce queue (100ms settle, 500ms max delay).
5. Handle `IN_Q_OVERFLOW` with full rescan fallback.
6. Update `CheckChangedFiles()` to process only watcher-reported files.

### Phase 2: Windows implementation

1. Implement `Filewatcher` using `ReadDirectoryChangesW` + IOCP.
2. `bWatchSubtree = TRUE` for recursive watching.
3. Same debounce queue as Linux.
4. Buffer overflow falls back to full rescan.
5. UTF-8 <-> wide string conversion for filenames.

### Phase 3: Reload pipeline improvements

1. Propagate changed-file list from watcher through to `Reload()`.
2. Skip `sound.StopAll()` when no audio files changed.
3. Skip `lua.LoadMain()` + `lua.Init()` when no script files changed.
4. Add `Physics::Clear()` and `CollisionWorld::Clear()` to `Reload()`.
5. Log changed filenames and reload duration.

### Phase 4: macOS FSEvents (optional)

1. Implement `Filewatcher` using FSEvents with file-level events.
2. Integrate with CFRunLoop or dispatch queue on the background thread.
3. Same debounce queue and `DrainChanges()` interface.

## Considered alternatives

### Third-party libraries

**efsw** (MIT, no dependencies, CMake): Cross-platform file watcher with
inotify/FSEvents/IOCP/kqueue backends. Would eliminate platform-specific code.
Rejected because: (a) the engine vendors all libraries and avoids unnecessary
dependencies, (b) the platform-specific code is small (~100 lines each for
Linux and Windows), (c) efsw's polling fallback and symlink handling add
complexity we don't need.

**libfswatch** (GPL-3.0): License is incompatible.

**wc-duck/fswatcher**: Self-described as "under development with bugs". Not
production-ready. User-provided allocators are appealing but not worth the
risk.

### Pure polling (status quo)

Keep the current rapidhash-every-file-every-10ms approach. It works on all
platforms and is simple. Rejected for Phase 1/2 because the CPU and I/O
overhead is measurable (especially on battery-powered laptops during
development) and the improvement is straightforward.

Polling is retained as the **fallback** for overflow conditions and as the
startup scan, ensuring correctness regardless of watcher behavior.

### fanotify (Linux)

Can monitor entire filesystems without per-directory watches. Requires
`CAP_SYS_ADMIN` for some features. Overkill for watching a single asset
directory.
