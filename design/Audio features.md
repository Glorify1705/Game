# Audio Features: Looping, Pause/Resume, Pitch, and Panning

**Status**: Implemented in PR #22.

## Motivation

The engine comparison (Engine comparison.md, §8) identified several missing audio features.
This document covers the four most impactful ones, in priority order:

1. **Looping** — needed for music and ambient sounds. Every reference engine supports it.
2. **Pause/resume** — per-source pause without rewinding. Basic expectation for any audio system.
3. **Pitch control** — speed/pitch shifting for variety (footsteps, impacts) and gameplay effects.
4. **Stereo panning** — positional left/right placement for game feel.

Lower-priority features (3D spatialization, audio effects, procedural synth, playlists, seek/tell)
are out of scope for this document.

## Current architecture

The audio callback path (from `sound.h` / `sound.cc`):

```
SDL audio thread calls SoundCallback()
  → for each Stream: stream.Load(buffer, samples_per_channel, channels)
    → Stream has an internal float buffer (2048 samples)
    → Stream calls cb_.Load() → QoaSampler::Load() or PcmSampler::Load()
    → Applies per-stream gain
  → Mixes all streams into output
  → Applies global gain
```

Key types:
- `QoaSampler` — streaming decoder, one QOA frame at a time, outputs float.
- `PcmSampler` — plays from a pre-decoded float buffer (for cached effects).
- `Stream` — owns a sampler via type-erased callbacks, handles gain and start/stop.
- `Sound` — top-level manager. Owns streams, mutex, effect cache.

All mixing happens in `SoundCallback` at the SDL device sample rate, in interleaved stereo float.

## Feature 1: Looping

### Design

Add a `loop` flag to `Stream`. When the sampler returns fewer samples than requested (EOF),
rewind it and continue filling the output buffer from the beginning.

### Changes

**`sound.h` — `Stream`**:
```cpp
// New field
bool loop_ = false;

// New method
void SetLoop(bool loop) { loop_ = loop; }
```

Modify `Stream::Load`: when the inner `cb_.Load()` returns 0 (EOF) and `loop_` is true,
call `cb_.Rewind()` and continue the fill loop instead of stopping.

```cpp
size_t Load(float* output, size_t samples_per_channel, size_t channels) {
  if (!playing_) return 0;
  size_t samples = samples_per_channel * channels;
  size_t read = 0;
  for (; read < samples;) {
    if (pos_ >= kBufferSizeInSamples) {
      const size_t samples_read =
          cb_.Load(samples_, kBufferSizeInSamples / channels, channels);
      if (samples_read == 0) {
        if (loop_) {
          cb_.Rewind();
          continue;  // Try again from the beginning.
        }
        Stop();
        return read;
      }
      pos_ = 0;
    }
    // ... copy to output as before
  }
  return read;
}
```

**`sound.h` — `Sound`**:
```cpp
bool SetLoop(Source source, bool loop);
```

**`sound.cc`**:
```cpp
bool Sound::SetLoop(Source source, bool loop) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].SetLoop(loop);
  return true;
}
```

**`lua_sound.cc`** — new Lua API:
```lua
sound.set_loop(source, loop)  -- enable/disable looping for a source
```


### Interaction with effects

Effects (`PcmSampler`) can loop too — the rewind just resets `pos_` to 0. This is useful
for looping ambient effects (rain, fire crackling).

## Feature 2: Pause / Resume

### Design

The `Stream` already has a `playing_` flag and a `Pause()` method that sets it to false.
The difference from `Stop()` is that `Pause()` does not rewind. This is already partially
implemented but not exposed.

### Changes

**`sound.h` — `Sound`**:
```cpp
bool Pause(Source source);
bool Resume(Source source);
bool IsPlaying(Source source) const;
```

**`sound.cc`**:
```cpp
bool Sound::Pause(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].Pause();
  return true;
}

bool Sound::Resume(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].Resume();
  return true;
}

bool Sound::IsPlaying(Source source) const {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  return streams_[source].IsPlaying();
}
```

**`sound.h` — `Stream`**: Add `Resume()` that sets `playing_ = true` without resetting `pos_`.
Currently `Start()` resets `pos_ = 0`, so we need a separate method:

```cpp
void Resume() { playing_ = true; }
// Start() stays as-is: sets playing_ = true AND resets pos_ = 0.
```

**`lua_sound.cc`** — new Lua API:
```lua
sound.pause(source)        -- pause without rewinding
sound.resume(source)       -- resume from where it was paused
sound.is_playing(source)   -- returns true/false
```

