# Sound Stream Free List

## Problem

Every call to `G.sound.play("gunshot.ogg")` allocates a new stream slot via `AddSource`. Streams are never reclaimed — even after they finish playing, the slot stays occupied. With `kMaxStreams = 16`, rapid-fire sound effects exhaust all slots and crash the game.

The current fix (linear scan for stopped streams) is O(n) and doesn't distinguish between streams the game still holds a handle to and fire-and-forget streams that nobody references.

## Current architecture

```
Sound::AddSource(name, &source)
  ├→ Linear scan for stopped stream (O(n))
  ├→ Or allocate from stream_++ high-water mark
  ├→ Create VorbisSampler / WavSampler via FreeList allocator
  └→ Return slot index as Source handle (uint32_t)

SoundCallback (audio thread, ~172Hz)
  └→ for i in 0..stream_: streams_[i].Load() → mix into output
      (stopped streams return 0 immediately)
```

Two usage patterns exist:

**Managed** (`add_source`): Returns a handle to Lua. The game holds it and calls `play_source`, `stop_source`, `set_volume` later. May replay the same source multiple times.

**Fire-and-forget** (`play`): No handle returned. Calls `AddSource` + `StartChannel` internally. The Source handle is discarded immediately. When the sound finishes (EOF in `Stream::Load`), the stream auto-stops but the slot is never freed.

## Design: simple free list of finished fire-and-forget streams

The fire-and-forget pattern is the one that causes slot exhaustion. Managed sources are typically few (background music, UI sounds) and long-lived. The fix only needs to reclaim fire-and-forget slots.

### Approach

Add an intrusive singly-linked free list through the `streams_[]` array. Only fire-and-forget streams enter this list when they finish playing.

```
streams_[0]  ←  managed (add_source, Lua holds handle)
streams_[1]  →  free list: 1 → 3 → 5 → kNullIndex
streams_[2]  ←  managed
streams_[3]  →  (on free list, linked from 1)
streams_[4]  ←  transient, currently playing
streams_[5]  →  (on free list, linked from 3)
```

### Stream changes

Add two fields to `Stream`:

```cpp
bool auto_free_ = false;      // true for fire-and-forget streams
uint32_t next_free_ = kNullIndex;  // intrusive free list link
```

No state enum needed. `playing_` and `auto_free_` together determine the stream's status:

| `auto_free_` | `playing_` | Meaning |
|---|---|---|
| false | true | Managed, playing |
| false | false | Managed, stopped (handle still valid) |
| true | true | Fire-and-forget, playing |
| true | false | Finished, on free list |

### Sound changes

```cpp
static constexpr uint32_t kNullIndex = UINT32_MAX;
static constexpr size_t kMaxStreams = 128;

uint32_t free_head_ = kNullIndex;  // head of free list
size_t stream_ = 0;                // high-water mark (kept)
```

### Allocation: O(1)

```cpp
uint32_t AllocStream() {
  if (free_head_ != kNullIndex) {
    uint32_t idx = free_head_;
    free_head_ = streams_[idx].next_free_;
    streams_[idx].next_free_ = kNullIndex;
    streams_[idx].auto_free_ = false;
    return idx;
  }
  if (stream_ < kMaxStreams) return stream_++;
  return kNullIndex;
}
```

### Deallocation: O(1)

```cpp
void FreeStream(uint32_t idx) {
  streams_[idx].next_free_ = free_head_;
  free_head_ = idx;
}
```

### AddSource changes

Add a `bool auto_free` parameter. `play()` passes `true`, `add_source()` passes `false`.

```cpp
bool AddSource(std::string_view name, Source* source, bool auto_free);
```

After initializing the stream, set `streams_[slot].auto_free_ = auto_free`.

### SoundCallback auto-reclaim

After calling `Load()` on each stream, check if a fire-and-forget stream just finished:

```cpp
for (size_t i = 0; i < stream_; ++i) {
  auto& stream = streams_[i];
  if (!stream.IsInUse()) continue;  // skip free slots
  size_t read = stream.Load(buffer_.data(), samples_per_channel, channels);
  for (size_t j = 0; j < read; ++j) {
    result[j] += buffer_[j];
  }
  // Auto-reclaim finished fire-and-forget streams.
  if (stream.auto_free_ && !stream.IsPlaying()) {
    FreeStream(i);
  }
}
```

This is thread-safe: `SoundCallback` already holds `mu_`, and all other operations on `free_head_` (via `AddSource`) also hold `mu_`.

### `IsInUse()` accessor

A free-list slot should be skipped by `SoundCallback`, `StopAll`, and `LoadSound`:

```cpp
bool IsInUse() const { return playing_ || !auto_free_; }
```

Wait — a managed stopped stream has `auto_free_ = false, playing_ = false`, and should NOT be skipped (it might get replayed). But `Load()` already returns 0 for non-playing streams, so iterating it is harmless. We only need to skip slots that are on the free list.

A stream is on the free list when `auto_free_ = true && playing_ = false`. So:

```cpp
bool IsOnFreeList() const { return auto_free_ && !playing_; }
```

`SoundCallback` can skip these. Or just let `Load()` return 0 — it's a single branch, negligible even at 128 streams.

### What about managed stream reclamation?

Managed streams (`add_source`) stay allocated forever, same as today. This is fine: games typically create a fixed set of managed sources at init. If needed later, a `free_source` API could be added, but it's not required for the current problem.

## Lua API changes

### `lua_sound.cc`

`play()`: pass `auto_free = true` to `AddSource`.
`add_source()`: pass `auto_free = false` to `AddSource`.
No new Lua functions needed.

### Validation

`play_source`, `stop_source`, `set_volume`: add a check that the slot is not on the free list. Return false if so, which triggers `LUA_ERROR` in the binding.

## Memory impact

Each `Stream` is ~8.3 KB (dominated by `float samples_[2048]`). At 128 streams: ~1 MB. Acceptable.

## Files to modify

- `src/sound.h` — `Stream` gets `auto_free_`, `next_free_`; `Sound` gets `free_head_`, `AllocStream`, `FreeStream`; `kMaxStreams` → 128; `AddSource` signature
- `src/sound.cc` — `AddSource` rewrite, `SoundCallback` auto-reclaim, validation in `Stop`/`StartChannel`/`SetSourceGain`
- `src/lua_sound.cc` — pass `auto_free` flag from `play()` vs `add_source()`

## Alternative: Music vs Effects split (Love2D-style)

Love2D distinguishes two source types: `"static"` (fully decoded, for short effects) and `"stream"` (decoded on the fly, for long music). The interesting idea isn't the decode strategy — we already stream everything — but the **partitioned allocation**. Music and effects can't starve each other because they draw from different pools.

### How it would work

Reserve a small number of dedicated music slots. Effects get the rest.

```
streams_[0]          ← music slot 0 (reserved)
streams_[1]          ← music slot 1 (reserved, for crossfade)
streams_[2..N-1]     ← effects pool
```

New Lua API:

```lua
-- Music: returns a handle, loops by default, only 1-2 active at a time.
local music = G.sound.play_music("overworld.ogg")
G.sound.stop_music(music)

-- Effects: fire-and-forget, draws from the effects pool.
G.sound.play("gunshot.ogg")

-- Managed effects (same as today's add_source):
local src = G.sound.add_source("door_open.ogg")
G.sound.play_source(src)
```

### Does this solve the slot exhaustion problem?

**No, not by itself.** It protects music from being crowded out by effects — which is a real usability win — but the effects pool still leaks slots the same way the current system does. You'd still need either a free list or a linear scan fix within the effects partition.

So this is an **additive** API improvement, not a replacement for the free list.

### What it does buy you

1. **Guaranteed music playback.** A burst of 100 gunshot sounds can never evict background music. This is the main practical benefit — losing music is far more noticeable than dropping an effect.

2. **Simpler music semantics.** Music typically wants looping, crossfading, and volume ducking. A dedicated `play_music` path can default to looping and handle crossfade between two reserved slots without any extra Lua bookkeeping.

