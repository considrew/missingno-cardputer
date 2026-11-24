#include "mn_drift.h"
#include "mn_gfx.h"       // hash32
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace mn {

static Life L;
static const uint32_t MAGIC = 0x6F6E6D2EU;   // ".mno"
static const uint16_t VERSION = 3;

Life& life() { return L; }

void lifeReset() {
  memset(&L, 0, sizeof(L));
  L.magic = MAGIC; L.version = VERSION; L.size = (uint16_t)sizeof(Life);
  L.spiralInterval = 20.0f;
  L.rootHz = 110.0f; L.volume = 0.6f;
  L.lastSealDay = -1;
}

static uint32_t checksum(const Life& of = L) {
  const uint8_t* p = (const uint8_t*)&of;
  size_t n = offsetof(Life, check);
  uint32_t h = 0x811C9DC5U;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619U; }
  return h;
}

const uint8_t* lifeBytes(size_t& n) {
  L.magic = MAGIC; L.version = VERSION; L.size = (uint16_t)sizeof(Life);
  L.check = checksum();
  n = sizeof(Life);
  return (const uint8_t*)&L;
}

bool lifeLoad(const uint8_t* p, size_t n) {
  if (n != sizeof(Life)) return false;
  // Checked in a heap copy: a Life is 13.5 KB and the device's loop stack is 8 KB.
  Life* in = (Life*)malloc(sizeof(Life));
  if (!in) return false;
  memcpy(in, p, sizeof(Life));
  const Life& c = *in;
  bool ok = c.magic == MAGIC && c.version == VERSION && c.size == sizeof(Life) && c.check == checksum(c)
            && c.cellCount <= LIFE_CELLS && c.foundCount <= LIFE_FOUND && c.printCount <= LIFE_PRINTS
            && c.printNext < LIFE_PRINTS + 1 && c.scrapCount <= LIFE_SCRAPS && c.scrapHead < LIFE_SCRAPS
            && c.spiralLen <= LIFE_SPIRAL && c.spiralInterval >= 1.0f && c.spiralInterval < 1.0e7f;
  if (ok) memcpy(&L, in, sizeof(Life));
  free(in);
  if (!ok) return false;
  for (int i = 0; i < LIFE_SCRAPS; i++) L.scraps[i][LIFE_SCRAPLEN - 1] = 0;
  if (!(L.rootHz > 60 && L.rootHz < 200)) L.rootHz = 110.0f;
  if (!(L.volume >= 0 && L.volume <= 1)) L.volume = 0.5f;
  return true;
}

// ------------------------------------------------------------------- cells
static inline uint32_t cellKey(int32_t ilat, int32_t ilon) { return (uint32_t)ilat * 36000U + (uint32_t)ilon + 1U; }

static bool cellFind(uint32_t key, int& slot) {
  int i = (int)(hash32(key) & (LIFE_CELLS - 1));
  for (int probe = 0; probe < LIFE_CELLS; probe++) {
    uint32_t v = L.cells[i];
    if (v == key) { slot = i; return true; }
    if (v == 0) { slot = i; return false; }
    i = (i + 1) & (LIFE_CELLS - 1);
  }
  slot = -1; return false;
}

static void forgetHalf() {
  // Porous: what leaks is not chosen.
  uint32_t* tmp = (uint32_t*)malloc(sizeof(L.cells));
  if (tmp) memcpy(tmp, L.cells, sizeof(L.cells));
  memset(L.cells, 0, sizeof(L.cells));
  L.cellCount = 0;
  if (!tmp) return;                                  // no room to choose: it all goes
  for (int i = 0; i < LIFE_CELLS; i++) {
    uint32_t k = tmp[i];
    if (!k || (hash32(k ^ L.spiralTotal) & 1)) continue;
    int slot; if (!cellFind(k, slot) && slot >= 0) { L.cells[slot] = k; L.cellCount++; }
  }
  free(tmp);
}

bool driftKnown(int32_t ilat, int32_t ilon) {
  if (ilat < 0 || ilat > 18000) return false;
  ilon %= 36000; if (ilon < 0) ilon += 36000;
  int slot; return cellFind(cellKey(ilat, ilon), slot);
}

static bool crossed(double a, double b, double at) { return (a - at) * (b - at) < 0; }

