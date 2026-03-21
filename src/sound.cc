#include "sound.h"

#include "assets.h"

namespace G {

size_t Sound::FindStreamSlot() {
  size_t slot = stream_;
  for (size_t i = 0; i < stream_; ++i) {
    if (!streams_[i].IsManaged() && !streams_[i].IsPlaying()) {
      slot = i;
      break;
    }
  }
  return slot;
}

bool Sound::AddSource(std::string_view name, Source* source,
                      Ownership ownership) {
  DbAssets::Sound sound;
  if (!sounds_.Lookup(name, &sound)) {
    LOG("Unknown sound ", name);
    return false;
  }
  LockMutex l(mu_);
  size_t slot = FindStreamSlot();
  if (slot >= kMaxStreams) {
    LOG("Maximum number of streams exceeded");
    return false;
  }

  auto* sampler = qoa_alloc_.Alloc();
  if (!sampler->Init(&sound)) {
    return false;
  }
  streams_[slot].InitFromStream(&sound, sampler);
  streams_[slot].SetOwnership(ownership);
  *source = slot;
  if (slot == stream_) stream_++;
  return true;
}

bool Sound::AddEffect(std::string_view name, Source* source,
                      Ownership ownership) {
  LockMutex l(mu_);
  DbAssets::Sound sound;
  if (!sounds_.Lookup(name, &sound)) {
    LOG("Unknown sound ", name);
    return false;
  }
  size_t slot = FindStreamSlot();
  if (slot >= kMaxStreams) {
    LOG("Maximum number of streams exceeded");
    return false;
  }

  // Check if we already have this effect decoded.
  DecodedEffect* cached = nullptr;
  if (!effect_cache_.Lookup(name, &cached)) {
    // Decode the full QOA and convert to float upfront.
    TIMER("Decoding effect ", name);
    ByteSlice data(sound.contents, sound.size);
    QoaDesc desc;
    FixedArray<int16_t> pcm = QoaDecode(data, &desc, qoa_alloc_.allocator());
    if (pcm.empty()) {
      LOG("Failed to decode QOA for effect ", name);
      return false;
    }
    FixedArray<float> floats(pcm.size(), qoa_alloc_.allocator());
    floats.Resize(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
      floats[i] = static_cast<float>(pcm[i]) / 32768.0f;
    }
    cached = qoa_alloc_.allocator()->New<DecodedEffect>(std::move(floats),
                                                        desc.channels);
    effect_cache_.Insert(name, cached);
  }

  auto* sampler = pcm_alloc_.Alloc();
  Slice<float> samples(cached->pcm.cdata(), cached->pcm.size());
  sampler->Init(samples, cached->channels);
  streams_[slot].InitFromStream(&sound, sampler);
  streams_[slot].SetOwnership(ownership);
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
  // Re-decode cached effect if the asset was reloaded.
  DecodedEffect* cached = nullptr;
  if (effect_cache_.Lookup(sound.name, &cached)) {
    ByteSlice data(sound.contents, sound.size);
    QoaDesc desc;
    FixedArray<int16_t> pcm = QoaDecode(data, &desc, qoa_alloc_.allocator());
    if (!pcm.empty()) {
      FixedArray<float> floats(pcm.size(), qoa_alloc_.allocator());
      floats.Resize(pcm.size());
      for (size_t i = 0; i < pcm.size(); ++i) {
        floats[i] = static_cast<float>(pcm[i]) / 32768.0f;
      }
      auto* new_cached = qoa_alloc_.allocator()->New<DecodedEffect>(
          std::move(floats), desc.channels);
      effect_cache_.Insert(sound.name, new_cached);
    }
  }
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
