#include "clock.h"

namespace G {

Time Now() { return Clock::now(); }

double NowInSeconds() { return ToSeconds(Now().time_since_epoch()); }

}  // namespace G