DriftEvent driftVisit(double lat, double lon, bool mark) {
  DriftEvent ev;
  if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return ev;
  if (L.haveLast) {
    double dla = lat - L.lastLat, dlo = lon - L.lastLon;
    bool anti = fabs(dlo) > 300.0;                       // stepped over the back of the world
    if (fabs(dla) < 1.0 && (fabs(dlo) < 1.0 || anti)) {
      if (floor(lat) != floor(L.lastLat) || (!anti && floor(lon) != floor(L.lastLon))) ev.line = true;
      if (crossed(L.lastLat, lat, 0.0)) ev.special = 1;
      else if (crossed(L.lastLat, lat, 23.4366) || crossed(L.lastLat, lat, -23.4366)) ev.special = 2;
      else if (crossed(L.lastLat, lat, 66.5634) || crossed(L.lastLat, lat, -66.5634)) ev.special = 3;
      else if (!anti && crossed(L.lastLon, lon, 0.0)) ev.special = 4;
      else if (anti) ev.special = 5;
      if (ev.special) ev.line = true;
      if (ev.line) L.linesCrossed++;
    }
  }
  L.lastLat = lat; L.lastLon = lon; L.haveLast = 1;
  if (mark) {
    int32_t ilat = (int32_t)floor((lat + 90.0) * 100.0), ilon = (int32_t)floor((lon + 180.0) * 100.0);
    if (ilon >= 36000) ilon -= 36000;
    int slot;
    if (!cellFind(cellKey(ilat, ilon), slot) && slot >= 0) {
      L.cells[slot] = cellKey(ilat, ilon); L.cellCount++; ev.newCell = true;
      if (L.cellCount > (LIFE_CELLS * 3) / 4) forgetHalf();
    }
  }
  return ev;
}

// ------------------------------------------------------------------- ether
void etherMeet(const uint32_t* hashes, int n, float& novelty, int& visits) {
  novelty = 0; visits = 0;
  if (n <= 0) return;
  if (n > 8) n = 8;
  int best = -1, bestCommon = 0;
  for (uint32_t i = 0; i < L.printCount; i++) {
    const EtherPrint& p = L.prints[i];
    int common = 0;
    for (int a = 0; a < n; a++) for (int b = 0; b < p.n; b++) if (hashes[a] == p.h[b]) { common++; break; }
    int smaller = n < p.n ? n : p.n;
    int need = smaller / 3; if (need < 2) need = 2; if (need > smaller) need = smaller;
    if (common >= need && common > bestCommon) { best = (int)i; bestCommon = common; }
  }
  if (best >= 0) {
    EtherPrint& p = L.prints[best];
    if (p.visits < 65535) p.visits++;
    visits = p.visits;
    novelty = 1.0f - (float)bestCommon / (float)n;
    // let the print follow the place as its voices change
    for (int a = 0; a < n && p.n < 8; a++) {
      bool have = false;
      for (int b = 0; b < p.n; b++) if (p.h[b] == hashes[a]) have = true;
      if (!have) p.h[p.n++] = hashes[a];
    }
    return;
  }
  novelty = 1.0f;
  uint32_t slot;
  if (L.printCount < LIFE_PRINTS) slot = L.printCount++;
  else { slot = L.printNext % LIFE_PRINTS; L.printNext = (slot + 1) % LIFE_PRINTS; }
  EtherPrint& p = L.prints[slot];
  memset(&p, 0, sizeof(p));
  for (int a = 0; a < n; a++) p.h[a] = hashes[a];
  p.n = (uint8_t)n; p.visits = 1;
  visits = 1;
}

// ------------------------------------------------------------------- found
bool foundHas(uint32_t tag) {
  uint32_t n = L.foundCount < LIFE_FOUND ? L.foundCount : LIFE_FOUND;
  for (uint32_t i = 0; i < n; i++) if (L.found[i] == tag) return true;
  return false;
}
bool foundAdd(uint32_t tag) {
  if (foundHas(tag)) return false;
  if (L.foundCount < LIFE_FOUND) L.found[L.foundCount++] = tag;
  return true;
}

// ------------------------------------------------------------------ scraps
void scrapKeep(const char* s) {
  if (!s || !s[0]) return;
  char* d = L.scraps[L.scrapHead];
  strncpy(d, s, LIFE_SCRAPLEN - 1); d[LIFE_SCRAPLEN - 1] = 0;
  L.scrapHead = (L.scrapHead + 1) % LIFE_SCRAPS;
  if (L.scrapCount < LIFE_SCRAPS) L.scrapCount++;
}
const char* scrapAt(int i) {
  if (i < 0 || (uint32_t)i >= L.scrapCount) return nullptr;
  int idx = ((int)L.scrapHead - 1 - i) % LIFE_SCRAPS; if (idx < 0) idx += LIFE_SCRAPS;
  return L.scraps[idx];
}

}  // namespace mn
