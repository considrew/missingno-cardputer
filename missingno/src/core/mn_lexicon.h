// missingno. -- the things it can say. Reading this file is a spoiler.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "mn_astro.h"

namespace mn {

enum Pool : uint8_t {
  L_GEN = 0, L_NIGHT, L_DAY, L_DUSK, L_DAWN, L_RAIN, L_FOG, L_SNOW, L_WIND, L_STORM, L_PRESSURE,
  L_FULLMOON, L_NEWMOON, L_HIGHTIDE, L_LOWTIDE, L_CROWDED, L_DEADAIR, L_NEWAIR, L_KNOWNAIR,
  L_STILL, L_WALK, L_CARRIED, L_FLIGHT, L_HIGH, L_LOUD, L_QUIET, L_WITCHING, L_COLD, L_HOT,
  L_KIN, L_KINLOST, L_LOWBATT, L_WAKE, L_SLEEP, L_FREEFALL, L_SHAKEN, L_SPUN, L_RESONANCE,
  L_NEWCELL, L_LINE, L_FAR, L_NEAR, L_CLOSE, L_RECEDE, L_NOSKY, L_KNOCK, L_FIRST, L_ANTENNA,
  L_COUNT
};

const char* lexPick(int pool, uint32_t rnd);
int lexCount(int pool);
const char* lexLine(int special);          // 1..5: the great circles
const char* lexVerdict(int verdict);       // 0..4, 5 = could not hear, 6 = muted

// What it says back when words are offered. Always writes something.
struct OracleCtx {
  const Sky* sky; bool haveTime, haveFix, haveWx;
  double lat, lon; float tempC; int wmo; float tide;
  int presence; uint32_t found, cells, turns;
};
void oracle(const char* line, const OracleCtx& c, uint32_t rnd, char* out, size_t n);

}  // namespace mn