## Feature 3: Pitch control

### Design

Pitch control is equivalent to playback speed: pitch > 1.0 plays faster and higher,
pitch < 1.0 plays slower and lower. This is implemented as sample rate conversion
in the sampler's `Load` method using linear interpolation.

We do not need a separate "speed" parameter — pitch and speed are coupled, which is
the standard behavior (Love2D, high_impact, Anchor all work this way).

### Changes

**`sound.h` — `Stream`**:
```cpp
float pitch_ = 1.0f;
float fractional_pos_ = 0.0f;  // Sub-sample position for interpolation.

void SetPitch(float pitch) { pitch_ = pitch; }
```

The implementation splits `Stream::Load` into two copy paths:

- **`CopyDirect`** (pitch == 1.0f): copies samples 1:1 with gain, panning, and mono
  upmix. This is the common case and avoids the overhead of interpolation.
- **`CopyPitched`** (pitch != 1.0f): advances `fractional_pos_` by `pitch_` per output
  frame, linearly interpolating between adjacent source samples.

```cpp
// CopyPitched, mono case:
while (written + 1 < total_output) {
  size_t i = static_cast<size_t>(fractional_pos_);
  if (i + 1 >= buf_len_) {
    pos_ = buf_len_;  // Force refill.
    return;
  }
  float frac = fractional_pos_ - float(i);
  float s = samples_[i] * (1.0f - frac) + samples_[i + 1] * frac;
  WriteStereoOutput(output, written, s, s);
  fractional_pos_ += pitch_;
}
```

When `CopyPitched` exhausts the buffer for interpolation (not enough samples remain for
a two-point interpolation), it sets `pos_ = buf_len_` to force a refill on the next
iteration. This is critical — without it, `pos_` can end up less than `buf_len_` while
`fractional_pos_` is too far advanced to interpolate, causing an infinite loop.

**Pitch range**: Clamp to `[0.25, 4.0]` to avoid extreme artifacts. This matches
Love2D's practical range.

**`sound.h` — `Sound`**:
```cpp
bool SetPitch(Source source, float pitch);
```

**`lua_sound.cc`** — new Lua API:
```lua
sound.set_pitch(source, pitch)  -- 1.0 = normal, 0.5 = half speed, 2.0 = double
```

### Interaction with looping

Pitch works with looping naturally — the rewind just resets the sampler, and playback
continues at the current pitch.

### Quality notes

Linear interpolation is sufficient for game audio. Higher-quality resampling (sinc, cubic)
adds complexity and CPU cost for minimal perceptual benefit in a game context. If quality
becomes an issue for extreme pitch values, we can upgrade to cubic later.

## Feature 4: Stereo panning

### Design

Panning places a sound in the stereo field. We use a simple constant-power panning law:

```
pan ∈ [-1.0, 1.0]   (-1 = full left, 0 = center, 1 = full right)

left_gain  = cos(θ)
right_gain = sin(θ)

where θ = (pan + 1) * π/4
```

This maintains constant perceived loudness as the sound moves across the stereo field,
unlike linear panning which has a ~3dB dip at center.

### Changes

**`sound.h` — `Stream`**:
```cpp
float pan_ = 0.0f;        // -1 (left) to +1 (right), 0 = center
float left_gain_ = 1.0f;  // Precomputed from pan_
float right_gain_ = 1.0f;

void SetPan(float pan);
```

**Implementation of `SetPan`**:
```cpp
void SetPan(float pan) {
  pan_ = std::clamp(pan, -1.0f, 1.0f);
  float angle = (pan_ + 1.0f) * (kPi / 4.0f);
  left_gain_ = std::cos(angle);
  right_gain_ = std::sin(angle);
}
```

Precomputing the gains in `SetPan` avoids trig in the audio callback.

Modify `Stream::Load` to apply panning. Since the output is interleaved stereo
(left, right, left, right, ...), apply `left_gain_` to even samples and
`right_gain_` to odd samples:

```cpp
// In the copy loop:
output[read]     = gain_ * left_gain_  * samples_[pos_];      // left
output[read + 1] = gain_ * right_gain_ * samples_[pos_ + 1];  // right
read += 2;
pos_ += 2;
```

For mono sources (effects decoded from mono QOA), both channels get the same source
sample, with different L/R gains applied:

```cpp
float s = samples_[pos_++];
output[read]     = gain_ * left_gain_  * s;
output[read + 1] = gain_ * right_gain_ * s;
read += 2;
```

