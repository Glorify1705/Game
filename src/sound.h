#pragma once
#ifndef _GAME_SOUND_H
#define _GAME_SOUND_H

#include <algorithm>

#include "SDL.h"
#include "allocators.h"
#include "array.h"
#include "assets.h"
#include "clock.h"
#include "dictionary.h"
#include "qoa.h"
#include "thread.h"

namespace G {

class Sound {
 public:
  explicit Sound(const SDL_AudioSpec& spec, Allocator* allocator)
      : buffer_(static_cast<size_t>(spec.channels) * spec.samples, allocator),
        sounds_(allocator),
        qoa_samplers_(256, allocator),
        pcm_samplers_(256, allocator),
        qoa_alloc_(allocator),
        pcm_alloc_(allocator),
        effect_cache_(allocator) {
    buffer_.Resize(buffer_.capacity());
    mu_ = SDL_CreateMutex();
  }

  ~Sound() {
    if (mu_ != nullptr) SDL_DestroyMutex(mu_);
  }

  using Source = uint32_t;

  // Whether a sound source is managed by Lua (replayable) or fire-and-forget.
  enum class Ownership : uint8_t {
    kManaged,   // Held by Lua via add_source(), can be replayed.
    kAutoFree,  // Created by play(), slot reclaimable once playback finishes.
  };

  bool AddSource(std::string_view name, Source* source,
                 Ownership ownership = Ownership::kManaged);

  bool AddEffect(std::string_view name, Source* source,
                 Ownership ownership = Ownership::kManaged);

  bool SetSourceGain(Source source, float gain);

  void SetGlobalGain(float gain) { global_gain_ = gain; }

  bool StartChannel(Source source);

  bool Stop(Source source);

  void StopAll() {
    LockMutex l(mu_);
    for (size_t i = 0; i < stream_; ++i) streams_[i].Stop();
  }

  void LoadSound(const DbAssets::Sound& sound);

  void SoundCallback(float* result, size_t samples_per_channel,
                     size_t channels);

 private:
  // Streaming QOA sampler: decodes one frame at a time.
  class QoaSampler {
   public:
    bool Init(const DbAssets::Sound* sound) {
      TIMER("Initializing QOA stream ", sound->name);
      ByteSlice data(sound->contents, sound->size);
      QoaDesc desc;
      if (!decoder_.Init(data, &desc)) {
        LOG("Failed to init QOA stream for ", sound->name);
        return false;
      }
      channels_ = desc.channels;
      LOG("QOA stream ", sound->name, ", channels = ", desc.channels,
          ", sample rate = ", desc.samplerate, ", samples = ", desc.samples);
      return true;
    }

    size_t Load(float* output, size_t samples_per_channel, size_t channels) {
      size_t total_needed = samples_per_channel * channels;
      size_t written = 0;

      while (written < total_needed) {
        // Drain buffered frame data first.
        while (frame_pos_ < frame_samples_ * channels_ &&
               written < total_needed) {
          output[written++] =
              static_cast<float>(frame_buffer_[frame_pos_++]) / 32768.0f;
        }
        if (written >= total_needed) break;

        // Decode next frame.
        frame_samples_ = decoder_.DecodeFrame(frame_buffer_, kQoaFrameLen);
        frame_pos_ = 0;
        if (frame_samples_ == 0) return written / channels;  // EOF
      }
      return samples_per_channel;
    }

    bool Rewind() {
      decoder_.Rewind();
      frame_pos_ = 0;
      frame_samples_ = 0;
      return true;
    }

    bool Deinit() { return true; }

   private:
    QoaStreamDecoder decoder_;
    uint32_t channels_ = 0;
    int16_t frame_buffer_[kQoaFrameLen * kQoaMaxChannels];
    size_t frame_pos_ = 0;
    size_t frame_samples_ = 0;
  };

  // PCM sampler: plays from a pre-decoded int16_t buffer (for effects).
  class PcmSampler {
   public:
    bool Init(Slice<int16_t> samples, uint32_t channels) {
      samples_ = samples;
      channels_ = channels;
      pos_ = 0;
      return true;
    }

    size_t Load(float* output, size_t samples_per_channel, size_t channels) {
      size_t total_needed = samples_per_channel * channels;
      size_t written = 0;
      while (written < total_needed && pos_ < samples_.size()) {
        output[written++] = static_cast<float>(samples_[pos_++]) / 32768.0f;
      }
      return written / channels;
    }

    bool Rewind() {
      pos_ = 0;
      return true;
    }

    bool Deinit() { return true; }

   private:
    Slice<int16_t> samples_;
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
    }

    size_t Load(float* output, size_t samples_per_channel, size_t channels) {
      if (!playing_) return 0;

      size_t samples = samples_per_channel * channels;

      size_t read = 0;

      for (; read < samples;) {
        if (pos_ >= kBufferSizeInSamples) {
          const size_t samples_read =
              cb_.Load(samples_, kBufferSizeInSamples / channels, channels);
          // EOF.
          if (samples_read == 0) {
            Stop();
            return read;
          }
          pos_ = 0;
        }
        size_t to_copy = std::min(samples - read, kBufferSizeInSamples - pos_);
        while (to_copy-- > 0) {
          output[read++] = gain_ * samples_[pos_++];
        }
      }
      return read;
    }

    void Start() {
      playing_ = true;
      pos_ = 0;
    }

    void Stop() {
      playing_ = false;
      cb_.Rewind();
    }

    void Pause() { playing_ = false; }

    bool IsPlaying() const { return playing_; }

    bool OnReload(const DbAssets::Sound* sound) {
      if (StringIntern(sound->name) != handle_) return true;
      // Rewind on reload — the asset data may have changed.
      cb_.Rewind();
      return true;
    }

    void Gain(float f) { gain_ = f; }

    void SetOwnership(Ownership ownership) { ownership_ = ownership; }
    bool IsManaged() const { return ownership_ == Ownership::kManaged; }

   private:
    const size_t kBufferSizeInSamples = sizeof(samples_) / sizeof(samples_[0]);

    uint32_t handle_;
    Callbacks cb_;
    bool playing_ = false;
    Ownership ownership_ = Ownership::kManaged;
    float gain_ = 1.0;
    float samples_[2048];
    size_t pos_;
  };

  // Find or allocate a stream slot.
  size_t FindStreamSlot();

  // Cached decoded PCM data for effects, keyed by asset name.
  struct DecodedEffect {
    FixedArray<int16_t> pcm;
    uint32_t channels;

    DecodedEffect(FixedArray<int16_t>&& p, uint32_t c)
        : pcm(std::move(p)), channels(c) {}
  };

  FixedArray<float> buffer_;
  SDL_mutex* mu_ = nullptr;
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
