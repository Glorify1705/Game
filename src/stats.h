#pragma once
#ifndef _GAME_STATS_H
#define _GAME_STATS_H

#include <array>
#include <cmath>
#include <string>

namespace G {

class Stats {
 public:
  Stats();
  void AddSample(double sample);

  friend void AppendToString(const Stats& stats, std::string& str);

  double min() const { return min_; }
  double max() const { return max_; }
  double avg() const { return avg_; }
  double stdev2() const { return stdev2_; }
  double stdev() const { return std::sqrt(stdev2_); }
  double samples() const { return samples_; }

  double Percentile(double percentile) const;

 private:
  void AppendToString(char* buf, std::size_t len) const;

  double min_, max_, avg_, stdev2_, samples_, sum_, m2n_;
  std::array<size_t, 32> buckets_;
};

}  // namespace G

#endif  // _GAME_STATS_H
