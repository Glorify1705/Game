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

## Existing bug

`SoundCallback` (`sound.cc:83`) uses `i` for both the outer stream loop and the inner sample mixing loop, shadowing the variable. The inner loop should use `j`.
