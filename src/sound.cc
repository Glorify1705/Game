#include "sound.h"

#include <algorithm>

#include "assets.h"

namespace G {

bool Sound::AddSource(std::string_view name, Source* source, bool auto_free) {
  DbAssets::Sound sound;
  if (!sounds_.Lookup(name, &sound)) {
    LOG("Unknown sound ", name);
    return false;
  }
  LockMutex l(mu_);
  // Reuse a finished fire-and-forget slot, or allocate a new one.
  size_t slot = stream_;
  for (size_t i = 0; i < stream_; ++i) {
    if (streams_[i].auto_free && !streams_[i].IsPlaying()) {
      slot = i;
      break;
    }
  }
  if (slot >= kMaxStreams) {
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
  } else if (HasSuffix(sound.name, ".wav")) {
    LOG("Loading WAV source ", sound.name);
    auto* wav = wavs_alloc_.New();
    if (!wav->Init(&sound)) {
      return false;
    }
    streams_[slot].InitFromStream(&sound, wav);
  } else {
    LOG("Unsupported sound format: ", sound.name);
    return false;
  }
  streams_[slot].auto_free = auto_free;
  *source = slot;
  if (slot == stream_) stream_++;
  return true;
}

bool Sound::SetSourceGain(Source source, float gain) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].Gain(gain);
  return true;
}

bool Sound::StartChannel(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].Start();
  return true;
}

bool Sound::Stop(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].Stop();
  return true;
}

void Sound::LoadSound(const DbAssets::Sound& sound) {
  LockMutex l(mu_);
  TIMER("Loading sound ", sound.name);
  sounds_.Insert(sound.name, sound);
  for (size_t i = 0; i < stream_; ++i) {
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
    size_t read = stream.Load(buffer_.data(), samples_per_channel, channels);
    for (size_t j = 0; j < read; ++j) {
      result[j] += buffer_[j];
    }
  }
  for (size_t i = 0; i < samples; ++i) {
    result[i] *= global_gain_;
  }
}

}  // namespace G
