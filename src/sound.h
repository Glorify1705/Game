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
      : allocator_(allocator),
        buffer_(2 * 4 * spec.samples, allocator),
        sounds_(allocator),
        vorbis_(64, allocator),
        wavs_(64, allocator) {
    buffer_.Resize(buffer_.capacity());
    mu_ = SDL_CreateMutex();
  }

  ~Sound() {
    if (mu_ != nullptr) SDL_DestroyMutex(mu_);
  }

  struct Source {
    enum Type : uint16_t { kWav = 0, kOgg = 1 };

    Type type = kWav;
    uint16_t index = 0;

    uint32_t AsNum() const { return (uint32_t(type) << 16) | index; }

    static Source FromNum(uint32_t s) {
      Source result;
      result.type = static_cast<Type>(s >> 16);
      result.index = s & 0xFFFF;
      return result;
    }
  };

  bool AddSource(std::string_view name, Source* source);

  bool SetGain(Source source, float gain);

  bool StartChannel(Source source);

  bool Stop(Source source);

  void StopAll() {
    LockMutex l(mu_);
    for (auto* source : vorbis_) source->Stop();
    for (auto* source : wavs_) source->Stop();
  }

  void LoadSound(const DbAssets::Sound& sound);

  void SoundCallback(float* result, size_t samples);

 private:
  class WavStream {
   public:
    WavStream() : allocator_(decode_buffer, sizeof(decode_buffer)) {
      callbacks_.pUserData = this;
      callbacks_.onMalloc = Alloc;
      callbacks_.onFree = Free;
      callbacks_.onRealloc = nullptr;
      gain_ = 1.0f;
      pos_ = 0;
    }

    void Init(const DbAssets::Sound* sound);

    void Start() { playing_ = true; }

    void Stop() { playing_ = false; }

    void SetGain(float gain) { gain_ = gain; }

    size_t Load(float* output, size_t samples);

    std::string_view source_name() const { return name_; }

   private:
    inline static constexpr size_t kDecoderMemorySize = Kilobytes(256);

    const size_t kBufferSize = sizeof(buffer_) / sizeof(float);

    static void* Alloc(size_t size, void* ud) {
      auto* stream = reinterpret_cast<WavStream*>(ud);
      return stream->allocator_.Alloc(size, /*align=*/1);
    }

    static void Free(void* ptr, void* ud) { (void)ptr, (void)ud; }

    uint8_t decode_buffer[kDecoderMemorySize];
    ArenaAllocator allocator_;

    float buffer_[4096];
    bool playing_;
    size_t pos_;
    float gain_ = 1.0;
    std::string_view name_;
    drwav_allocation_callbacks callbacks_;
    drwav wav_;
  };

  class VorbisStream {
   public:
    VorbisStream() {
      vorbis_ = nullptr;
      playing_ = false;
      gain_ = 1.0;
      pos_ = 0;
    }

    void Init(const DbAssets::Sound* sound);

    void Deinit() {
      name_ = std::string_view();
      stb_vorbis_close(vorbis_);
      vorbis_ = nullptr;
    }

    void Start() {
      Rewind();
      playing_ = true;
    }

    void Stop() {
      playing_ = false;
      Rewind();
    }

    void Rewind() { pos_ = 0; }

    void SetGain(float gain) { gain_ = gain; }

    size_t Load(float* output, size_t samples);

    std::string_view source_name() const { return name_; }

   private:
    inline static constexpr size_t kDecoderMemorySize = Kilobytes(256);

    const size_t kBufferSize = sizeof(buffer_) / sizeof(float);

    std::string_view name_;
    float buffer_[4096];
    size_t pos_ = 0;
    bool playing_ = false;
    float gain_ = 1.0;
    char vorbis_memory_[kDecoderMemorySize];
    stb_vorbis_alloc vorbis_alloc_;
    stb_vorbis* vorbis_;
  };

  Allocator* allocator_;
  FixedArray<float> buffer_;
  SDL_mutex* mu_ = nullptr;
  Dictionary<DbAssets::Sound> sounds_;
  FixedArray<VorbisStream*> vorbis_;
  FixedArray<WavStream*> wavs_;
};

}  // namespace G

#endif  // _GAME_SOUND_H