This requires knowing whether the source is mono or stereo. Add a `channels_` field
to `Stream` set during `InitFromStream`.

**`sound.h` — `Sound`**:
```cpp
bool SetPan(Source source, float pan);
```

**`lua_sound.cc`** — new Lua API:
```lua
sound.set_pan(source, pan)  -- -1.0 (left) to 1.0 (right), 0 = center
```

## Implementation order

All four features were implemented together in a single pass:

1. **Looping** — with automatic crossfade at loop boundaries.
2. **Pause/resume** — exposes existing `Pause()` and adds `Resume()`.
3. **Pitch** — dual-path CopyDirect/CopyPitched with linear interpolation.
4. **Panning** — constant-power law with mono upmix in the stream.

## Lua API summary

After all four features, the full sound API would be:

```lua
-- Source management
sound.add_source(name) -> source       -- streaming QOA source
sound.add_effect(name) -> source       -- pre-decoded effect
sound.play(name)                       -- fire-and-forget stream
sound.play_effect(name)                -- fire-and-forget effect

-- Playback control
sound.play_source(source)              -- start/restart playback
sound.pause(source)                    -- pause without rewinding
sound.resume(source)                   -- resume from paused position
sound.stop_source(source)              -- stop and rewind
sound.is_playing(source) -> bool       -- query playback state

-- Parameters
sound.set_volume(source, gain)         -- per-source volume [0, 1]
sound.set_global_volume(gain)          -- master volume [0, 1]
sound.set_loop(source, loop)           -- enable/disable looping
sound.set_pitch(source, pitch)         -- playback speed [0.25, 4.0]
sound.set_pan(source, pan)             -- stereo position [-1, 1]
```

## Crossfade looping

### How reference engines handle loop boundaries

None of the three reference engines implement crossfade looping natively:

- **Love2D** uses OpenAL's built-in `Source:setLooping()` which hard-wraps from end to
  start. OpenAL supports loop point markers (`AL_LOOP_START`/`AL_LOOP_END`) but Love2D
  doesn't expose them. Users work around clicks by finding zero-crossing points in the
  asset or padding the loop region manually. This is a frequent pain point on the forums.

- **high_impact** uses QOA with a simple loop flag (`sound_set_loop`). When the decoder
  hits EOF it rewinds. No crossfade. QOA's frame-based structure (5120 samples/frame)
  helps because frame boundaries are more predictable, but clicks are still possible
  with carelessly authored assets.

- **Anchor** supports crossfading, but only between **playlist tracks** (track A fades
  out while track B fades in). This is track-to-track crossfading in a sequence, not
  loop-point crossfading within a single sound. Individual sound looping is a simple flag
  via miniaudio's `ma_sound_set_looping()`.

Professional middleware (FMOD, Wwise) do support loop crossfading: FMOD uses transition
markers with pre-roll, Wwise uses power-crossfade envelopes. These are designed for AAA
adaptive music systems and are significantly more complex than what we need.

### Do we need crossfade looping?

For most game audio, **clean loop points in the asset are sufficient**. A well-authored
music loop or ambient sound will have matching waveforms at the start and end, producing
no click. This is how Love2D, high_impact, and Anchor all work in practice.

However, crossfade looping is cheap to implement and provides a safety net for assets
that aren't perfectly edited. It also enables seamless looping of sounds that are
inherently hard to loop cleanly (e.g. recordings with reverb tails, complex ambient
textures).

### Design

Implement crossfade as an **overlap-add** at the loop boundary. When looping is enabled,
the stream pre-reads a small window from the beginning of the source and blends it with
the tail end as playback wraps around.

**Parameters**:
- Crossfade duration: fixed at **20ms** (e.g. 882 samples at 44100 Hz). This is long
  enough to eliminate clicks but short enough to be imperceptible. Professional middleware
  typically uses 10-50ms depending on content.
- Crossfade curve: **constant-power** (same cos/sin law as panning) to maintain perceived
  loudness across the blend. A linear crossfade causes a ~3dB dip at the midpoint.

**How it works**:

```
                         crossfade_len
                    ├─────────────────────┤
Source samples: ... [tail_samples] [end]  →  [start] [head_samples] ...
                         ↓ fade out              ↓ fade in
Output:              tail * cos(θ) + head * sin(θ)
```

1. When a looping source initializes (or when `SetLoop(true)` is called on a playing
   source), pre-decode the first `crossfade_len` samples from the beginning into a
   `loop_head_` buffer. This is a one-time cost.

2. During normal playback, nothing changes — samples play as before.

