#pragma once
#ifndef _GAME_SOUND_H
#define _GAME_SOUND_H

#include <SDL3/SDL.h>

#include <algorithm>
#include <mutex>

#include "allocators.h"
#include "array.h"
#include "assets.h"
#include "clock.h"
#include "dictionary.h"
#include "error.h"
#include "qoa.h"
#include "thread.h"

namespace G {

class Sound {
 public:
  explicit Sound(size_t channels, size_t buffer_samples, Allocator* allocator)
      : buffer_(channels * buffer_samples, allocator),
        sounds_(allocator),
        qoa_samplers_(256, allocator),
        pcm_samplers_(256, allocator),
        qoa_alloc_(allocator),
        pcm_alloc_(allocator),
        effect_cache_(allocator) {
    buffer_.Resize(buffer_.capacity());
  }

  ~Sound() = default;

  using Source = uint32_t;

  // Whether a sound source is managed by Lua (replayable) or fire-and-forget.
  enum class Ownership : uint8_t {
    kManaged,   // Held by Lua via add_source(), can be replayed.
    kAutoFree,  // Created by play(), slot reclaimable once playback finishes.
  };

  ErrorOr<Source> AddSource(std::string_view name,
                            Ownership ownership = Ownership::kManaged);

  ErrorOr<Source> AddEffect(std::string_view name,
                            Ownership ownership = Ownership::kManaged);

  ErrorOr<void> SetSourceGain(Source source, float gain);
  bool SetLoop(Source source, bool loop);
  bool SetPitch(Source source, float pitch);
  bool SetPan(Source source, float pan);

  void SetGlobalGain(float gain) { global_gain_ = gain; }

  ErrorOr<void> StartChannel(Source source);

  ErrorOr<void> Stop(Source source);
  bool Pause(Source source);
  bool Resume(Source source);
  bool IsPlaying(Source source) const;

  void StopAll() {
    LockMutex l(mu_);
    for (size_t i = 0; i < stream_; ++i) streams_[i].Stop();
  }

  void LoadSound(const DbAssets::Sound& sound);

  void SoundCallback(float* result, size_t samples_per_channel,
                     size_t channels);

  // Debug snapshot of a single stream slot, used by the debug UI.
  struct StreamDebugInfo {
    uint32_t handle;  // Interned name handle (use StringByHandle to resolve).
    bool playing;     // Whether the stream is currently producing audio.
    bool loop;        // Whether the stream loops on completion.
    bool managed;     // Whether the stream is Lua-managed or fire-and-forget.
    float gain;       // Per-stream gain (0-1).
    float pitch;      // Playback pitch multiplier.
    float pan;        // Stereo pan (-1 left, 0 center, +1 right).
  };

  // Returns the number of allocated stream slots (including stopped ones).
  size_t stream_count() const { return stream_; }

  // Returns the maximum number of stream slots.
  size_t max_streams() const { return kMaxStreams; }

  // Returns the current global gain.
  float global_gain() const { return global_gain_; }

  // Fills an array with debug info for all allocated streams.
  void GetStreamDebugInfo(StreamDebugInfo* out, size_t max_count) const;

 private:
  // Streaming QOA sampler: decodes one frame at a time.
  class QoaSampler {
   public:
    bool Init(const DbAssets::Sound* sound);
    size_t Load(float* output, size_t samples_per_channel, size_t channels);

    bool Rewind() {
      decoder_.Rewind();
      frame_pos_ = 0;
      frame_len_ = 0;
      return true;
    }

    bool Deinit() { return true; }

   private:
    QoaStreamDecoder decoder_;
    uint32_t channels_ = 0;
    int16_t raw_[kQoaFrameLen * kQoaMaxChannels];
    float frame_buffer_[kQoaFrameLen * kQoaMaxChannels];
    size_t frame_pos_ = 0;
    size_t frame_len_ = 0;
  };

  // PCM sampler: plays from a pre-decoded float buffer (for effects).
  class PcmSampler {
   public:
    bool Init(Slice<float> samples, uint32_t channels) {
      samples_ = samples;
      channels_ = channels;
      pos_ = 0;
      return true;
    }

    size_t Load(float* output, size_t samples_per_channel, size_t channels);

    bool Rewind() {
      pos_ = 0;
      return true;
    }

    bool Deinit() { return true; }

   private:
    Slice<float> samples_;
    uint32_t channels_ = 0;
    size_t pos_ = 0;
  };

  class Stream {
   public:
    struct Callbacks {
      size_t (*load)(float*, size_t, size_t, void*);
      void (*rewind)(void*);
      void (*deinit)(void*);
      void* ud;

      size_t Load(float* a, size_t b, size_t c) { return load(a, b, c, ud); }

