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
#include "libraries/stb_vorbis.h"
#include "thread.h"

namespace G {

class Sound {
 public:
  explicit Sound(const SDL_AudioSpec& spec, Allocator* allocator)
      : allocator_(allocator),
        buffer_(2 * 4 * spec.samples, allocator),
        sounds_(allocator),
        sources_(16, allocator) {
    buffer_.Resize(buffer_.capacity());
    mu_ = SDL_CreateMutex();
  }

  ~Sound() {
    if (mu_ != nullptr) SDL_DestroyMutex(mu_);
  }

  int AddSource(std::string_view name);

  bool SetGain(size_t source, float gain) {
    LockMutex l(mu_);
    if (source >= sources_.size()) return false;
    sources_[source]->SetGain(gain);
    return true;
  }

  bool StartChannel(size_t source) {
    LockMutex l(mu_);
    if (source >= sources_.size()) return false;
    sources_[source]->Start();
    return true;
  }

  bool Stop(size_t source) {
    LockMutex l(mu_);
    if (source >= sources_.size()) return false;
    sources_[source]->Stop();
    return true;
  }

  void StopAll() {
    LockMutex l(mu_);
    for (auto* sources : sources_) sources->Stop();
  }

  void LoadSound(const DbAssets::Sound& sound);

  void SoundCallback(float* result, size_t samples);

 private:
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
  FixedArray<VorbisStream*> sources_;
};

}  // namespace G

#endif  // _GAME_SOUND_H
