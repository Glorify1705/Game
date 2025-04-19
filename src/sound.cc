#include "sound.h"

#include <algorithm>

#include "assets.h"

namespace G {

bool Sound::AddSource(std::string_view name, Source* source) {
  DbAssets::Sound sound;
  if (!sounds_.Lookup(name, &sound)) {
    LOG("Unknown sound ", name);
    return false;
  }
  if (HasSuffix(sound.name, ".ogg")) {
    LockMutex l(mu_);
    Stream* stream = &streams_.back();
    auto* vorbis = vorbis_alloc_.Alloc();
    vorbis->Init(&sound);
    stream->InitFromStream(&sound, vorbis);
  } else {
    LockMutex l(mu_);
    Stream* stream = &streams_.back();
    auto* vorbis = wavs_alloc_.Alloc();
    vorbis->Init(&sound);
    stream->InitFromStream(&sound, vorbis);
  }
  *source = streams_.size() - 1;
  return true;
}

bool Sound::SetSourceGain(Source source, float gain) {
  LockMutex l(mu_);
  if (source >= streams_.size()) return false;
  streams_[source].Gain(gain);
  return true;
}

bool Sound::StartChannel(Source source) {
  LockMutex l(mu_);
  if (source >= streams_.size()) return false;
  streams_[source].Start();
  return true;
}

bool Sound::Stop(Source source) {
  LockMutex l(mu_);
  streams_[source].Start();
  if (source >= streams_.size()) return false;
  streams_[source].Stop();
  return true;
}

void Sound::LoadSound(const DbAssets::Sound& sound) {
  LockMutex l(mu_);
  TIMER("Loading ", sound.name);
  for (auto& stream : streams_) {
    if (!stream.OnReload(&sound)) {
      return;
    }
  }
  sounds_.Insert(sound.name, sound);
}

void Sound::SoundCallback(float* result, size_t samples_per_channel,
                          size_t channels) {
  LockMutex l(mu_);
  const size_t samples = samples_per_channel * channels;
  std::memset(result, 0, samples * sizeof(float));
  for (auto& stream : streams_) {
    size_t read = stream.Load(buffer_.data(), samples_per_channel, samples);
    for (size_t i = 0; i < read; ++i) {
      result[i] += buffer_[i];
    }
  }
  for (size_t i = 0; i < samples; ++i) {
    result[i] *= global_gain_;
  }
}

}  // namespace G
