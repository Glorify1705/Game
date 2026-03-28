#include "qoa.h"

#include <cstring>

#include "logging.h"

namespace G {

namespace {

constexpr uint32_t kQoaMagic = 0x716f6166;  // "qoaf"
constexpr size_t kQoaFileHeaderSize = 8;
constexpr size_t kQoaFrameHeaderSize = 8;
constexpr size_t kQoaLmsStateSize = 16;  // per channel

// clang-format off
constexpr int kQoaQuantTab[17] = {
    7, 7, 7, 5, 5, 3, 3, 1,
    0,
    0, 2, 2, 4, 4, 6, 6, 6
};

constexpr int kQoaReciprocalTab[16] = {
    65536, 9363, 3121, 1457, 781, 475, 311, 216,
    156, 117, 90, 71, 57, 47, 39, 32
};

constexpr int kQoaDequantTab[16][8] = {
    {   1,    -1,    3,    -3,    5,    -5,     7,     -7},
    {   5,    -5,   18,   -18,   32,   -32,    49,    -49},
    {  16,   -16,   53,   -53,   95,   -95,   147,   -147},
    {  34,   -34,  113,  -113,  203,  -203,   315,   -315},
    {  63,   -63,  210,  -210,  378,  -378,   588,   -588},
    { 104,  -104,  345,  -345,  621,  -621,   966,   -966},
    { 158,  -158,  528,  -528,  950,  -950,  1477,  -1477},
    { 228,  -228,  760,  -760, 1368, -1368,  2128,  -2128},
    { 316,  -316, 1053, -1053, 1895, -1895,  2947,  -2947},
    { 422,  -422, 1405, -1405, 2529, -2529,  3934,  -3934},
    { 548,  -548, 1828, -1828, 3290, -3290,  5117,  -5117},
    { 696,  -696, 2320, -2320, 4176, -4176,  6496,  -6496},
    { 868,  -868, 2893, -2893, 5207, -5207,  8099,  -8099},
    {1064, -1064, 3548, -3548, 6386, -6386,  9933,  -9933},
    {1286, -1286, 4288, -4288, 7718, -7718, 12005, -12005},
    {1536, -1536, 5120, -5120, 9216, -9216, 14336, -14336},
};
// clang-format on

int QoaClampS16(int v) {
  if (v < -32768) return -32768;
  if (v > 32767) return 32767;
  return v;
}

int QoaClamp(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

int QoaDiv(int v, int scalefactor) {
  int reciprocal = kQoaReciprocalTab[scalefactor];
  int n = (v * reciprocal + (1 << 15)) >> 16;
  n += ((v > 0) - (v < 0)) - ((n > 0) - (n < 0));  // Round away from zero
  return n;
}

int QoaLmsPredict(const QoaLms* lms) {
  int prediction = 0;
  for (uint32_t i = 0; i < kQoaLmsLen; i++) {
    prediction += lms->weights[i] * lms->history[i];
  }
  return prediction >> 13;
}

void QoaLmsUpdate(QoaLms* lms, int sample, int residual) {
  int delta = residual >> 4;
  for (uint32_t i = 0; i < kQoaLmsLen; i++) {
    lms->weights[i] += lms->history[i] < 0 ? -delta : delta;
  }
  for (uint32_t i = 0; i < kQoaLmsLen - 1; i++) {
    lms->history[i] = lms->history[i + 1];
  }
  lms->history[kQoaLmsLen - 1] = sample;
}

uint64_t QoaReadU64(const uint8_t* bytes, size_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) {
    v = (v << 8) | bytes[(*p)++];
  }
  return v;
}

void QoaWriteU64(uint64_t v, uint8_t* bytes, size_t* p) {
  for (int i = 7; i >= 0; i--) {
    bytes[(*p)++] = (v >> (i * 8)) & 0xFF;
  }
}

size_t QoaFrameSize(uint32_t channels, uint32_t slices) {
  return kQoaFrameHeaderSize + kQoaLmsStateSize * channels +
         static_cast<size_t>(8) * slices * channels;
}

// Decodes a single frame starting at bytes + *p.
// Returns number of samples decoded per channel, 0 on error.
// Advances *p past the frame.
size_t QoaDecodeFrame(const uint8_t* bytes, size_t size, QoaDesc* desc,
                      QoaLms* lms, int16_t* sample_data, size_t* p) {
  if (size - *p < kQoaFrameHeaderSize) return 0;

  uint64_t frame_header = QoaReadU64(bytes, p);
  uint32_t channels = (frame_header >> 56) & 0xFF;
  uint32_t samplerate = (frame_header >> 32) & 0xFFFFFF;
  uint32_t samples = (frame_header >> 16) & 0xFFFF;
  uint32_t frame_size = frame_header & 0xFFFF;

  if (channels != desc->channels || samplerate != desc->samplerate ||
      frame_size > size - *p + kQoaFrameHeaderSize || channels == 0 ||
      channels > kQoaMaxChannels) {
    return 0;
  }

  size_t lms_data_size = kQoaLmsStateSize * channels;
  if (size - *p < lms_data_size) return 0;

  for (uint32_t c = 0; c < channels; c++) {
    uint64_t history = QoaReadU64(bytes, p);
    uint64_t weights = QoaReadU64(bytes, p);
    for (uint32_t i = 0; i < kQoaLmsLen; i++) {
      lms[c].history[i] = static_cast<int16_t>(history >> 48);
      history <<= 16;
      lms[c].weights[i] = static_cast<int16_t>(weights >> 48);
      weights <<= 16;
    }
  }

  for (uint32_t sample_index = 0; sample_index < samples;
       sample_index += kQoaSliceLen) {
    for (uint32_t c = 0; c < channels; c++) {
      if (size - *p < 8) return 0;
      uint64_t slice = QoaReadU64(bytes, p);

      int scalefactor = (slice >> 60) & 0xF;
      slice <<= 4;

      uint32_t slice_end = QoaClamp(sample_index + kQoaSliceLen, 0, samples);
      for (uint32_t si = sample_index; si < slice_end; si++) {
        int predicted = QoaLmsPredict(&lms[c]);
        int quantized = (slice >> 61) & 0x7;
        int dequantized = kQoaDequantTab[scalefactor][quantized];
        int reconstructed = QoaClampS16(predicted + dequantized);

        sample_data[si * channels + c] = static_cast<int16_t>(reconstructed);
        slice <<= 3;

        QoaLmsUpdate(&lms[c], reconstructed, dequantized);
      }
    }
  }

  return samples;
}

size_t QoaEncodeFrame(const int16_t* sample_data, QoaDesc* desc, QoaLms* lms,
                      uint32_t frame_len, uint8_t* bytes) {
  uint32_t channels = desc->channels;
  size_t p = 0;

  uint32_t slices = (frame_len + kQoaSliceLen - 1) / kQoaSliceLen;
  uint32_t frame_size = QoaFrameSize(channels, slices);
  int prev_scalefactor[kQoaMaxChannels] = {};

  QoaWriteU64((static_cast<uint64_t>(channels) << 56) |
                  (static_cast<uint64_t>(desc->samplerate) << 32) |
                  (static_cast<uint64_t>(frame_len) << 16) |
                  static_cast<uint64_t>(frame_size),
              bytes, &p);

  for (uint32_t c = 0; c < channels; c++) {
    uint64_t history = 0;
    uint64_t weights = 0;
    for (uint32_t i = 0; i < kQoaLmsLen; i++) {
      history = (history << 16) | (lms[c].history[i] & 0xFFFF);
      weights = (weights << 16) | (lms[c].weights[i] & 0xFFFF);
    }
    QoaWriteU64(history, bytes, &p);
    QoaWriteU64(weights, bytes, &p);
  }

  for (uint32_t sample_index = 0; sample_index < frame_len;
       sample_index += kQoaSliceLen) {
    for (uint32_t c = 0; c < channels; c++) {
      uint32_t slice_len = QoaClamp(kQoaSliceLen, 0, frame_len - sample_index);

      uint64_t best_rank = UINT64_MAX;
      uint64_t best_slice = 0;
      QoaLms best_lms = {};
      int best_scalefactor = 0;

      for (int sfi = 0; sfi < 16; sfi++) {
        int scalefactor = (sfi + prev_scalefactor[c]) % 16;

        QoaLms current_lms = lms[c];
        uint64_t slice = scalefactor;
        uint64_t current_rank = 0;

        for (uint32_t si = 0; si < slice_len; si++) {
          int sample = sample_data[(sample_index + si) * channels + c];
          int predicted = QoaLmsPredict(&current_lms);

          int residual = sample - predicted;
          int scaled = QoaDiv(residual, scalefactor);
          int clamped = QoaClamp(scaled, -8, 8);
          int quantized = kQoaQuantTab[clamped + 8];
          int dequantized = kQoaDequantTab[scalefactor][quantized];
          int reconstructed = QoaClampS16(predicted + dequantized);

          int64_t error = sample - reconstructed;
          uint64_t error_sq = error * error;

          int weights_penalty =
              ((current_lms.weights[0] * current_lms.weights[0] +
                current_lms.weights[1] * current_lms.weights[1] +
                current_lms.weights[2] * current_lms.weights[2] +
                current_lms.weights[3] * current_lms.weights[3]) >>
               18) -
              0x8ff;
          if (weights_penalty < 0) weights_penalty = 0;

          current_rank += error_sq + static_cast<uint64_t>(weights_penalty) *
                                         weights_penalty;
          if (current_rank > best_rank) break;

          QoaLmsUpdate(&current_lms, reconstructed, dequantized);
          slice = (slice << 3) | quantized;
        }

        if (current_rank < best_rank) {
          best_rank = current_rank;
          best_slice = slice;
          best_lms = current_lms;
          best_scalefactor = scalefactor;
        }
      }

      prev_scalefactor[c] = best_scalefactor;
      lms[c] = best_lms;

      best_slice <<= (kQoaSliceLen - slice_len) * 3;
      QoaWriteU64(best_slice, bytes, &p);
    }
  }

  return p;
}

}  // namespace

FixedArray<int16_t> QoaDecode(ByteSlice data, QoaDesc* desc,
                              Allocator* allocator) {
  if (data.size() < kQoaFileHeaderSize) {
    LOG("QOA: data too small for header");
    return FixedArray<int16_t>(0, allocator);
  }

  size_t p = 0;
  uint64_t file_header = QoaReadU64(data.data(), &p);
  uint32_t magic = (file_header >> 32) & 0xFFFFFFFF;
  uint32_t total_samples = file_header & 0xFFFFFFFF;

  if (magic != kQoaMagic) {
    LOG("QOA: invalid magic");
    return FixedArray<int16_t>(0, allocator);
  }

  // Read first frame header to get channels and samplerate.
  if (data.size() < p + kQoaFrameHeaderSize) {
    LOG("QOA: data too small for first frame header");
    return FixedArray<int16_t>(0, allocator);
  }

  uint64_t first_frame = QoaReadU64(data.data(), &p);
  desc->channels = (first_frame >> 56) & 0xFF;
  desc->samplerate = (first_frame >> 32) & 0xFFFFFF;
  desc->samples = total_samples;

  if (desc->channels == 0 || desc->channels > kQoaMaxChannels) {
    LOG("QOA: invalid channel count ", desc->channels);
    return FixedArray<int16_t>(0, allocator);
  }

  // Reset position to first frame for decoding.
  p = kQoaFileHeaderSize;

  size_t total_interleaved =
      static_cast<size_t>(total_samples) * desc->channels;
  FixedArray<int16_t> output(total_interleaved, allocator);
  output.Resize(total_interleaved);

  QoaLms lms[kQoaMaxChannels] = {};
  size_t sample_offset = 0;

  while (p < data.size() && sample_offset < total_samples) {
    size_t frame_samples =
        QoaDecodeFrame(data.data(), data.size(), desc, lms,
                       output.data() + sample_offset * desc->channels, &p);
    if (frame_samples == 0) break;
    sample_offset += frame_samples;
  }

  return output;
}

FixedArray<uint8_t> QoaEncode(Slice<int16_t> samples, const QoaDesc* desc,
                              Allocator* allocator) {
  if (desc->channels == 0 || desc->channels > kQoaMaxChannels ||
      desc->samples == 0 || desc->samplerate == 0) {
    LOG("QOA: invalid desc for encoding: channels=", desc->channels,
        " samples=", desc->samples, " samplerate=", desc->samplerate);
    return FixedArray<uint8_t>(0, allocator);
  }

  uint32_t num_frames = (desc->samples + kQoaFrameLen - 1) / kQoaFrameLen;
  size_t max_frame_slices = (kQoaFrameLen + kQoaSliceLen - 1) / kQoaSliceLen;
  size_t max_frame_size = QoaFrameSize(desc->channels, max_frame_slices);
  size_t max_total = kQoaFileHeaderSize + num_frames * max_frame_size;

  FixedArray<uint8_t> output(max_total, allocator);
  output.Resize(max_total);

  size_t p = 0;
  uint64_t file_header =
      (static_cast<uint64_t>(kQoaMagic) << 32) | desc->samples;
  QoaWriteU64(file_header, output.data(), &p);

  QoaLms lms[kQoaMaxChannels] = {};
  // Initialize LMS weights.
  for (uint32_t c = 0; c < desc->channels; c++) {
    lms[c].weights[0] = 0;
    lms[c].weights[1] = 0;
    lms[c].weights[2] = -(1 << 13);
    lms[c].weights[3] = 1 << 14;
  }

  QoaDesc frame_desc = *desc;
  uint32_t sample_offset = 0;

  while (sample_offset < desc->samples) {
    uint32_t frame_len =
        QoaClamp(kQoaFrameLen, 0, desc->samples - sample_offset);

    size_t frame_bytes = QoaEncodeFrame(
        samples.data() + static_cast<size_t>(sample_offset) * desc->channels,
        &frame_desc, lms, frame_len, output.data() + p);
    p += frame_bytes;
    sample_offset += frame_len;
  }

  // Shrink to actual size.
  output.Resize(p);
  return output;
}

bool QoaStreamDecoder::Init(ByteSlice data, QoaDesc* desc) {
  data_ = data;
  pos_ = 0;

  if (data_.size() < kQoaFileHeaderSize) {
    LOG("QOA stream: data too small");
    return false;
  }

  uint64_t file_header = QoaReadU64(data_.data(), &pos_);
  uint32_t magic = (file_header >> 32) & 0xFFFFFFFF;
  uint32_t total_samples = file_header & 0xFFFFFFFF;

  if (magic != kQoaMagic) {
    LOG("QOA stream: invalid magic");
    return false;
  }

  // Peek at first frame header for channels + samplerate.
  if (data_.size() < pos_ + kQoaFrameHeaderSize) {
    LOG("QOA stream: no frame data");
    return false;
  }

  size_t peek = pos_;
  uint64_t first_frame = QoaReadU64(data_.data(), &peek);
  desc_.channels = (first_frame >> 56) & 0xFF;
  desc_.samplerate = (first_frame >> 32) & 0xFFFFFF;
  desc_.samples = total_samples;

  if (desc_.channels == 0 || desc_.channels > kQoaMaxChannels) {
    LOG("QOA stream: invalid channel count ", desc_.channels);
    return false;
  }

  first_frame_pos_ = pos_;
  std::memset(lms_, 0, sizeof(lms_));

  *desc = desc_;
  return true;
}

size_t QoaStreamDecoder::DecodeFrame(int16_t* output, size_t max_samples) {
  if (pos_ >= data_.size()) return 0;

  size_t frame_samples =
      QoaDecodeFrame(data_.data(), data_.size(), &desc_, lms_, output, &pos_);
  return frame_samples;
}

void QoaStreamDecoder::Rewind() {
  pos_ = first_frame_pos_;
  std::memset(lms_, 0, sizeof(lms_));
}

}  // namespace G
