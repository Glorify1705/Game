#pragma once
#ifndef _GAME_STATS_H
#define _GAME_STATS_H

#include <array>
#include <string>

namespace G {

class Stats {
 public:
  Stats();
  void AddSample(double sample);

  friend void AppendToString(const Stats& stats, std::string& str);

 private:
  static constexpr double kMax = 50.0;

  double Percentile(double percentile) const;
  void AppendToString(char* buf, size_t len) const;

  double min_, max_, avg_, stdev_, samples_, sum_, m2n_;
  std::array<size_t, 32> buckets_;
};

}  // namespace G

#endif  // _GAME_STATS_H