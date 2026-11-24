// missingno. -- having-been. Everything it keeps between wakings lives in one
// plain block (`Life`) that the body writes to the card and reads back.
// It is porous on purpose: when it fills, it forgets half at random.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace mn {

constexpr int LIFE_CELLS  = 2048;
constexpr int LIFE_FOUND  = 128;
constexpr int LIFE_PRINTS = 64;
constexpr int LIFE_SCRAPS = 24;
constexpr int LIFE_SCRAPLEN = 40;
constexpr int LIFE_SPIRAL = 720;

struct EtherPrint { uint32_t h[8]; uint8_t n; uint8_t pad; uint16_t visits; };

struct Life {
  uint32_t magic;
  uint16_t version, size;
  // drift: 0.01-degree cells of the earth it has stood in
  uint32_t cells[LIFE_CELLS];
  uint32_t cellCount;
  // sealed places it has opened
  uint32_t found[LIFE_FOUND];
  uint32_t foundCount;
  // the ether: radio fingerprints of places
  EtherPrint prints[LIFE_PRINTS];
  uint32_t printCount, printNext;
  // scraps: things that were said to it
  char scraps[LIFE_SCRAPS][LIFE_SCRAPLEN];
  uint32_t scrapHead, scrapCount;
  // the spiral
  uint8_t spiral[LIFE_SPIRAL], spiralEv[LIFE_SPIRAL];
  uint32_t spiralLen, spiralTotal;
  float spiralInterval;
  // tallies
  uint32_t linesCrossed, steps, wakes, soundings, resonances, words;
  // habits
  float rootHz, volume;
  uint8_t muted, haveLast, pad0, pad1;
  int32_t lastSealDay;
  double lastLat, lastLon, lastUtc, bornUtc;
  uint32_t check;
};

Life& life();
void lifeReset();                                  // erase having-been
const uint8_t* lifeBytes(size_t& n);               // seals the checksum, returns the block
bool lifeLoad(const uint8_t* p, size_t n);         // false = not ours / damaged

struct DriftEvent {
  bool newCell = false;
  bool line = false;        // an integer degree of latitude or longitude
  int  special = 0;         // 1 equator, 2 a tropic, 3 a polar circle, 4 prime meridian, 5 antimeridian
};
DriftEvent driftVisit(double lat, double lon, bool mark);
bool driftKnown(int32_t ilat, int32_t ilon);       // indices as the chart computes them

// Offer a census of the air. novelty: 0 known ground .. 1 never heard.
void etherMeet(const uint32_t* hashes, int n, float& novelty, int& visits);

bool foundHas(uint32_t tag);
bool foundAdd(uint32_t tag);                       // true if it was new

void scrapKeep(const char* s);
const char* scrapAt(int i);                        // i = 0 newest; nullptr past the end

}  // namespace mn