3. **Clearer mental model for game code.** `play_music` vs `play` is easier to reason about than remembering which `add_source` handles are long-lived vs transient.

### What it costs

1. **More API surface.** `play_music`, `stop_music`, `set_music_volume` alongside the existing effect functions. More code in `lua_sound.cc`.

2. **Rigid partitioning.** If `kMusicSlots = 2` and some game wants 3 layered music tracks, you're stuck. Love2D avoids this because it doesn't have a hard stream limit — it creates OS-level audio sources dynamically.

3. **Doesn't eliminate the need for effect slot reclamation.** The effects pool still needs the free list (or at minimum the `auto_free_` scan fix). This means the music/effects split is strictly more work than just doing the free list alone.

### Code changes required (on top of the free list)

**`sound.h`:**
```cpp
static constexpr size_t kMusicSlots = 2;
static constexpr size_t kEffectSlotStart = kMusicSlots;

// Music-specific state.
Source music_active_ = kNullIndex;      // currently playing music slot
Source music_next_ = kNullIndex;        // for crossfade
bool music_looping_[kMusicSlots] = {};
```

**`sound.cc` — new `PlayMusic` method:**
```cpp
bool Sound::PlayMusic(std::string_view name, Source* source) {
  // Pick music slot: prefer the inactive one (for crossfade),
  // else reuse the active one.
  uint32_t slot = kNullIndex;
  for (size_t i = 0; i < kMusicSlots; ++i) {
    if (!streams_[i].IsPlaying()) { slot = i; break; }
  }
  if (slot == kNullIndex) {
    // All music slots playing — stop the oldest.
    slot = music_active_;
    streams_[slot].Stop();
  }
  // Init and start (same load logic as AddSource).
  // ...
  music_looping_[slot] = true;
  *source = slot;
  return true;
}
```

**`sound.cc` — `SoundCallback` loop change for music:**
```cpp
// In SoundCallback, after a music stream's Load() returns 0:
if (i < kMusicSlots && music_looping_[i]) {
  stream.Start();  // rewind and keep playing
}
```

**`sound.cc` — `AllocStream` must skip music slots:**
```cpp
uint32_t AllocStream() {
  // Free list only contains effect slots (indices >= kMusicSlots).
  if (free_head_ != kNullIndex) { ... }
  // High-water mark starts at kEffectSlotStart.
  if (stream_ < kMaxStreams) return stream_++;
  return kNullIndex;
}
// Constructor initializes: stream_ = kEffectSlotStart;
```

**`lua_sound.cc` — 3 new functions:**
```cpp
{"play_music", ...},   // calls PlayMusic, returns handle
{"stop_music", ...},   // stops music slot, rewinds
{"set_music_volume", ...},  // sets gain on music slot
```

## Simpler alternatives considered

Before committing to the free list (or the music/effects split), it's worth examining whether simpler fixes suffice.

### Alternative 1: Just fix the linear scan with `auto_free_`

The simplest possible fix. Keep the O(n) scan in `AddSource`, but only reuse streams marked `auto_free_`:

```cpp
bool Sound::AddSource(std::string_view name, Source* source, bool auto_free) {
  // ...
  size_t slot = stream_;
  for (size_t i = 0; i < stream_; ++i) {
    if (streams_[i].auto_free_ && !streams_[i].IsPlaying()) {
      slot = i;
      break;
    }
  }
  // ... rest unchanged ...
  streams_[slot].auto_free_ = auto_free;
}
```

**Pros:**
- Minimal diff. One new bool on Stream, a single `if` condition change in `AddSource`. No free list, no `next_free_`, no `AllocStream`/`FreeStream`.
- Solves the correctness bug (managed streams never get stomped).
- O(n) is irrelevant at n=16 or even n=128 — the scan is behind a mutex that the audio thread already contends on at 172 Hz. A scan over 128 bools costs ~2 cache lines.

