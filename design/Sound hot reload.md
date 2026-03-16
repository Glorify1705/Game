# Sound System Hot Reload

Hot reload is one of the engine's key features. This document analyzes how the sound system interacts with it today, what's broken, and how to improve it incrementally.

## Current reload flow

```
File change detected (background thread, rapidhash comparison)
  → SDL_AtomicSet(&pending_changes_, count)
  → Main loop picks it up:
      1. sound.StopAll()          ← stops ALL sounds unconditionally
      2. assets->Load()           ← reloads changed assets from DB
         → RegisterSoundLoad callback
           → Sound::LoadSound(asset)
             → sounds_.Insert()   ← updates the asset data in the dictionary
             → for each stream: stream.OnReload(&sound)
               → if name matches: cb_.Reload(sound)
                 → ⚠ currently a no-op (returns true, does nothing)
      3. lua.LoadMain()           ← re-executes main.lua
      4. lua.Init()               ← calls init()
```

### Key files

- `src/game.cc:374-377` — `EngineModules::Reload()`: calls `sound.StopAll()` then `assets->Load()`
- `src/game.cc:506-513` — Main loop: detects pending changes, triggers reload + Lua re-init
- `src/game.cc:305-310` — Registers `Sound::LoadSound` as the sound asset callback
- `src/assets.cc:210-272` — `DbAssets::Load()`: iterates changed assets, calls per-type loaders
- `src/packer.cc:339-410` — Background thread: computes rapidhash, detects file changes
- `src/sound.cc` — `LoadSound`, `StopAll`, `OnReload`
- `src/sound.h` — `Stream::Callbacks::Reload` (unimplemented no-op)

## Problems with the current approach

### 1. StopAll is a nuclear option

`StopAll()` at step 1 kills everything — background music, ambient loops, UI sounds — regardless of which asset changed. Editing a Lua file that has nothing to do with sound still causes an audible gap. This is the single biggest UX pain point.

### 2. OnReload is a no-op

The `Stream::Callbacks` struct has a `reload` function pointer, but it's never set by `CallbackMaker`. The current `OnReload` path returns `true` without doing anything. Even if `StopAll()` were removed, streams playing a reloaded asset would continue decoding from stale data — undefined behavior.

### 3. Managed handles become zombies on Lua re-init

When `lua.Init()` re-runs, scripts call `add_source()` again, creating new stream slots. The old handles from the previous `init()` cycle still occupy slots — they're stopped but not freed. Over many reload cycles, this leaks slots. The `auto_free_` scan only reclaims fire-and-forget streams, not managed ones.

### 4. Decoder state points at raw asset data

`VorbisSampler` and `WavSampler` hold pointers into the asset's compressed data buffer (from the SQLite blob). When the asset system reloads, that buffer may be freed or overwritten. Any stream still referencing the old buffer has dangling pointers.

## Incremental improvement plan

### Step 1: Implement per-asset stream reload (near term)

Make `cb_.Reload()` actually work. When a sound asset changes, only streams playing that specific asset are affected — everything else keeps playing.

**What to implement:**

Wire up `Reload` in `CallbackMaker`:

```cpp
// In CallbackMaker<T>::callbacks():
c.reload = Reload;

// New static method:
static bool Reload(void* ud) {
  return reinterpret_cast<T*>(ud)->Reload();
}
```

Add `Reload` to `VorbisSampler` and `WavSampler` that re-initializes the decoder against the new asset data. The `LoadSound` path already updates `sounds_` dictionary before calling `OnReload`, so the new data is available.

```cpp
// VorbisSampler::Reload(const DbAssets::Sound* sound):
stb_vorbis_close(vorbis_);
vorbis_alloc_.alloc_buffer = vorbis_memory_;
vorbis_alloc_.alloc_buffer_length_in_bytes = kDecoderMemorySize;
int error = 0;
vorbis_ = stb_vorbis_open_memory(sound->contents, sound->size, &error,
                                 &vorbis_alloc_);
return vorbis_ != nullptr;

// WavSampler::Reload(const DbAssets::Sound* sound):
drwav_uninit(&wav_);
allocator_.Reset();
return drwav_init_memory(&wav_, sound->contents, sound->size, &callbacks_);
```

