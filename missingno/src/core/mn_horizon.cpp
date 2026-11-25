#include "mn_horizon.h"
#include <math.h>
#include <string.h>

namespace mn {

Horizon HZ;

static HorizonRead rd = nullptr;
static uint32_t fileSize = 0;
static struct {
  int32_t lat0, lon0, dlat, dlon;
  uint16_t ny, nx, rec;
} G;

static inline uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline int16_t i16(const uint8_t* p) { return (int16_t)u16(p); }
static inline int32_t i32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }

void horizonClose() {
  rd = nullptr; fileSize = 0;
  HZ.open = HZ.here = false; HZ.index = -1;
}

bool horizonOpen(HorizonRead r, uint32_t size) {
  horizonClose();
  uint8_t h[64];
  if (!r || size < 64 || !r(0, h, 64)) return false;
  if (memcmp(h, "MNHZ", 4) != 0 || h[4] != 1) return false;
  uint8_t bands = h[5];
  uint16_t naz = u16(h + 6);
  if (bands < 1 || bands > HZ_BANDS || naz != HZ_NAZ) return false;
  G.lat0 = i32(h + 8); G.lon0 = i32(h + 12); G.dlat = i32(h + 16); G.dlon = i32(h + 20);
  G.ny = u16(h + 24); G.nx = u16(h + 26); G.rec = u16(h + 28);
  if (G.ny == 0 || G.nx == 0 || G.rec != 12 + bands * HZ_NAZ) return false;
  if ((G.ny > 1 || G.nx > 1) && (G.dlat <= 0 || G.dlon <= 0)) return false;
  if (64u + (uint32_t)G.ny * G.nx * G.rec > size) return false;
  rd = r; fileSize = size;
  HZ.open = true; HZ.here = false; HZ.index = -1; HZ.gen++;
  HZ.bands = bands; HZ.sources = h[38];
  for (int i = 0; i < HZ_BANDS; i++) HZ.bandKm[i] = u16(h + 30 + i * 2);
  HZ.points = (uint32_t)G.ny * G.nx;
  HZ.stepKm = G.dlat * 1.0e-6f * 111.195f;
  return true;
}

void horizonAt(bool havePos, double lat, double lon) {
  if (!HZ.open) return;
  if (!havePos) { HZ.here = false; return; }
  // the nearest grid point, if the place is on (or just off) the grid
  double fr = G.dlat > 0 ? (lat * 1e6 - G.lat0) / G.dlat : 0;
  double fc = G.dlon > 0 ? (lon * 1e6 - G.lon0) / G.dlon : 0;
  int r = (int)floor(fr + 0.5), c = (int)floor(fc + 0.5);
  double slackR = G.ny > 1 ? 1.5 : 0.0, slackC = G.nx > 1 ? 1.5 : 0.0;
  // a single point stands for its town: allow about ten kilometres
  double dLat = lat - (G.lat0 + (double)(r < 0 ? 0 : (r >= G.ny ? G.ny - 1 : r)) * G.dlat) * 1e-6;
  double dLon = (lon - (G.lon0 + (double)(c < 0 ? 0 : (c >= G.nx ? G.nx - 1 : c)) * G.dlon) * 1e-6) * cos(lat * 0.01745329);
  float off = (float)(sqrt(dLat * dLat + dLon * dLon) * 111.195);
  bool onGrid = fr > -slackR - 0.5 && fr < G.ny - 0.5 + slackR && fc > -slackC - 0.5 && fc < G.nx - 0.5 + slackC;
  if (G.ny == 1 && G.nx == 1) onGrid = off < 10.0f;
  if (!onGrid) { HZ.here = false; return; }
  if (r < 0) r = 0; if (r >= G.ny) r = G.ny - 1;
  if (c < 0) c = 0; if (c >= G.nx) c = G.nx - 1;
  HZ.offKm = off;
  int32_t idx = r * G.nx + c;
  if (idx == HZ.index) { HZ.here = true; return; }
  uint32_t at = 64u + (uint32_t)idx * G.rec;
  uint8_t head[12];
  bool ok = rd(at, head, 12);
  for (int b = 0; ok && b < HZ.bands; b++) ok = rd(at + 12 + (uint32_t)b * HZ_NAZ, HZ.prof[b], HZ_NAZ);
  if (!ok || !(head[2] & 1)) { HZ.here = false; HZ.index = -1; return; }
  HZ.groundM = i16(head);
  HZ.peakAz = u16(head + 4) * 0.1f;
  HZ.peakAng = i16(head + 6) * 0.01f;
  HZ.peakKm = u16(head + 8) * 0.1f;
  HZ.index = idx; HZ.here = true;
}

float horizonHeight(int band, float az) {
  if (!HZ.here || band < 0 || band >= HZ.bands) return 0;
  float f = fmodf(az - 0.5f, 360.0f); if (f < 0) f += 360.0f;
  int i0 = (int)f; float k = f - i0;
  int i1 = i0 + 1 >= HZ_NAZ ? 0 : i0 + 1;
  if (i0 >= HZ_NAZ) i0 = HZ_NAZ - 1;
  return (HZ.prof[band][i0] * (1.0f - k) + HZ.prof[band][i1] * k) * (1.0f / 255.0f);
}

float horizonAngle(float az, int* band) {
  float best = 0; int bb = -1;
  for (int b = 0; b < HZ.bands; b++) {
    float h = horizonHeight(b, az);
    if (h > best) { best = h; bb = b; }
  }
  if (band) *band = bb;
  return best * best * 90.0f;
}

}  // namespace mn
