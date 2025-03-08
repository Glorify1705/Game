#include "sound.h"

#include <algorithm>

#include "assets.h"

namespace G {

void Sound::WavStream::Init(const DbAssets::Sound* sound) {
  LOG("Initializing ", sound->name);
  std::memset(&wav_, 0, sizeof(wav_));
  CHECK(drwav_init_memory(&wav_, sound->contents, sound->size, &callbacks_));
  const auto& fmt = wav_.fmt;
  LOG("Channels = ", fmt.channels);
  LOG("Sample rate = ", fmt.sampleRate);
  // Samples are always interleaved, and we have 2 channels.
  CHECK(drwav_read_pcm_frames_f32(&wav_, kMaxSamples / 2, buffer_) <=
        kMaxSamples);
  name_ = sound->name;
}

size_t Sound::WavStream::Load(float* output, size_t samples) {
  if (!playing_) return 0;
  for (size_t read = 0; read < samples;) {
    if (pos_ >= kMaxSamples) {
      size_t samples_read =
          drwav_read_pcm_frames_f32(&wav_, kMaxSamples / 2, buffer_);
      if (samples_read == 0) {  // EOF.
        Stop();
        return read;
      }
      pos_ = 0;
    }
    size_t to_copy = std::min(samples - read, sizeof(buffer_) - pos_);
    for (size_t p = 0; p < to_copy; p++) {
      output[read++] = gain_ * buffer_[pos_++];
    }
  }
  return samples;
}

void Sound::VorbisStream::Init(const DbAssets::Sound* sound) {
  if (vorbis_ != nullptr) Deinit();
  TIMER("Initializing to play ", sound->name);
  playing_ = false;
  gain_ = 1.0;
  name_ = sound->name;
  pos_ = 0;
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
  // Load the first set of samples.
  stb_vorbis_get_samples_float_interleaved(vorbis_, 2, buffer_, kBufferSize);
}

size_t Sound::VorbisStream::Load(float* output, size_t samples) {
  if (!playing_) return 0;
  for (size_t read = 0; read < samples;) {
    if (pos_ >= kBufferSize) {
      size_t samples_read = stb_vorbis_get_samples_float_interleaved(
          vorbis_, 2, buffer_, kBufferSize);  // two audio channels.
      if (samples_read == 0) {                // EOF.
        Stop();
        return read;
      }
      pos_ = 0;
    }
    size_t to_copy = std::min(samples - read, kBufferSize - pos_);
    for (size_t p = 0; p < to_copy; p++) {
      output[read++] = gain_ * buffer_[pos_++];
    }
  }
  return samples;
}

bool Sound::AddSource(std::string_view name, Source* source) {
  DbAssets::Sound sound;
  if (!sounds_.Lookup(name, &sound)) {
    LOG("Unknown sound ", name);
    return false;
  }
  Source result;
  if (HasSuffix(sound.name, ".ogg")) {
    result.type = Source::kOgg;
    auto* source = allocator_->New<VorbisStream>();
    CHECK(source != nullptr);
    source->Init(&sound);
    {
      LockMutex l(mu_);
      vorbis_.Push(source);
      result.index = vorbis_.size() - 1;
    }
  } else {
    result.type = Source::kWav;
    auto* source = allocator_->New<WavStream>();
    CHECK(source != nullptr);
    source->Init(&sound);
    {
      LockMutex l(mu_);
      wavs_.Push(source);
      result.index = wavs_.size() - 1;
    }
  }
  *source = result;
  return true;
}

bool Sound::SetSourceGain(Source source, float gain) {
  LockMutex l(mu_);
  switch (source.type) {
    case Source::kOgg:
      if (source.index >= vorbis_.size()) return false;
      vorbis_[source.index]->SetGain(gain);
      return true;
    case Source::kWav:
      if (source.index >= wavs_.size()) return false;
      wavs_[source.index]->SetGain(gain);
      return true;
  }
  return false;
}

bool Sound::StartChannel(Source source) {
  LockMutex l(mu_);
  switch (source.type) {
    case Source::kOgg:
      if (source.index >= vorbis_.size()) return false;
      vorbis_[source.index]->Start();
      return true;
    case Source::kWav:
      if (source.index >= wavs_.size()) return false;
      wavs_[source.index]->Start();
      return true;
  }
}

bool Sound::Stop(Source source) {
  LockMutex l(mu_);
  switch (source.type) {
    case Source::kOgg:
      if (source.index >= vorbis_.size()) return false;
      vorbis_[source.index]->Stop();
      return true;
    case Source::kWav:
      if (source.index >= wavs_.size()) return false;
      wavs_[source.index]->Stop();
      return true;
  }
}

void Sound::LoadSound(const DbAssets::Sound& sound) {
  LockMutex l(mu_);
  TIMER("Loading ", sound.name);
  // Find if any of the tracks are using this version.
  // In that case they need to be reinit and restarted.
  for (auto& source : vorbis_) {
    if (source->source_name() == sound.name) {
      source->Init(&sound);
      source->Stop();
    }
  }
  for (auto& source : wavs_) {
    if (source->source_name() == sound.name) {
      source->Init(&sound);
      source->Stop();
    }
  }
  sounds_.Insert(sound.name, sound);
}

void Sound::SoundCallback(float* result, size_t samples) {
  LockMutex l(mu_);
  for (size_t i = 0; i < vorbis_.size(); ++i) {
    auto* source = vorbis_[i];
    size_t read = source->Load(buffer_.data(), samples);
    for (size_t i = 0; i < read; ++i) {
      result[i] += buffer_[i];
    }
  }
  for (size_t i = 0; i < wavs_.size(); ++i) {
    auto* source = wavs_[i];
    size_t read = source->Load(buffer_.data(), samples);
    for (size_t i = 0; i < read; ++i) {
      result[i] += buffer_[i];
    }
  }
  for (size_t i = 0; i < samples; ++i) {
    result[i] *= global_gain_;
  }
}

}  // namespace G