**What changes in the reload flow:**

`OnReload` currently receives the sound data but only passes `ud` to the callback. The callback signature needs the sound data:

```cpp
// Current:
bool (*reload)(void*);
// New:
bool (*reload)(const DbAssets::Sound*, void*);
```

**What to remove:**

Remove `StopAll()` from `EngineModules::Reload()`. Streams that don't match the reloaded asset continue playing uninterrupted. Streams that match get their decoder rebuilt and restart from the beginning.

**Edge case — what about Lua code reloads?**

When only `.lua` files change (no sound assets), `LoadSound` is never called, so no streams are affected. Music keeps playing through a code reload. This is the desired behavior.

When `init()` re-runs, it may call `add_source()` again for sounds it already has handles to. This creates duplicate slots. Two options:

1. **Accept the leak.** Managed sources are few (typically < 10). Slots are cheap. The old slots sit idle.
2. **Add a `Sound::Reset()` call** before `lua.Init()` that stops and frees all managed (non-auto_free) streams. This is a softer version of `StopAll()` that only affects managed handles, not currently-playing fire-and-forget effects.

Option 2 is cleaner but requires the Lua side to always re-acquire handles in `init()`, which it already does today.

### Step 2: Selective StopAll — only stop managed streams (medium term)

Replace `StopAll()` with `ResetManagedStreams()`:

```cpp
void ResetManagedStreams() {
  LockMutex l(mu_);
  for (size_t i = 0; i < stream_; ++i) {
    if (!streams_[i].auto_free_) {
      streams_[i].Stop();
      streams_[i].auto_free_ = true;  // mark for reuse
    }
  }
}
```

Call this from `Reload()` instead of `StopAll()`. Fire-and-forget effects that are still playing continue uninterrupted. Managed sources get stopped and their slots become reclaimable. Lua re-creates them in `init()`.

### Step 3: Static sources for interruption-free effect reload (future)

See [Sound stream free list.md](Sound%20stream%20free%20list.md) § "Future work: Static vs Stream sources" for the full analysis.

Summary: static sources (fully decoded PCM buffers) can be reloaded by swapping the buffer pointer. Active play cursors keep reading — no audible interruption at all. This is the ideal end state for short effects.

| | Static (effects) | Stream (music) |
|---|---|---|
| Reload mechanism | Swap decoded buffer | Rebuild decoder (step 1) |
| Audio interruption | None — cursors keep playing | Music restarts (acceptable) |
| `StopAll()` needed? | No | Only for streams of the reloaded asset |
| Lua `init()` re-run impact | None — buffer cache persists | May need to re-acquire handle |
| Thread safety | Atomic pointer swap + fence | Must hold `mu_`, same as today |

### Migration summary

| Step | Change | Audible impact on reload |
|---|---|---|
| Current | `StopAll()` + no-op `OnReload` | All sound dies, audible gap |
| Step 1 | Implement `cb_.Reload()`, remove `StopAll()` | Only reloaded asset restarts |
| Step 2 | `ResetManagedStreams()` instead of `StopAll()` | Fire-and-forget effects survive Lua re-init |
| Step 3 | Static source buffer swap | Effects survive asset reload too |

## Open questions

- **Should `init()` re-acquire all sound handles?** Today it does, but if we move to persistent static buffers, `init()` could skip re-creating sources that already exist. This would require a `Sound::HasSource(name)` query.
- **Crossfade on music reload?** When a music asset is reloaded, instead of hard-restarting, we could crossfade from the old decoder to the new one using two music slots. Probably not worth the complexity — the user is iterating on the audio file itself, they want to hear the new version immediately.
- **What happens if the reloaded asset has different channel count or sample rate?** Currently nothing handles this. The decoder just opens with the new format. If the SDL audio spec doesn't match, the output will be garbled. Probably need a format check in `Reload()` and a warning log if it changes.
