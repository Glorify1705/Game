#pragma once
#ifndef _GAME_SOUND_H
#define _GAME_SOUND_H

#include <algorithm>
#include <cmath>

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
  bool SetLoop(Source source, bool loop);
  bool SetPitch(Source source, float pitch);
  bool SetPan(Source source, float pan);

  void SetGlobalGain(float gain) { global_gain_ = gain; }

  bool StartChannel(Source source);

  bool Stop(Source source);
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
        while (frame_pos_ < frame_len_ && written < total_needed) {
          output[written++] = frame_buffer_[frame_pos_++];
        }
        if (written >= total_needed) break;

        // Decode next frame and convert to float up front.
        int16_t raw[kQoaFrameLen * kQoaMaxChannels];
        frame_len_ = decoder_.DecodeFrame(raw, kQoaFrameLen) * channels_;
        frame_pos_ = 0;
        if (frame_len_ == 0) return written / channels;  // EOF
        for (size_t i = 0; i < frame_len_; ++i) {
          frame_buffer_[i] = static_cast<float>(raw[i]) / 32768.0f;
        }
      }
      return samples_per_channel;
    }

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

    size_t Load(float* output, size_t samples_per_channel, size_t channels) {
      size_t total_needed = samples_per_channel * channels;
      size_t written = 0;
      while (written < total_needed && pos_ < samples_.size()) {
        output[written++] = samples_[pos_++];
      }
      return written / channels;
    }

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

    size_t Load(float* output, size_t samples_per_channel, size_t channels) {
      if (!playing_) return 0;

      const size_t total_output = samples_per_channel * channels;
      size_t written = 0;

      while (written < total_output) {
        // Refill internal buffer from sampler when exhausted.
        if (pos_ >= buf_len_) {
          const size_t frames_read =
              cb_.Load(samples_, kBufferSizeInSamples / source_channels_,
                       source_channels_);
          if (frames_read == 0) {
            if (loop_) {
              HandleLoopCrossfade(output, written, total_output);
              cb_.Rewind();
              // Skip past the samples we already crossfaded in.
              if (loop_head_ready_) {
                cb_.Load(samples_, loop_head_len_ / source_channels_,
                         source_channels_);
              }
              pos_ = kBufferSizeInSamples;  // Force refill on next iteration.
              buf_len_ = kBufferSizeInSamples;
              fractional_pos_ = 0.0f;
              continue;
            }
            Stop();
            return written;
          }
          buf_len_ = frames_read * source_channels_;
          pos_ = 0;
          fractional_pos_ = 0.0f;
        }

        // Copy samples from internal buffer to output with gain, pitch,
        // panning, and mono upmix.
        if (pitch_ == 1.0f) {
          CopyDirect(output, written, total_output);
        } else {
          CopyPitched(output, written, total_output);
        }
      }
      return written;
    }

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

    bool OnReload(const DbAssets::Sound* sound) {
      if (StringIntern(sound->name) != handle_) return true;
      cb_.Rewind();
      loop_head_ready_ = false;
      return true;
    }

    void Gain(float f) { gain_ = f; }

    void SetLoop(bool loop) {
      loop_ = loop;
      if (loop && !loop_head_ready_) PrepareLoopHead();
    }

    void SetPitch(float pitch) { pitch_ = std::clamp(pitch, 0.25f, 4.0f); }

    void SetPan(float pan) {
      pan_ = std::clamp(pan, -1.0f, 1.0f);
      float angle = (pan_ + 1.0f) * (static_cast<float>(M_PI) / 4.0f);
      left_gain_ = std::cos(angle);
      right_gain_ = std::sin(angle);
    }

    void SetOwnership(Ownership ownership) { ownership_ = ownership; }
    bool IsManaged() const { return ownership_ == Ownership::kManaged; }

   private:
    // Write one output sample pair (stereo) from a mono or stereo source
    // sample, applying gain and panning.
    void WriteStereoOutput(float* output, size_t& written, float s_left,
                           float s_right) {
      output[written++] = gain_ * left_gain_ * s_left;
      output[written++] = gain_ * right_gain_ * s_right;
    }

    // Copy samples 1:1 (no pitch shift) with panning and mono upmix.
    void CopyDirect(float* output, size_t& written, size_t total_output) {
      if (source_channels_ == 1) {
        while (written + 1 < total_output && pos_ < buf_len_) {
          float s = samples_[pos_++];
          WriteStereoOutput(output, written, s, s);
        }
      } else {
        while (written + 1 < total_output && pos_ + 1 < buf_len_) {
          float l = samples_[pos_];
          float r = samples_[pos_ + 1];
          pos_ += 2;
          WriteStereoOutput(output, written, l, r);
        }
      }
    }

    // Copy samples with pitch-shifted playback using linear interpolation.
    void CopyPitched(float* output, size_t& written, size_t total_output) {
      if (source_channels_ == 1) {
        while (written + 1 < total_output) {
          size_t i = static_cast<size_t>(fractional_pos_);
          if (i + 1 >= buf_len_) break;
          float frac = fractional_pos_ - static_cast<float>(i);
          float s = samples_[i] * (1.0f - frac) + samples_[i + 1] * frac;
          WriteStereoOutput(output, written, s, s);
          fractional_pos_ += pitch_;
        }
        pos_ = static_cast<size_t>(fractional_pos_);
      } else {
        while (written + 1 < total_output) {
          size_t i = static_cast<size_t>(fractional_pos_) * 2;
          if (i + 3 >= buf_len_) break;
          float frac = fractional_pos_ -
                       static_cast<float>(static_cast<size_t>(fractional_pos_));
          float l = samples_[i] * (1.0f - frac) + samples_[i + 2] * frac;
          float r = samples_[i + 1] * (1.0f - frac) + samples_[i + 3] * frac;
          WriteStereoOutput(output, written, l, r);
          fractional_pos_ += pitch_;
        }
        pos_ = static_cast<size_t>(fractional_pos_) * 2;
      }
    }

    // Pre-decode the first kCrossfadeSamples from the source for crossfading.
    void PrepareLoopHead() {
      float saved_samples[kBufferSizeInSamples];
      std::memcpy(saved_samples, samples_, sizeof(samples_));
      size_t saved_pos = pos_;
      size_t saved_buf_len = buf_len_;

      cb_.Rewind();
      size_t frames = cb_.Load(loop_head_, kCrossfadeSamples, source_channels_);
      loop_head_len_ = frames * source_channels_;
      loop_head_ready_ = loop_head_len_ > 0;
      cb_.Rewind();

      // Restore sampler state by re-loading up to the saved position.
      // For simplicity, we just mark the buffer as needing a refill.
      // The caller should handle this appropriately.
      std::memcpy(samples_, saved_samples, sizeof(samples_));
      pos_ = saved_pos;
      buf_len_ = saved_buf_len;
    }

    // Apply crossfade blend at the loop boundary: blend the tail of the
    // current output with the pre-decoded loop head using constant-power law.
    void HandleLoopCrossfade(float* output, size_t written,
                             size_t total_output) {
      if (!loop_head_ready_ || loop_head_len_ == 0) return;

      // Determine how many stereo output samples to crossfade.
      size_t xfade_pairs = loop_head_len_ / source_channels_;
      // Don't crossfade more than what we've already written.
      size_t written_pairs = written / 2;
      if (xfade_pairs > written_pairs) xfade_pairs = written_pairs;

      // Walk backwards from the end of what we've written.
      size_t out_start = written - xfade_pairs * 2;
      for (size_t i = 0; i < xfade_pairs; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(xfade_pairs);
        float angle = t * (static_cast<float>(M_PI) / 2.0f);
        float fade_out = std::cos(angle);
        float fade_in = std::sin(angle);

        size_t out_idx = out_start + i * 2;
        if (source_channels_ == 1) {
          float head = loop_head_[i];
          output[out_idx] =
              fade_out * output[out_idx] + fade_in * gain_ * left_gain_ * head;
          output[out_idx + 1] = fade_out * output[out_idx + 1] +
                                fade_in * gain_ * right_gain_ * head;
        } else {
          float head_l = loop_head_[i * 2];
          float head_r = loop_head_[i * 2 + 1];
          output[out_idx] = fade_out * output[out_idx] +
                            fade_in * gain_ * left_gain_ * head_l;
          output[out_idx + 1] = fade_out * output[out_idx + 1] +
                                fade_in * gain_ * right_gain_ * head_r;
        }
      }
    }

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
