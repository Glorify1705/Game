#include "sound.h"

#include <algorithm>

#include "SDL_mixer.h"
#include "assets.h"

namespace G {

void Sound::VorbisStream::Init(const DbAssets::Sound* sound) {
  if (vorbis_ != nullptr) Deinit();
  LOG("Initializing to play ", sound->name);
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
  size_t read;
  for (read = 0; read < samples;) {
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
  return read;
}

int Sound::AddSource(std::string_view name) {
  DbAssets::Sound sound;
  if (!sounds_.Lookup(name, &sound)) {
    LOG("Unknown sound ", name);
    return -1;
  }
  auto* source = allocator_->New<VorbisStream>();
  CHECK(source != nullptr);
  source->Init(&sound);
  {
    LockMutex l(mu_);
    sources_.Push(source);
  }
  return sources_.size() - 1;
}

void Sound::LoadSound(const DbAssets::Sound& sound) {
  LockMutex l(mu_);
  // Find if any of the tracks are using this version.
  // In that case they need to be reinit and restarted.
  for (size_t i = 0; i < sources_.size(); ++i) {
    auto* source = sources_[i];
    if (source->source_name() == sound.name) {
      source->Init(&sound);
      source->Stop();
    }
  }
  sounds_.Insert(sound.name, sound);
}

void Sound::SoundCallback(float* result, size_t samples) {
  LockMutex l(mu_);
  for (size_t i = 0; i < sources_.size(); ++i) {
    auto* source = sources_[i];
    size_t read = source->Load(buffer_.data(), samples);
    for (size_t i = 0; i < read; ++i) {
      result[i] += buffer_[i];
    }
  }
}

}  // namespace G
