// missingno. -- reading the sky. A small NMEA parser (GGA + RMC, any talker).
#pragma once
#include <stdint.h>

namespace mn {

struct GpsFix {
  bool   fix = false;          // the last sentence that could say so, said so
  double lat = 0, lon = 0;
  float  altM = 0, speedKmh = 0;
  int    sats = 0;
  bool   haveTime = false;
  double utc = 0;              // unix seconds, from RMC
  uint32_t good = 0;           // sentences that passed their checksum
  uint32_t bad = 0;
};

struct Nmea {
  GpsFix g;
  char line[100];
  int  n = 0;
  // Feed one byte. Returns true when a sentence just updated `g`.
  bool feed(char c);
};

}  // namespace mn
