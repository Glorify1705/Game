#pragma once
#ifndef _GAME_ZONE_STATS_H
#define _GAME_ZONE_STATS_H

#include <string_view>

#include "clock.h"
#include "profiler.h"
#include "stats.h"

namespace G {

// Fixed-capacity table mapping zone name to running statistics. Used for
// live in-engine profiling. Zone names must be string literals or otherwise
// outlive the ZoneStats instance.
class ZoneStats {
 public:
  static constexpr int kMaxZones = 64;

  // A single profiling zone with its accumulated statistics.
  struct Zone {
    std::string_view name;
    Stats stats;
  };

  // Records a timing sample (in milliseconds) for the named zone.
  // Creates the zone if it doesn't exist yet.
  void Record(std::string_view name, double ms);

  // Clears all zone data.
  void Reset();

  int zone_count() const { return zone_count_; }
  const Zone& zone(int i) const { return zones_[i]; }

 private:
  Zone zones_[kMaxZones];
  int zone_count_ = 0;
};

// Global singleton for zone statistics.
ZoneStats* GetZoneStats();

// RAII guard that records elapsed time for a named zone on scope exit.
// Always feeds ZoneStats for the live debug UI. When GAME_WITH_PROFILING
// is enabled and recording is active, also feeds the Chrome Tracing profiler.
struct ZoneGuard {
  std::string_view name;
  Time start;
  ZoneGuard(std::string_view n) : name(n), start(Now()) {}
  ~ZoneGuard() {
    float ms = ElapsedMs(start);
    GetZoneStats()->Record(name, ms);
#ifdef GAME_WITH_PROFILING
    Profiler* p = GetProfiler();
    if (p->recording()) {
      p->AddEvent(name, "engine", NowInSeconds(),
                  static_cast<double>(ms) / 1000.0, /*tid=*/0);
    }
#endif
  }
};

#define ZONE(name) ::G::ZoneGuard INTERNAL_ID(zone_)(name)

}  // namespace G

#endif  // _GAME_ZONE_STATS_H
