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
#include "libraries/dr_wav.h"
#include "libraries/stb_vorbis.h"
#include "thread.h"

namespace G {

class Sound {
 public:
  explicit Sound(const SDL_AudioSpec& spec, Allocator* allocator)
      : buffer_(spec.channels * spec.samples, allocator),
        sounds_(allocator),
        streams_(256, allocator),
        vorbis_(256, allocator),
        wavs_(256, allocator),
        vorbis_alloc_(allocator),
        wavs_alloc_(allocator) {
    buffer_.Resize(buffer_.capacity());
    mu_ = SDL_CreateMutex();
  }

  ~Sound() {
    if (mu_ != nullptr) SDL_DestroyMutex(mu_);
  }

  using Source = uint32_t;

  bool AddSource(std::string_view name, Source* source);

  bool SetSourceGain(Source source, float gain);

  void SetGlobalGain(float gain) { global_gain_ = gain; }

  bool StartChannel(Source source);

  bool Stop(Source source);

  void StopAll() {
    LockMutex l(mu_);
    for (auto& stream : streams_) stream.Stop();
  }

  void LoadSound(const DbAssets::Sound& sound);

  void SoundCallback(float* result, size_t samples_per_channel,
                     size_t channels);

 private:
  class WavSampler {
   public:
    WavSampler() : allocator_(decode_buffer_, sizeof(decode_buffer_)) {
      callbacks_.pUserData = this;
      callbacks_.onMalloc = Alloc;
      callbacks_.onFree = Free;
      callbacks_.onRealloc = nullptr;
    }

    bool Init(const DbAssets::Sound* sound) {
      LOG("Initializing ", sound->name);
      std::memset(&wav_, 0, sizeof(wav_));
      if (!drwav_init_memory(&wav_, sound->contents, sound->size,
                             &callbacks_)) {
        return false;
      }
      const auto& fmt = wav_.fmt;
      LOG("Channels = ", fmt.channels);
      LOG("Sample rate = ", fmt.sampleRate);
      return true;
    }

    size_t Load(float* output, size_t samples_per_channel, size_t channels) {
      DCHECK(channels == 2);
      return drwav_read_pcm_frames_f32(&wav_, samples_per_channel, output);
    }

    bool Rewind() {
      drwav_seek_to_pcm_frame(&wav_, 0);
      return true;
    }

    bool Deinit() {
      drwav_uninit(&wav_);
      return true;
    }

   private:
    inline static constexpr size_t kDecoderMemorySize = Kilobytes(256);

    static void* Alloc(size_t size, void* ud) {
      auto* stream = reinterpret_cast<WavSampler*>(ud);
      return stream->allocator_.Alloc(size, /*align=*/1);
    }

    static void Free(void* ptr, void* ud) { (void)ptr, (void)ud; }

    uint8_t decode_buffer_[kDecoderMemorySize];
    ArenaAllocator allocator_;
    drwav_allocation_callbacks callbacks_;
    drwav wav_;
  };

  class VorbisSampler {
   public:
    bool Init(const DbAssets::Sound* sound) {
      TIMER("Initializing to play ", sound->name);
      int error;
      vorbis_alloc_.alloc_buffer = vorbis_memory_;
      vorbis_alloc_.alloc_buffer_length_in_bytes = kDecoderMemorySize;
      vorbis_ = stb_vorbis_open_memory(sound->contents, sound->size, &error,
                                       &vorbis_alloc_);
      CHECK(vorbis_ != nullptr, "Failed to open ", sound->name, ": ", error);
      stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
      LOG("Opened file ", sound->name, ", channels = ", info.channels,
          ", sample rate = ", info.sample_rate);
      LOG("Memory required = ", info.temp_memory_required, ", ",
          info.setup_memory_required, ", ", info.setup_temp_memory_required);
      return true;
    }

    size_t Load(float* output, size_t samples_per_channel, size_t channels) {
      DCHECK(channels == 2);
      return stb_vorbis_get_samples_float_interleaved(vorbis_, channels, output,
                                                      samples_per_channel);
    }

    bool Rewind() {
      stb_vorbis_seek(vorbis_, 0);
      return true;
    }

    bool Deinit() {
      stb_vorbis_close(vorbis_);
      return true;
    }

   private:
    inline static constexpr size_t kDecoderMemorySize = Kilobytes(192);

    char vorbis_memory_[kDecoderMemorySize];
    stb_vorbis_alloc vorbis_alloc_;
    stb_vorbis* vorbis_;
  };

  class Stream {
   public:
    struct Callbacks {
      size_t (*load)(float*, size_t, size_t, void*);
      void (*rewind)(void*);
      void (*deinit)(void*);
      bool (*reload)(void*);
      void* ud;

      size_t Load(float* a, size_t b, size_t c) { return load(a, b, c, ud); }

      void Rewind() { rewind(ud); }

      void Deinit() { deinit(ud); }

      bool Reload(const DbAssets::Sound* sound) { return reload(ud); }
    };

    template <typename T>
    class CallbackMaker {
     public:
      static Callbacks callbacks(T* ptr) {
        Callbacks c;
        c.ud = ptr;
        c.load = Load;
        c.deinit = Deinit;
        c.deinit = Rewind;
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

      static bool Reload(const DbAssets::Sound* sound, void* ud) {
        return reinterpret_cast<T*>(ud)->Reload(sound);
      }
    };

    template <typename T>
    void InitFromStream(const DbAssets::Sound* sound, T* stream) {
      cb_ = CallbackMaker<T>::callbacks(stream);
      handle_ = StringIntern(sound->name);
    }

    size_t Load(float* output, size_t samples_per_channel, size_t channels) {
      if (!playing_) return 0;
      size_t samples = samples_per_channel * channels;
      for (size_t read = 0; read < samples;) {
        if (pos_ >= kBufferSize) {
          const size_t samples_read =
              cb_.Load(samples_, samples_per_channel, channels);
          // EOF.
          if (samples_read == 0) {
            Stop();
            return read;
          }
          pos_ = 0;
        }
        size_t to_copy = std::min(samples - read, kBufferSize - pos_);
        for (size_t p = 0; p < to_copy; p++) {
          output[read++] = gain_ * samples_[pos_++];
        }
      }
      return 0;
    }

    void Start() { playing_ = true; }

    void Stop() {
      playing_ = false;
      cb_.Rewind();
    }

    void Pause() { playing_ = false; }

    bool OnReload(const DbAssets::Sound* sound) {
      if (StringIntern(sound->name) != handle_) return true;
      return cb_.Reload(sound);
    }

    void Gain(float f) { gain_ = f; }

   private:
    const size_t kBufferSize = sizeof(samples_) / sizeof(samples_[0]);

    uint32_t handle_;
    Callbacks cb_;
    bool playing_ = false;
    float gain_ = 1.0;
    float samples_[1024];
    size_t pos_;
  };

  FixedArray<float> buffer_;
  SDL_mutex* mu_ = nullptr;
  Dictionary<DbAssets::Sound> sounds_;
  FixedArray<Stream> streams_;
  FixedArray<VorbisSampler*> vorbis_;
  FixedArray<WavSampler*> wavs_;
  FreeList<VorbisSampler> vorbis_alloc_;
  FreeList<WavSampler> wavs_alloc_;
  float global_gain_ = 1.0;
};

}  // namespace G

#endif  // _GAME_SOUND_H
