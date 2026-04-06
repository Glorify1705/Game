---
status: implemented
tags: [audio, codec]
---

# QOA Audio Format Support

## Motivation

The engine currently uses WAV and OGG Vorbis for audio, decoded via `dr_wav` and `stb_vorbis`.
This is inconsistent with the image pipeline, where PNG is converted to QOI at pack time and QOI
is the sole runtime format. We want the same for audio: **WAV and OGG are converted to QOA at
pack time, and QOA is the only runtime audio format.**

QOA (Quite OK Audio) is a good fit:
- ~400 LOC reference implementation in C, easy to vendor and own like QOI.
- 3.2 bits/sample (5:1 compression of 16-bit audio). Better quality than ADPCM at comparable bitrate.
- 3x faster decoding than Vorbis. Simple enough for real-time streaming without dedicated threads.
- Frame-based structure (5120 samples/frame) naturally supports both full decode and streaming.
- Same design philosophy as QOI: simplicity, no dependencies, easy to implement from scratch.

## QOA Format Summary

- Magic: `0x716f6166` ("qoaf")
- 8-byte file header: magic (32-bit) + total samples (32-bit)
- Frames of up to 5120 samples each (256 slices x 20 samples/slice)
- Frame header (8 bytes): channels (8-bit), sample rate (24-bit), samples (16-bit), frame size (16-bit)
- Per-channel LMS state (16 bytes): 4 history + 4 weight values
- Each 8-byte slice: 4-bit scalefactor + 20 x 3-bit quantized residuals
- Uses Sign-Sign LMS predictor with 16 scalefactors and lookup-table dequantization
- Up to 8 channels, any sample rate

## Design

### Two playback modes

Following the existing design doc (Sound system.md), audio sources split into two categories:

**1. Effects (upfront decode)**
- Short sounds played frequently (gunshots, UI clicks, footsteps).
- Fully decoded to interleaved `int16_t` samples at `AddSource` time.
- Stored in a pool. Multiple streams can reference the same decoded buffer.
- Memory cost: `samples * channels * 2` bytes per effect.

**2. Music/ambient (streaming)**
- Long sounds, typically looped.
- Decoded on demand one frame at a time (5120 samples) during the audio callback.
- Only the current frame buffer lives in memory (~10KB per channel per stream).
- The compressed QOA data stays in the asset database blob; the sampler reads from it.

### Codec implementation: `src/qoa.h` / `src/qoa.cc`

Following the QOI pattern in `image.h`/`image.cc`, implement QOA encode/decode from scratch (not
vendoring the reference header). This keeps the code style consistent and lets us use our allocator
system.

```cpp
namespace G {

struct QoaDesc {
  uint32_t channels;
  uint32_t samplerate;
  uint32_t samples;      // total sample count (per channel)
};

// Decode entire QOA buffer to interleaved int16_t samples.
// Returns allocated FixedArray. Populates desc with format info.
FixedArray<int16_t> QoaDecode(ByteSlice data, QoaDesc* desc,
                              Allocator* allocator);

// Encode interleaved int16_t samples to QOA.
// Returns allocated FixedArray with the encoded bytes.
FixedArray<uint8_t> QoaEncode(Slice<int16_t> samples, const QoaDesc* desc,
                              Allocator* allocator);

// Streaming decoder: decodes one frame at a time from a QOA buffer.
class QoaStreamDecoder {
 public:
  // Initialize with a QOA buffer (non-owning view).
  bool Init(ByteSlice data, QoaDesc* desc);

  // Decode next frame into output buffer (interleaved int16_t).
  // Returns number of samples decoded (per channel), 0 at EOF.
  size_t DecodeFrame(Slice<int16_t>* output);

  // Seek back to beginning.
  void Rewind();

 private:
  ByteSlice data_;
  size_t pos_;             // current byte position
  QoaDesc desc_;
  // LMS state per channel, updated across frames
  // (4 history + 4 weights per channel)
};

}  // namespace G
```

### Packer changes: `src/packer.cc`

Add WAV/OGG -> QOA conversion at pack time, mirroring PNG -> QOI:

```
Source (.wav/.ogg) -> decode to int16_t PCM (via dr_wav / stb_vorbis)
                   -> encode to QOA (via QoaEncode)
                   -> store QOA blob in `audios` table
```

- The `InsertAudio` method currently stores raw WAV/OGG bytes. Change it to:
  1. Detect format by extension.
  2. Decode to PCM int16_t samples using existing dr_wav/stb_vorbis.
  3. Encode PCM to QOA.
  4. Store QOA bytes + metadata (channels, samplerate, total samples) in the DB.
- Also support `.qoa` files directly (pass through, like `.qoi` for images).
- The `audios` table schema needs new columns: `channels`, `samplerate`, `samples` (mirroring
  `images` having `width`, `height`, `components`).

### Sound system changes: `src/sound.h` / `src/sound.cc`