      void Rewind() { rewind(ud); }

      void Deinit() { deinit(ud); }
    };

    template <typename T>
    class CallbackMaker {
     public:
      static Callbacks callbacks(T* ptr) {
        Callbacks c;
        c.ud = ptr;
        c.load = Load;
        c.deinit = Deinit;
        c.rewind = Rewind;
        return c;
      }

     private:
      static size_t Load(float* samples, size_t samples_per_channel,
                         size_t channels, void* ud) {
        return reinterpret_cast<T*>(ud)->Load(samples, samples_per_channel,
                                              channels);
      }

      static void Rewind(void* ud) { reinterpret_cast<T*>(ud)->Rewind(); }

      static void Deinit(void* ud) { reinterpret_cast<T*>(ud)->Deinit(); }
    };

    template <typename T>
    void InitFromStream(const DbAssets::Sound* sound, T* stream) {
      cb_ = CallbackMaker<T>::callbacks(stream);
      handle_ = StringIntern(sound->name);
      gain_ = 1.0;
      pos_ = 0;
      playing_ = false;
      source_channels_ = sound->channels;
      loop_head_ready_ = false;
    }

    size_t Load(float* output, size_t samples_per_channel, size_t channels);

    void Start() {
      playing_ = true;
      pos_ = 0;
      buf_len_ = 0;
      fractional_pos_ = 0.0f;
    }

    void Stop() {
      playing_ = false;
      cb_.Rewind();
      pos_ = 0;
      buf_len_ = 0;
      fractional_pos_ = 0.0f;
    }

    void Pause() { playing_ = false; }
    void Resume() { playing_ = true; }
    bool IsPlaying() const { return playing_; }

    bool OnReload(const DbAssets::Sound* sound);

    void Gain(float f) { gain_ = f; }
    void SetLoop(bool loop);
    void SetPitch(float pitch) { pitch_ = std::clamp(pitch, 0.25f, 4.0f); }
    void SetPan(float pan);

    void SetOwnership(Ownership ownership) { ownership_ = ownership; }
    bool IsManaged() const { return ownership_ == Ownership::kManaged; }

   private:
    friend class Sound;

    void WriteStereoOutput(float* output, size_t& written, float s_left,
                           float s_right) {
      output[written++] = gain_ * left_gain_ * s_left;
      output[written++] = gain_ * right_gain_ * s_right;
    }

    void CopyDirect(float* output, size_t& written, size_t total_output);
    void CopyPitched(float* output, size_t& written, size_t total_output);
    void PrepareLoopHead();
    void HandleLoopCrossfade(float* output, size_t written,
                             size_t total_output);

    static constexpr size_t kBufferSizeInSamples = 2048;
    // ~20ms at 44100 Hz, for crossfade looping.
    static constexpr size_t kCrossfadeSamples = 882;

    uint32_t handle_;
    Callbacks cb_;
    bool playing_ = false;
    Ownership ownership_ = Ownership::kManaged;
    float gain_ = 1.0f;
    float samples_[kBufferSizeInSamples];
    size_t pos_ = 0;
    size_t buf_len_ = 0;

    // Looping.
    bool loop_ = false;
    float loop_head_[kCrossfadeSamples * 2];  // Stereo max.
    size_t loop_head_len_ = 0;
    bool loop_head_ready_ = false;

    // Pitch.
    float pitch_ = 1.0f;
    float fractional_pos_ = 0.0f;

    // Panning.
    float pan_ = 0.0f;
    float left_gain_ = 1.0f;
    float right_gain_ = 1.0f;

    // Source info.
    uint32_t source_channels_ = 2;
  };

  // Find or allocate a stream slot.
  size_t FindStreamSlot();

  // Cached decoded PCM data for effects (pre-converted to float), keyed by
  // asset name.
  struct DecodedEffect {
    FixedArray<float> pcm;
    uint32_t channels;

    DecodedEffect(FixedArray<float>&& p, uint32_t c)
        : pcm(std::move(p)), channels(c) {}
  };

  FixedArray<float> buffer_;
  mutable std::mutex mu_;
  Dictionary<DbAssets::Sound> sounds_;
  static constexpr size_t kMaxStreams = 128;
  Stream streams_[kMaxStreams];
  size_t stream_ = 0;
  FixedArray<QoaSampler*> qoa_samplers_;
  FixedArray<PcmSampler*> pcm_samplers_;
  FreeList<QoaSampler> qoa_alloc_;
  FreeList<PcmSampler> pcm_alloc_;
  Dictionary<DecodedEffect*> effect_cache_;
  float global_gain_ = 1.0;
};

}  // namespace G

#endif  // _GAME_SOUND_H
