#include "sound.h"

#include <algorithm>

#include "assets.h"

namespace G {

uint32_t Sound::AllocStream() {
  if (free_head_ != kNullIndex) {
    uint32_t idx = free_head_;
    free_head_ = streams_[idx].next_free_;
    streams_[idx].next_free_ = kNullIndex;
    return idx;
  }
  if (stream_ < kMaxStreams) return stream_++;
  return kNullIndex;
}

void Sound::FreeStream(uint32_t idx) {
  auto& s = streams_[idx];
  s.DeinitSampler();
  if (s.is_vorbis_)
    vorbis_alloc_.Dealloc(reinterpret_cast<VorbisSampler*>(s.SamplerPtr()));
  else
    wavs_alloc_.Dealloc(reinterpret_cast<WavSampler*>(s.SamplerPtr()));
  s.next_free_ = free_head_;
  free_head_ = idx;
}

bool Sound::AddSource(std::string_view name, Source* source, bool auto_free) {
  DbAssets::Sound sound;
  if (!sounds_.Lookup(name, &sound)) {
    LOG("Unknown sound ", name);
    return false;
  }
  LockMutex l(mu_);
  uint32_t slot = AllocStream();
  if (slot == kNullIndex) {
    LOG("Maximum number of streams exceeded");
    return false;
  }
  if (HasSuffix(sound.name, ".ogg")) {
    LOG("Loading vorbis source ", sound.name);
    auto* vorbis = vorbis_alloc_.Alloc();
    if (!vorbis->Init(&sound)) {
      return false;
    }
    streams_[slot].InitFromStream(&sound, vorbis);
    streams_[slot].is_vorbis_ = true;
  } else if (HasSuffix(sound.name, ".wav")) {
    LOG("Loading WAV source ", sound.name);
    auto* wav = wavs_alloc_.New();
    if (!wav->Init(&sound)) {
      return false;
    }
    streams_[slot].InitFromStream(&sound, wav);
    streams_[slot].is_vorbis_ = false;
  } else {
    LOG("Unsupported sound format: ", sound.name);
    return false;
  }
  streams_[slot].auto_free_ = auto_free;
  *source = slot;
  return true;
}

bool Sound::SetSourceGain(Source source, float gain) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  if (streams_[source].IsOnFreeList()) return false;
  streams_[source].Gain(gain);
  return true;
}

bool Sound::StartChannel(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  if (streams_[source].IsOnFreeList()) return false;
  streams_[source].Start();
  return true;
}

bool Sound::Stop(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  if (streams_[source].IsOnFreeList()) return false;
  streams_[source].Stop();
  return true;
}

void Sound::LoadSound(const DbAssets::Sound& sound) {
  LockMutex l(mu_);
  TIMER("Loading sound ", sound.name);
  sounds_.Insert(sound.name, sound);
  for (size_t i = 0; i < stream_; ++i) {
    if (streams_[i].IsOnFreeList()) continue;
    if (!streams_[i].OnReload(&sound)) {
      return;
    }
  }
}

void Sound::SoundCallback(float* result, size_t samples_per_channel,
                           size_t channels) {
  LockMutex l(mu_);
  const size_t samples = samples_per_channel * channels;
  std::memset(result, 0, samples * sizeof(float));
  for (size_t i = 0; i < stream_; ++i) {
    auto& stream = streams_[i];
    if (stream.IsOnFreeList()) continue;
    size_t read = stream.Load(buffer_.data(), samples_per_channel, channels);
    for (size_t j = 0; j < read; ++j) {
      result[j] += buffer_[j];
    }
    if (stream.auto_free_ && !stream.IsPlaying()) {
      FreeStream(i);
    }
  }
  for (size_t i = 0; i < samples; ++i) {
    result[i] *= global_gain_;
  }
}

}  // namespace G