**Cons:**
- Doesn't proactively reclaim in `SoundCallback`. Instead, slots sit "dead" until the next `AddSource` call happens to find them. This is fine — dead slots just return 0 from `Load()`, costing one branch per callback.
- If the high-water mark hits `kMaxStreams` before any `play()` call reclaims a slot, you get a false "out of streams" error. In practice unlikely: `play()` is the only thing that creates transient streams, and it's also the only thing that reclaims them.

**Verdict:** This is probably good enough. The free list is a premature optimization at these sizes. Ship this first, add the free list later only if profiling shows the scan matters (it won't).

### Alternative 2: Bump `kMaxStreams` and do nothing else

Just raise the limit from 16 to, say, 64 or 128. Don't fix the leak.

**Pros:**
- Zero code changes beyond one constant.

**Cons:**
- Doesn't fix the bug, just delays it. A game session that plays 129 sound effects still crashes. This is a ticking time bomb.
- Memory cost grows for no reason (dead streams hold 8 KB each for an unused sample buffer, plus ~256 KB each for the VorbisSampler/WavSampler decode buffer that was allocated from the free list allocator and never returned).

**Verdict:** Not a fix. The decoder memory leak alone makes this untenable — every unreclaimed stream holds onto its `VorbisSampler` or `WavSampler` allocation permanently.

### Alternative 3: Ring buffer for effects

Instead of a free list, use a circular index for fire-and-forget effects. New effects always go to `effect_next_`, which wraps around. If the slot is still playing, either skip it (drop the new sound) or stop it (cut it short).

```cpp
uint32_t effect_next_ = kEffectSlotStart;

uint32_t AllocEffect() {
  uint32_t slot = effect_next_;
  effect_next_ = kEffectSlotStart +
      (effect_next_ - kEffectSlotStart + 1) % kEffectSlotCount;
  if (streams_[slot].IsPlaying()) {
    streams_[slot].Stop();  // cut it short
  }
  return slot;
}
```

**Pros:**
- O(1), no linked list overhead.
- Graceful degradation: when the pool is full, the oldest effect gets cut short instead of crashing. This is arguably the right behavior for sound effects.
- Naturally prevents unbounded growth.

**Cons:**
- Only works for fire-and-forget effects. Managed sources need separate handling (same partitioning problem as the music/effects split).
- Stopping a still-playing sound mid-stream can produce audible pops. Would need a short fade-out, which complicates `Stream::Stop()`.
- Interleaves with managed sources awkwardly — either partition the array (back to the music/effects split) or keep managed sources in a separate structure.

**Verdict:** Interesting, but the forced eviction and partitioning complexity make it harder than the free list for not much benefit.

## Recommendation

**Do Alternative 1 first** (linear scan + `auto_free_` flag). It's the smallest correct fix:
- Add `bool auto_free_` to `Stream` (1 line)
- Change `AddSource` signature to take `bool auto_free` (1 line)
- Change the reuse scan to check `auto_free_ && !IsPlaying()` (1 line)
- Set `auto_free_` after init (1 line)
- Pass `true`/`false` from `lua_sound.cc` (2 call sites)

That's ~6 lines of real change. It fixes the correctness bug (managed streams are never reused) and the slot exhaustion (fire-and-forget streams are reused). Ship it.

**If music protection matters** (i.e., you want background music to never be interrupted by a burst of effects), add the music/effects split on top. But this is a separate concern from the slot leak — it's an API design question, not a bug fix.

**The free list becomes relevant only if** `kMaxStreams` grows large enough that the O(n) scan measurably affects the mutex hold time in `SoundCallback`. At 128 streams, the scan touches ~128 bytes (one bool per stream) — well within L1 cache, ~50 ns. The SDL audio callback itself takes microseconds. Not worth optimizing.

**Decoder memory note:** Whichever approach is chosen, reclaimed fire-and-forget streams should also free their `VorbisSampler`/`WavSampler` back to the respective `FreeList<>` allocator. Currently `AddSource` allocates via `vorbis_alloc_.Alloc()` / `wavs_alloc_.New()` but never frees. The `Stream::Callbacks::Deinit()` path exists but is never called. When reusing a slot, call `cb_.Deinit()` before reinitializing.
