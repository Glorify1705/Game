#pragma once
#ifndef _GAME_STATS_H
#define _GAME_STATS_H

#include <array>
#include <string>

class Stats {
 public:
  Stats();
  void AddSample(double sample);

  void AppendToString(std::string& str) const;

 private:
  static constexpr double kMax = 50.0;
  double Percentile(double percentile) const;
  double min_, max_, avg_, stdev_, samples_, sum_, m2n_;
  std::array<size_t, 32> buckets_;
};

#endif  // _GAME_STATS_H