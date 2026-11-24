// missingno. -- where the sun and moon are, worked out from nothing but a
// clock and a position. No network, no time zones: it keeps solar time.
#pragma once
#include <stdint.h>

namespace mn {

struct Sky {
  float sunEl = -9, sunAz = 270;     // degrees; azimuth from north, eastward
  float moonEl = 25, moonAz = 150;
  float moonPhase = 0.3f;            // 0 new .. 0.5 full .. 1 new
  float tide = 0;                    // -1 low .. +1 high (an ideal ocean)
  float solarHour = 0;               // local apparent solar time, 0..24
  int   month = 1, day = 1;          // the date by the sun, here
  int   year = 2000;
};

// utc: unix seconds. lat/lon: degrees, east positive.
void astro(double utc, double lat, double lon, Sky& out);

// The moon's phase needs no position at all.
float moonPhaseAt(double utc);

}  // namespace mn