3. When the sampler reports EOF (or returns fewer samples than requested), we are in the
   crossfade region. For the remaining samples before EOF:
   - The outgoing (tail) samples fade out: `tail[i] * cos(θ_i)`
   - The incoming (head) samples fade in: `head[i] * sin(θ_i)`
   - Output: `tail[i] * cos(θ_i) + head[i] * sin(θ_i)`
   - Where `θ_i = (i / crossfade_len) * π/2`

4. After the crossfade completes, rewind the sampler and skip ahead by `crossfade_len`
   samples (since we already played them via `loop_head_`). Continue normal playback.

**Changes to `Stream`**:

```cpp
static constexpr size_t kCrossfadeSamples = 882;  // ~20ms at 44100 Hz

float loop_head_[kCrossfadeSamples * 2];  // Pre-decoded start (stereo max)
size_t loop_head_len_ = 0;                // Actual samples in loop_head_
bool loop_head_ready_ = false;            // Whether loop_head_ has been filled

void PrepareLoopHead();  // Pre-decode first kCrossfadeSamples from source
```

`PrepareLoopHead()` saves the current buffer state, rewinds the sampler, decodes the
first `kCrossfadeSamples` into `loop_head_`, rewinds again, and restores the buffer.
Called once when `SetLoop(true)` is called.

**Implementation**: Rather than a streaming crossfade state machine, the implementation
uses a simpler **retroactive overlap-add** approach. When the sampler returns 0 (EOF)
and `loop_` is true, `HandleLoopCrossfade()` walks backwards through the already-written
output and blends in the pre-decoded loop head:

```cpp
// In Stream::Load, when sampler returns 0 and loop_ is true:
HandleLoopCrossfade(output, written, total_output);
cb_.Rewind();
// Skip past the samples we already crossfaded in.
if (loop_head_ready_) {
  cb_.Load(samples_, loop_head_len_ / source_channels_, source_channels_);
}
```

`HandleLoopCrossfade` applies constant-power crossfade (cos/sin) between the tail
of the output buffer and the loop head, respecting source channel count and panning:

```cpp
for (size_t i = 0; i < xfade_pairs; ++i) {
  float t = float(i) / float(xfade_pairs);
  float fade_out = cos(t * π/2);
  float fade_in  = sin(t * π/2);
  output[j]     = fade_out * output[j]     + fade_in * gain * left_gain  * head_l;
  output[j + 1] = fade_out * output[j + 1] + fade_in * gain * right_gain * head_r;
}
```

This is simpler than a streaming crossfade because it avoids tracking crossfade state
across buffer boundaries — the entire blend happens in one pass over the output that's
already been written.

**Interaction with pitch**: When pitch != 1.0, the effective crossfade duration in
wall-clock time changes (faster pitch = shorter crossfade). This is acceptable — the
crossfade is still the same number of output samples, which is what matters for
click elimination.

**Opt-in vs default**: Crossfade is **on by default** when looping is enabled. It adds
minimal CPU cost and eliminates an entire class of audio bugs. Sources with clean loop
points won't be audibly affected by a 20ms crossfade.

### Lua API

No new API needed — crossfade is automatic when looping is enabled.

## Mono upmixing

Mono sources need to be upmixed to stereo for the mixer. This happens in the **stream's
copy loop**, not in the sampler. This keeps the samplers simple (they output whatever
channel count the source has) and lets panning work naturally on mono sources.

During `Stream::Load`, when copying from the internal buffer to the output:

```cpp
if (source_channels_ == 1) {
  // Mono: duplicate the sample to both channels, apply panning.
  float s = samples_[pos_++];
  output[read]     = gain_ * left_gain_  * s;
  output[read + 1] = gain_ * right_gain_ * s;
  read += 2;
} else {
  // Stereo: apply panning to existing L/R.
  output[read]     = gain_ * left_gain_  * samples_[pos_];
  output[read + 1] = gain_ * right_gain_ * samples_[pos_ + 1];
  read += 2;
  pos_ += 2;
}
```

The `source_channels_` field is set during `InitFromStream` from the `DbAssets::Sound`
metadata (which stores the QOA channel count).

## Sample rate mismatch

All audio assets are required to match the SDL audio device sample rate. The packer
can validate this at pack time and emit a warning or error if a source file's sample
rate doesn't match the target. Since we control the entire asset pipeline (WAV/OGG
are converted to QOA at pack time via the packer), we can resample during packing if
needed, keeping the runtime simple.

This is the same approach as high_impact, which also requires assets to match the
device rate.
