#include "stats.h"

#include <cmath>
#include <cstdlib>
#include <limits>

#include "logging.h"

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
  stdev_ = m2n_ / samples_;
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

void Stats::AppendToString(std::string& str) const {
  char buf[128] = {0};
  if (samples_ > 1) {
    int written =
        snprintf(buf, sizeof(buf),
                 "min = %.2lf, max = %.2lf, avg = %.2lf, stdev = "
                 "%.2lf, p50 = %.2f, p90 = %.2f, p99 = %.2f FPS = %.2f",
                 min_, max_, avg_, std::sqrt(stdev_), Percentile(50),
                 Percentile(90), Percentile(99), 1000.0 / avg_);
    CHECK(written >= 0 && written < 128, "wrote ", written, " to buffer");
  }
  str.append(buf);
}
