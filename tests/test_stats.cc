#include <cmath>

#include "gtest/gtest.h"
#include "stats.h"

namespace G {

TEST(StatsTest, EmptyStats) {
  Stats s;
  EXPECT_EQ(s.samples(), 0);
  EXPECT_EQ(s.avg(), 0);
  EXPECT_EQ(s.stdev(), 0);
}

TEST(StatsTest, SingleSample) {
  Stats s;
  s.AddSample(5.0);
  EXPECT_EQ(s.samples(), 1);
  EXPECT_EQ(s.min(), 5.0);
  EXPECT_EQ(s.max(), 5.0);
  EXPECT_EQ(s.avg(), 5.0);
  EXPECT_EQ(s.stdev(), 0.0);
}

TEST(StatsTest, TwoIdenticalSamples) {
  Stats s;
  s.AddSample(3.0);
  s.AddSample(3.0);
  EXPECT_EQ(s.min(), 3.0);
  EXPECT_EQ(s.max(), 3.0);
  EXPECT_EQ(s.avg(), 3.0);
  EXPECT_EQ(s.stdev(), 0.0);
}

TEST(StatsTest, KnownDataset) {
  // Dataset: {2, 4, 4, 4, 5, 5, 7, 9}
  // Mean = 5.0, population variance = 4.0, stdev = 2.0
  Stats s;
  for (double v : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) {
    s.AddSample(v);
  }
  EXPECT_EQ(s.samples(), 8);
  EXPECT_EQ(s.min(), 2.0);
  EXPECT_EQ(s.max(), 9.0);
  EXPECT_NEAR(s.avg(), 5.0, 1e-10);
  EXPECT_NEAR(s.stdev2(), 4.0, 1e-10);
  EXPECT_NEAR(s.stdev(), 2.0, 1e-10);
}

TEST(StatsTest, MinMaxTracking) {
  Stats s;
  s.AddSample(10.0);
  s.AddSample(3.0);
  s.AddSample(7.0);
  s.AddSample(1.0);
  s.AddSample(15.0);
  EXPECT_EQ(s.min(), 1.0);
  EXPECT_EQ(s.max(), 15.0);
}

TEST(StatsTest, NegativeSamples) {
  Stats s;
  s.AddSample(-5.0);
  s.AddSample(-3.0);
  s.AddSample(-1.0);
  EXPECT_EQ(s.min(), -5.0);
  EXPECT_EQ(s.max(), -1.0);
  EXPECT_NEAR(s.avg(), -3.0, 1e-10);
}

TEST(StatsTest, NegativeSamplesNoOOB) {
  // Negative samples should not crash (was an OOB bug).
  Stats s;
  s.AddSample(-10.0);
  s.AddSample(-0.5);
  s.AddSample(0.0);
  EXPECT_EQ(s.samples(), 3);
  EXPECT_EQ(s.min(), -10.0);
}

TEST(StatsTest, PercentileAllSameBucket) {
  Stats s;
  // All samples in the same bucket (around 1.0, kMax=16, bucket 2 of 32).
  for (int i = 0; i < 100; ++i) {
    s.AddSample(1.0);
  }
  double p50 = s.Percentile(50);
  double p99 = s.Percentile(99);
  // Both should return the same bucket lower bound.
  EXPECT_EQ(p50, p99);
}

TEST(StatsTest, PercentileSpread) {
  Stats s;
  // Add samples across the range [0, 16).
  for (int i = 0; i < 320; ++i) {
    s.AddSample(static_cast<double>(i) * 16.0 / 320.0);
  }
  double p50 = s.Percentile(50);
  double p90 = s.Percentile(90);
  // p50 should be around 8, p90 around 14.
  EXPECT_NEAR(p50, 8.0, 1.0);
  EXPECT_NEAR(p90, 14.0, 1.5);
}

TEST(StatsTest, LargeSampleAboveMax) {
  // Samples >= kMax (16.0) should clamp to last bucket, not crash.
  Stats s;
  s.AddSample(100.0);
  s.AddSample(1000.0);
  EXPECT_EQ(s.samples(), 2);
  EXPECT_EQ(s.max(), 1000.0);
}

TEST(StatsTest, AppendToStringEmpty) {
  Stats s;
  LogBuffer buf;
  AppendToString(s, buf);
  EXPECT_EQ(buf.view(), "");
}

TEST(StatsTest, AppendToStringSingleSample) {
  Stats s;
  s.AddSample(5.0);
  LogBuffer buf;
  AppendToString(s, buf);
  // No output for single sample (guard is > 1).
  EXPECT_EQ(buf.view(), "");
}

TEST(StatsTest, AppendToStringMultipleSamples) {
  Stats s;
  s.AddSample(2.0);
  s.AddSample(4.0);
  LogBuffer buf;
  AppendToString(s, buf);
  auto out = buf.view();
  EXPECT_NE(out.find("min"), std::string_view::npos);
  EXPECT_NE(out.find("max"), std::string_view::npos);
  EXPECT_NE(out.find("avg"), std::string_view::npos);
  EXPECT_NE(out.find("stdev"), std::string_view::npos);
}

}  // namespace G