Replace `WavSampler` and `VorbisSampler` with a single `QoaSampler` that handles streaming:

```cpp
class QoaSampler {
 public:
  bool Init(const DbAssets::Sound* sound);

  // Decodes next chunk of samples as interleaved float.
  // Converts int16_t -> float (divide by 32768).
  size_t Load(float* output, size_t samples_per_channel, size_t channels);

  bool Rewind();
  bool Deinit();

 private:
  QoaStreamDecoder decoder_;
  const DbAssets::Sound* sound_;  // reference to asset data (non-owning)
  int16_t frame_buffer_[QOA_FRAME_LEN * QOA_MAX_CHANNELS];
  size_t frame_pos_;
  size_t frame_samples_;
};
```

For effects (upfront decode), add a new sampler that plays from a pre-decoded buffer:

```cpp
class PcmSampler {
 public:
  // Takes a non-owning view into a shared decoded buffer.
  bool Init(Slice<int16_t> samples, size_t channels);
  size_t Load(float* output, size_t samples_per_channel, size_t channels);
  bool Rewind();
  bool Deinit();

 private:
  Slice<int16_t> samples_;   // non-owning view into shared decoded buffer
  size_t channels_;
  size_t pos_;
};
```

### Lua API changes: `src/lua_sound.cc`

Extend the API with explicit effect vs music distinction:

```lua
-- Existing API (unchanged semantics, but now backed by QOA):
G.sound.add_source(name)         -- streaming QOA source
G.sound.play(name)               -- fire-and-forget streaming
G.sound.play_source(source_id)
G.sound.stop_source(source_id)
G.sound.set_volume(source_id, gain)
G.sound.set_global_volume(gain)

-- New API:
G.sound.add_effect(name)         -- fully decodes QOA upfront, returns source_id
G.sound.play_effect(name)        -- fire-and-forget effect (decode once, cache)
```

Effects that are loaded via `add_effect` or `play_effect` share a single decoded PCM buffer
in memory. Multiple simultaneous playbacks of the same effect each get their own `PcmSampler`
pointing to the same data.

### Asset schema changes

Update the `audios` table:

```sql
CREATE TABLE IF NOT EXISTS audios (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT UNIQUE NOT NULL,
  channels INTEGER NOT NULL DEFAULT 0,
  samplerate INTEGER NOT NULL DEFAULT 0,
  samples INTEGER NOT NULL DEFAULT 0,
  contents BLOB NOT NULL
);
```

Update `DbAssets::Sound` to include the new fields:

```cpp
struct Sound {
  std::string_view name;
  size_t size;
  uint32_t channels;
  uint32_t samplerate;
  uint32_t samples;
  uint8_t* contents;
  ChecksumType checksum;
};
```

## Implementation plan

1. **QOA codec** (`qoa.h`, `qoa.cc`): Implement decode (full + streaming) and encode following
   the reference spec. Write tests against reference test vectors.

2. **Schema migration**: Add columns to `audios` table. Update `DbAssets::Sound` struct and
   the asset loading code in `assets.cc`.

3. **Packer conversion**: Modify `InsertAudio` to decode WAV/OGG -> encode QOA. Add `.qoa`
   passthrough handler. Remove `stb_vorbis` and `dr_wav` from runtime (keep in packer only).

4. **QoaSampler**: Implement the streaming sampler using `QoaStreamDecoder`. Drop `WavSampler`
   and `VorbisSampler` from `sound.h`.

5. **PcmSampler + effect cache**: Implement upfront-decode path. Add a `Dictionary<FixedArray<int16_t>>`
   in `Sound` that caches decoded PCM buffers keyed by asset name. `PcmSampler` holds a
   `Slice<int16_t>` view into the cached buffer.

6. **Lua API**: Add `add_effect` / `play_effect`. Keep existing API working.

7. **Hot reload**: Ensure QOA assets hot-reload correctly (invalidate cached PCM buffers,
   reinitialize active samplers).

## Dependencies removed at runtime

After this change, the runtime no longer needs:
- `stb_vorbis.h` (Vorbis decoding) - only needed in packer
- `dr_wav.h` (WAV decoding) - only needed in packer

This mirrors how `stb_image.h` is only needed in the packer (for PNG), not at runtime.

## Open questions

- **Looping**: The current system doesn't support looping. The Sound system design doc mentions
  it as desired. Should we add loop support as part of this work? (Simple: when `QoaSampler`
  hits EOF, rewind instead of stopping.)
- **Sample rate conversion**: QOA preserves the source sample rate. If the SDL audio device
  runs at a different rate than the QOA file, we need resampling. Currently WAV/OGG decoders
  handle this implicitly. We may need a simple linear resampler, or require all assets match
  the device sample rate.
- **Channel conversion**: Similarly, mono QOA files need upmixing to stereo for the mixer.
  This is straightforward (duplicate samples) but needs to be explicit.
