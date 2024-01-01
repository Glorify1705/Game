#include "stats.h"

#include <cstdlib>
#include <limits>

#include "logging.h"

namespace G {
namespace {

static constexpr double kMax = 50.0;

}  // namespace

Stats::Stats() {
  std::memset(this, 0, sizeof(Stats));
  min_ = std::numeric_limits<double>::max();
  max_ = std::numeric_limits<double>::min();
}

void Stats::AddSample(double sample) {
  samples_ += 1;
  sum_ += sample;
  min_ = std::min(min_, sample);
  max_ = std::max(max_, sample);
  const double prev_avg = avg_;
  avg_ = ((samples_ - 1) * prev_avg + sample) / samples_;
  // Welford's algorithm.
  m2n_ += (sample - prev_avg) * (sample - avg_);
  stdev2_ = m2n_ / samples_;
  const double index =
      std::min(buckets_.size() - 1.0, buckets_.size() * sample / kMax);
  buckets_[std::floor(index)]++;
}

double Stats::Percentile(double percentile) const {
  for (size_t i = 0, sum = 0; i < buckets_.size(); ++i) {
    sum += buckets_[i];
    if (sum >= samples_ * (percentile / 100)) {
      return (kMax * i) / buckets_.size();
    }
  }
  return kMax;
}

void AppendToString(const Stats& stats, std::string& str) {
  if (stats.samples() > 1) {
    StrAppend(&str, "min = ", stats.min(), " max = ", stats.max(),
              " avg = ", stats.avg(), " stdev = ", stats.stdev(),
              " p50 = ", stats.Percentile(50), " p90 = ", stats.Percentile(90),
              " p99 = ", stats.Percentile(99));
  }
}

}  // namespace G