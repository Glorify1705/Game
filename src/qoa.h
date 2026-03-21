#pragma once
#ifndef _GAME_QOA_H
#define _GAME_QOA_H

#include <cstdint>

#include "allocators.h"
#include "array.h"

namespace G {

inline constexpr uint32_t kQoaSliceLen = 20;
inline constexpr uint32_t kQoaSlicesPerFrame = 256;
inline constexpr uint32_t kQoaFrameLen = kQoaSlicesPerFrame * kQoaSliceLen;
inline constexpr uint32_t kQoaLmsLen = 4;
inline constexpr uint32_t kQoaMaxChannels = 8;

struct QoaDesc {
  uint32_t channels;
  uint32_t samplerate;
  uint32_t samples;
};

struct QoaLms {
  int history[kQoaLmsLen];
  int weights[kQoaLmsLen];
};

// Decode entire QOA buffer to interleaved int16_t samples.
// Populates desc with format info. Returns empty array on error.
FixedArray<int16_t> QoaDecode(ByteSlice data, QoaDesc* desc,
                              Allocator* allocator);

// Encode interleaved int16_t samples to QOA.
// Returns encoded bytes. Returns empty array on error.
FixedArray<uint8_t> QoaEncode(Slice<int16_t> samples, const QoaDesc* desc,
                              Allocator* allocator);

// Streaming decoder: decodes one frame at a time from a QOA buffer.
class QoaStreamDecoder {
 public:
  bool Init(ByteSlice data, QoaDesc* desc);
  size_t DecodeFrame(int16_t* output, size_t max_samples);
  void Rewind();

  const QoaDesc& desc() const { return desc_; }

 private:
  ByteSlice data_;
  size_t pos_ = 0;
  size_t first_frame_pos_ = 0;
  QoaDesc desc_;
  QoaLms lms_[kQoaMaxChannels];
};

}  // namespace G

#endif  // _GAME_QOA_H
