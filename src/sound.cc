#include "sound.h"

#include <cmath>

#include "assets.h"

namespace G {

// -- QoaSampler ---------------------------------------------------------------

bool Sound::QoaSampler::Init(const DbAssets::Sound* sound) {
  TIMER("Initializing QOA stream ", sound->name);
  frame_pos_ = 0;
  frame_len_ = 0;
  ByteSlice data(sound->contents, sound->size);
  QoaDesc desc;
  if (!decoder_.Init(data, &desc)) {
    LOG("Failed to init QOA stream for ", sound->name);
    return false;
  }
  channels_ = desc.channels;
  return true;
}

size_t Sound::QoaSampler::Load(float* output, size_t samples_per_channel,
                               size_t channels) {
  size_t total_needed = samples_per_channel * channels;
  size_t written = 0;

  while (written < total_needed) {
    // Drain buffered frame data first.
    while (frame_pos_ < frame_len_ && written < total_needed) {
      output[written++] = frame_buffer_[frame_pos_++];
    }
    if (written >= total_needed) break;

    // Decode next frame and convert to float up front.
    frame_len_ = decoder_.DecodeFrame(raw_, kQoaFrameLen) * channels_;
    frame_pos_ = 0;
    if (frame_len_ == 0) return written / channels;  // EOF
    for (size_t i = 0; i < frame_len_; ++i) {
      frame_buffer_[i] = static_cast<float>(raw_[i]) / 32768.0f;
    }
  }
  return samples_per_channel;
}

// -- PcmSampler ---------------------------------------------------------------

size_t Sound::PcmSampler::Load(float* output, size_t samples_per_channel,
                               size_t channels) {
  size_t total_needed = samples_per_channel * channels;
  size_t written = 0;
  while (written < total_needed && pos_ < samples_.size()) {
    output[written++] = samples_[pos_++];
  }
  return written / channels;
}

// -- Stream -------------------------------------------------------------------

size_t Sound::Stream::Load(float* output, size_t samples_per_channel,
                           size_t channels) {
  if (!playing_) return 0;

  const size_t total_output = samples_per_channel * channels;
  size_t written = 0;

  while (written < total_output) {
    // Refill internal buffer from sampler when exhausted.
    if (pos_ >= buf_len_) {
      const size_t frames_read = cb_.Load(
          samples_, kBufferSizeInSamples / source_channels_, source_channels_);
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

bool Sound::Stream::OnReload(const DbAssets::Sound* sound) {
  if (StringIntern(sound->name) != handle_) return true;
  cb_.Rewind();
  loop_head_ready_ = false;
  return true;
}

void Sound::Stream::SetLoop(bool loop) {
  loop_ = loop;
  if (loop && !loop_head_ready_) PrepareLoopHead();
}

void Sound::Stream::SetPan(float pan) {
  pan_ = std::clamp(pan, -1.0f, 1.0f);
  float angle = (pan_ + 1.0f) * (static_cast<float>(M_PI) / 4.0f);
  left_gain_ = std::cos(angle);
  right_gain_ = std::sin(angle);
}

void Sound::Stream::CopyDirect(float* output, size_t& written,
                               size_t total_output) {
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

void Sound::Stream::CopyPitched(float* output, size_t& written,
                                size_t total_output) {
  if (source_channels_ == 1) {
    while (written + 1 < total_output) {
      size_t i = static_cast<size_t>(fractional_pos_);
      if (i + 1 >= buf_len_) {
        pos_ = buf_len_;  // Force refill.
        return;
      }
      float frac = fractional_pos_ - static_cast<float>(i);
      float s = samples_[i] * (1.0f - frac) + samples_[i + 1] * frac;
      WriteStereoOutput(output, written, s, s);
      fractional_pos_ += pitch_;
    }
    pos_ = static_cast<size_t>(fractional_pos_);
  } else {
    while (written + 1 < total_output) {
      size_t i = static_cast<size_t>(fractional_pos_) * 2;
      if (i + 3 >= buf_len_) {
        pos_ = buf_len_;  // Force refill.
        return;
      }
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

void Sound::Stream::PrepareLoopHead() {
  float saved_samples[kBufferSizeInSamples];
  std::memcpy(saved_samples, samples_, sizeof(samples_));
  size_t saved_pos = pos_;
  size_t saved_buf_len = buf_len_;

  cb_.Rewind();
  size_t frames = cb_.Load(loop_head_, kCrossfadeSamples, source_channels_);
  loop_head_len_ = frames * source_channels_;
  loop_head_ready_ = loop_head_len_ > 0;
  cb_.Rewind();

  std::memcpy(samples_, saved_samples, sizeof(samples_));
  pos_ = saved_pos;
  buf_len_ = saved_buf_len;
}

void Sound::Stream::HandleLoopCrossfade(float* output, size_t written,
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
      output[out_idx + 1] =
          fade_out * output[out_idx + 1] + fade_in * gain_ * right_gain_ * head;
    } else {
      float head_l = loop_head_[i * 2];
      float head_r = loop_head_[i * 2 + 1];
      output[out_idx] =
          fade_out * output[out_idx] + fade_in * gain_ * left_gain_ * head_l;
      output[out_idx + 1] = fade_out * output[out_idx + 1] +
                            fade_in * gain_ * right_gain_ * head_r;
    }
  }
}

// -- Sound --------------------------------------------------------------------

size_t Sound::FindStreamSlot() {
  for (size_t i = 0; i < stream_; ++i) {
    if (!streams_[i].IsManaged() && !streams_[i].IsPlaying()) {
      return i;
    }
  }
  return stream_;
}

ErrorOr<Sound::Source> Sound::AddSource(std::string_view name,
                                        Ownership ownership) {
  DbAssets::Sound sound;
  if (!sounds_.Lookup(name, &sound)) {
    LOG("Unknown sound ", name);
    return Error::Message("unknown sound name");
  }
  LockMutex l(mu_);
  size_t slot = FindStreamSlot();
  if (slot >= kMaxStreams) {
    LOG("Maximum number of streams exceeded");
    return Error::Message("maximum number of streams exceeded");
  }

  auto* sampler = qoa_alloc_.Alloc();
  if (!sampler->Init(&sound)) {
    return Error::Message("qoa init failed");
  }
  streams_[slot].InitFromStream(&sound, sampler);
  streams_[slot].SetOwnership(ownership);
  Source source = slot;
  if (slot == stream_) stream_++;
  return source;
}

ErrorOr<Sound::Source> Sound::AddEffect(std::string_view name,
                                        Ownership ownership) {
  LockMutex l(mu_);
  DbAssets::Sound sound;
  if (!sounds_.Lookup(name, &sound)) {
    LOG("Unknown sound ", name);
    return Error::Message("unknown sound name");
  }
  size_t slot = FindStreamSlot();
  if (slot >= kMaxStreams) {
    LOG("Maximum number of streams exceeded");
    return Error::Message("maximum number of streams exceeded");
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
      return Error::Message("qoa decode failed");
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
  Source source = slot;
  if (slot == stream_) stream_++;
  return source;
}

ErrorOr<void> Sound::SetSourceGain(Source source, float gain) {
  LockMutex l(mu_);
  if (source >= stream_) {
    LOG("Invalid source ", source);
    return Error::Message("invalid source");
  }
  streams_[source].Gain(gain);
  return {};
}

ErrorOr<void> Sound::StartChannel(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) {
    LOG("Invalid source ", source);
    return Error::Message("invalid source");
  }
  streams_[source].Start();
  return {};
}

ErrorOr<void> Sound::Stop(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) {
    LOG("Invalid source ", source);
    return Error::Message("invalid source");
  }
  streams_[source].Stop();
  return {};
}

bool Sound::Pause(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].Pause();
  return true;
}

bool Sound::Resume(Source source) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].Resume();
  return true;
}

bool Sound::IsPlaying(Source source) const {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  return streams_[source].IsPlaying();
}

bool Sound::SetLoop(Source source, bool loop) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].SetLoop(loop);
  return true;
}

bool Sound::SetPitch(Source source, float pitch) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].SetPitch(pitch);
  return true;
}

bool Sound::SetPan(Source source, float pan) {
  LockMutex l(mu_);
  if (source >= stream_) return false;
  streams_[source].SetPan(pan);
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

void Sound::GetStreamDebugInfo(StreamDebugInfo* out, size_t max_count) const {
  LockMutex l(mu_);
  size_t count = stream_ < max_count ? stream_ : max_count;
  for (size_t i = 0; i < count; ++i) {
    out[i].handle = streams_[i].handle_;
    out[i].playing = streams_[i].IsPlaying();
    out[i].loop = streams_[i].loop_;
    out[i].managed = streams_[i].IsManaged();
    out[i].gain = streams_[i].gain_;
    out[i].pitch = streams_[i].pitch_;
    out[i].pan = streams_[i].pan_;
  }
}

}  // namespace G
