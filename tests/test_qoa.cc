#include <cmath>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"
#include "qoa.h"
#include "test_fixture.h"

namespace G {

class QoaTest : public BaseTest {};

// Helper: generate a sine wave of int16 samples.
static std::vector<int16_t> GenerateSine(uint32_t samples, uint32_t channels,
                                         double freq, uint32_t samplerate) {
  std::vector<int16_t> pcm(samples * channels);
  for (uint32_t i = 0; i < samples; ++i) {
    double t = static_cast<double>(i) / samplerate;
    int16_t val = static_cast<int16_t>(std::sin(2.0 * M_PI * freq * t) * 16000);
    for (uint32_t c = 0; c < channels; ++c) {
      pcm[i * channels + c] = val;
    }
  }
  return pcm;
}

TEST_F(QoaTest, RoundtripSilence) {
  constexpr uint32_t kSamples = 1024;
  std::vector<int16_t> pcm(kSamples, 0);
  QoaDesc desc{};
  desc.channels = 1;
  desc.samplerate = 44100;
  desc.samples = kSamples;

  auto encoded =
      QoaEncode(Slice<int16_t>(pcm.data(), pcm.size()), &desc, alloc);
  ASSERT_GT(encoded.size(), 0u);

  QoaDesc decoded_desc{};
  auto decoded = QoaDecode(MakeByteSlice(encoded.data(), encoded.size()),
                           &decoded_desc, alloc);
  ASSERT_EQ(decoded.size(), kSamples);
  EXPECT_EQ(decoded_desc.channels, 1u);
  EXPECT_EQ(decoded_desc.samplerate, 44100u);
  EXPECT_EQ(decoded_desc.samples, kSamples);

  // Silence should decode to near-zero.
  for (uint32_t i = 0; i < kSamples; ++i) {
    EXPECT_NEAR(decoded[i], 0, 100) << "sample " << i;
  }
}

TEST_F(QoaTest, RoundtripSineWave) {
  constexpr uint32_t kSamples = 44100;
  auto pcm = GenerateSine(kSamples, 1, 440.0, 44100);
  QoaDesc desc{};
  desc.channels = 1;
  desc.samplerate = 44100;
  desc.samples = kSamples;

  auto encoded =
      QoaEncode(Slice<int16_t>(pcm.data(), pcm.size()), &desc, alloc);
  ASSERT_GT(encoded.size(), 0u);

  QoaDesc decoded_desc{};
  auto decoded = QoaDecode(MakeByteSlice(encoded.data(), encoded.size()),
                           &decoded_desc, alloc);
  ASSERT_EQ(decoded.size(), kSamples);

  // QOA is lossy — check peak error is reasonable.
  int max_error = 0;
  for (uint32_t i = 0; i < kSamples; ++i) {
    int err = std::abs(static_cast<int>(decoded[i]) - static_cast<int>(pcm[i]));
    max_error = std::max(max_error, err);
  }
  EXPECT_LT(max_error, 1000) << "Peak error too large for 440Hz sine";
}

TEST_F(QoaTest, RoundtripStereo) {
  constexpr uint32_t kSamples = 2048;
  auto pcm = GenerateSine(kSamples, 2, 440.0, 44100);
  QoaDesc desc{};
  desc.channels = 2;
  desc.samplerate = 44100;
  desc.samples = kSamples;

  auto encoded =
      QoaEncode(Slice<int16_t>(pcm.data(), pcm.size()), &desc, alloc);
  ASSERT_GT(encoded.size(), 0u);

  QoaDesc decoded_desc{};
  auto decoded = QoaDecode(MakeByteSlice(encoded.data(), encoded.size()),
                           &decoded_desc, alloc);
  EXPECT_EQ(decoded_desc.channels, 2u);
  EXPECT_EQ(decoded.size(), kSamples * 2);
}

TEST_F(QoaTest, SingleSample) {
  int16_t pcm[] = {1000};
  QoaDesc desc{};
  desc.channels = 1;
  desc.samplerate = 44100;
  desc.samples = 1;

  auto encoded = QoaEncode(Slice<int16_t>(pcm, 1), &desc, alloc);
  ASSERT_GT(encoded.size(), 0u);

  QoaDesc decoded_desc{};
  auto decoded = QoaDecode(MakeByteSlice(encoded.data(), encoded.size()),
                           &decoded_desc, alloc);
  EXPECT_EQ(decoded_desc.samples, 1u);
  EXPECT_EQ(decoded.size(), 1u);
}

TEST_F(QoaTest, ExactFrameBoundary) {
  // kQoaFrameLen = 5120, exactly one frame.
  constexpr uint32_t kSamples = kQoaFrameLen;
  std::vector<int16_t> pcm(kSamples, 500);
  QoaDesc desc{};
  desc.channels = 1;
  desc.samplerate = 44100;
  desc.samples = kSamples;

  auto encoded =
      QoaEncode(Slice<int16_t>(pcm.data(), pcm.size()), &desc, alloc);
  ASSERT_GT(encoded.size(), 0u);

  QoaDesc decoded_desc{};
  auto decoded = QoaDecode(MakeByteSlice(encoded.data(), encoded.size()),
                           &decoded_desc, alloc);
  EXPECT_EQ(decoded_desc.samples, kSamples);
  EXPECT_EQ(decoded.size(), kSamples);
}

TEST_F(QoaTest, MultipleFrames) {
  // kQoaFrameLen + 1 forces a second frame.
  constexpr uint32_t kSamples = kQoaFrameLen + 1;
  std::vector<int16_t> pcm(kSamples, 300);
  QoaDesc desc{};
  desc.channels = 1;
  desc.samplerate = 44100;
  desc.samples = kSamples;

  auto encoded =
      QoaEncode(Slice<int16_t>(pcm.data(), pcm.size()), &desc, alloc);
  ASSERT_GT(encoded.size(), 0u);

  QoaDesc decoded_desc{};
  auto decoded = QoaDecode(MakeByteSlice(encoded.data(), encoded.size()),
                           &decoded_desc, alloc);
  EXPECT_EQ(decoded_desc.samples, kSamples);
  EXPECT_EQ(decoded.size(), kSamples);
}

TEST_F(QoaTest, StreamingMatchesBulk) {
  constexpr uint32_t kSamples = 8000;
  auto pcm = GenerateSine(kSamples, 1, 440.0, 44100);
  QoaDesc desc{};
  desc.channels = 1;
  desc.samplerate = 44100;
  desc.samples = kSamples;

  auto encoded =
      QoaEncode(Slice<int16_t>(pcm.data(), pcm.size()), &desc, alloc);
  ASSERT_GT(encoded.size(), 0u);

  // Bulk decode.
  QoaDesc bulk_desc{};
  auto bulk = QoaDecode(MakeByteSlice(encoded.data(), encoded.size()),
                        &bulk_desc, alloc);
  ASSERT_EQ(bulk.size(), kSamples);

  // Streaming decode.
  QoaStreamDecoder stream;
  QoaDesc stream_desc{};
  ASSERT_TRUE(
      stream.Init(MakeByteSlice(encoded.data(), encoded.size()), &stream_desc));
  EXPECT_EQ(stream_desc.samples, kSamples);

  std::vector<int16_t> streamed;
  int16_t frame_buf[kQoaFrameLen];
  while (true) {
    size_t n = stream.DecodeFrame(frame_buf, kQoaFrameLen);
    if (n == 0) break;
    streamed.insert(streamed.end(), frame_buf, frame_buf + n);
  }
  ASSERT_EQ(streamed.size(), kSamples);

  for (uint32_t i = 0; i < kSamples; ++i) {
    EXPECT_EQ(streamed[i], bulk[i]) << "mismatch at sample " << i;
  }
}

TEST_F(QoaTest, StreamingRewind) {
  constexpr uint32_t kSamples = 2048;
  std::vector<int16_t> pcm(kSamples, 1000);
  QoaDesc desc{};
  desc.channels = 1;
  desc.samplerate = 44100;
  desc.samples = kSamples;

  auto encoded =
      QoaEncode(Slice<int16_t>(pcm.data(), pcm.size()), &desc, alloc);
  ASSERT_GT(encoded.size(), 0u);

  QoaStreamDecoder stream;
  QoaDesc stream_desc{};
  ASSERT_TRUE(
      stream.Init(MakeByteSlice(encoded.data(), encoded.size()), &stream_desc));

  // First pass.
  std::vector<int16_t> pass1;
  int16_t frame_buf[kQoaFrameLen];
  while (true) {
    size_t n = stream.DecodeFrame(frame_buf, kQoaFrameLen);
    if (n == 0) break;
    pass1.insert(pass1.end(), frame_buf, frame_buf + n);
  }

  // Rewind and decode again.
  stream.Rewind();
  std::vector<int16_t> pass2;
  while (true) {
    size_t n = stream.DecodeFrame(frame_buf, kQoaFrameLen);
    if (n == 0) break;
    pass2.insert(pass2.end(), frame_buf, frame_buf + n);
  }

  ASSERT_EQ(pass1.size(), pass2.size());
  for (size_t i = 0; i < pass1.size(); ++i) {
    EXPECT_EQ(pass1[i], pass2[i]) << "mismatch at sample " << i;
  }
}

TEST_F(QoaTest, DecodeFramePastEnd) {
  constexpr uint32_t kSamples = 100;
  std::vector<int16_t> pcm(kSamples, 500);
  QoaDesc desc{};
  desc.channels = 1;
  desc.samplerate = 44100;
  desc.samples = kSamples;

  auto encoded =
      QoaEncode(Slice<int16_t>(pcm.data(), pcm.size()), &desc, alloc);
  ASSERT_GT(encoded.size(), 0u);

  QoaStreamDecoder stream;
  QoaDesc stream_desc{};
  ASSERT_TRUE(
      stream.Init(MakeByteSlice(encoded.data(), encoded.size()), &stream_desc));

  int16_t frame_buf[kQoaFrameLen];
  // Drain all frames.
  while (stream.DecodeFrame(frame_buf, kQoaFrameLen) > 0) {
  }
  // Past end should return 0.
  EXPECT_EQ(stream.DecodeFrame(frame_buf, kQoaFrameLen), 0u);
}

TEST_F(QoaTest, InvalidMagic) {
  uint8_t garbage[] = {0, 1, 2, 3, 4, 5, 6, 7};
  QoaDesc desc{};
  auto decoded = QoaDecode(MakeByteSlice(garbage, 8), &desc, alloc);
  EXPECT_EQ(decoded.size(), 0u);
}

TEST_F(QoaTest, TruncatedHeader) {
  uint8_t short_buf[] = {0x71, 0x6f, 0x61, 0x66};  // "qoaf" only
  QoaDesc desc{};
  auto decoded = QoaDecode(MakeByteSlice(short_buf, 4), &desc, alloc);
  EXPECT_EQ(decoded.size(), 0u);
}

TEST_F(QoaTest, StreamInitInvalidMagic) {
  uint8_t garbage[] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0};
  QoaStreamDecoder stream;
  QoaDesc desc{};
  EXPECT_FALSE(stream.Init(MakeByteSlice(garbage, 16), &desc));
}

TEST_F(QoaTest, EncodeZeroSamples) {
  QoaDesc desc{};
  desc.channels = 1;
  desc.samplerate = 44100;
  desc.samples = 0;
  auto encoded = QoaEncode(Slice<int16_t>(nullptr, 0), &desc, alloc);
  EXPECT_EQ(encoded.size(), 0u);
}

TEST_F(QoaTest, EncodeZeroChannels) {
  int16_t pcm[] = {100};
  QoaDesc desc{};
  desc.channels = 0;
  desc.samplerate = 44100;
  desc.samples = 1;
  auto encoded = QoaEncode(Slice<int16_t>(pcm, 1), &desc, alloc);
  EXPECT_EQ(encoded.size(), 0u);
}

}  // namespace G
