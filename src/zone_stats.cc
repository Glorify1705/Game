#include "zone_stats.h"

namespace G {

void ZoneStats::Record(std::string_view name, double ms) {
  // Linear scan for existing zone.
  for (int i = 0; i < zone_count_; ++i) {
    if (zones_[i].name == name) {
      zones_[i].stats.AddSample(ms);
      return;
    }
  }
  // Create new zone if capacity allows.
  if (zone_count_ < kMaxZones) {
    zones_[zone_count_].name = name;
    zones_[zone_count_].stats = Stats();
    zones_[zone_count_].stats.AddSample(ms);
    ++zone_count_;
  }
}

void ZoneStats::Reset() {
  zone_count_ = 0;
}

ZoneStats* GetZoneStats() {
  static ZoneStats instance;
  return &instance;
}

}  // namespace G
