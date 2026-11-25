#include "mn_pro.h"
#include "mn_state.h"
#include "mn_gfx.h"
#include "mn_draw.h"
#include "mn_mind.h"
#include "mn_astro.h"
#include "mn_drift.h"
#include "mn_horizon.h"
#include "mn_theremin.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace mn {

// An instrument panel: graphite, one cool accent, warm only for warnings.
static constexpr uint16_t P_BG = rgb(8, 10, 14), P_PANEL = rgb(17, 21, 27), P_EDGE = rgb(40, 46, 56),
  P_DIM = rgb(104, 112, 126), P_TEXT = rgb(226, 230, 236), P_ACC = rgb(92, 196, 255),
  P_OK = rgb(110, 222, 150), P_WARN = rgb(255, 176, 64), P_BAD = rgb(255, 92, 80);

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int iround(float v) { return (int)floorf(v + 0.5f); }
static inline float maxf(float a, float b) { return a > b ? a : b; }

// ================================================================ histories
struct Ring {
  static constexpr int N = 96;
  uint8_t v[N]; int n = 0, head = 0;
  void push(int x) { v[head] = (uint8_t)(x < 0 ? 0 : (x > 255 ? 255 : x)); head = (head + 1) % N; if (n < N) n++; }
  int at(int i) const { return v[(head - n + i + 2 * N) % N]; }            // 0 = oldest
};

static struct {
  Ring level, ap, acc, wires;
  float levelAcc, accAcc, wiresAcc;
  float peakDb, peakG, spinMax; int falls; bool wasFall;
  float knockT[16]; int knockN; uint32_t knocks;
  float fps;
  float stepT[7]; uint32_t stepV[7]; int stepI, stepN; float stepAcc, cadence;
  float sunAcc; bool sunOk; double rise, set, ridgeRise, ridgeSet;
  float tidePrev, tideRate, tideAcc;
  bool init;
} P;

static void resetPeaks() { P.peakDb = -120; P.peakG = 1; P.spinMax = 0; P.falls = 0; }

static bool where(double& lat, double& lon) {
  if (S.in.haveFix) { lat = S.in.lat; lon = S.in.lon; return true; }
  Life& L = life();
  if (L.haveLast) { lat = L.lastLat; lon = L.lastLon; return true; }
  return false;
}

// The next sunrise and sunset from here: over the level line (upper limb, with
// refraction), and, when the land is known, the moment it clears or meets the ridge.
static void sunTimes() {
  P.sunOk = false; P.rise = P.set = P.ridgeRise = P.ridgeSet = 0;
  double lat, lon;
  if (!S.in.haveTime || !where(lat, lon)) return;
  const double step = 900.0, now = S.in.utc;
  Sky k; double t = now - step;
  astro(t, lat, lon, k);
  float pe = k.sunEl + 0.833f, pr = k.sunEl - horizonAngle(k.sunAz);
  for (int i = 0; i < 136; i++) {
    t += step;
    astro(t, lat, lon, k);
    float e = k.sunEl + 0.833f, r = k.sunEl - horizonAngle(k.sunAz);
    double te = t - step * e / (e - pe + 1e-9f), tr = t - step * r / (r - pr + 1e-9f);
    if (pe < 0 && e >= 0 && !P.rise && te > now - 60) P.rise = te;
    if (pe >= 0 && e < 0 && !P.set && te > now - 60) P.set = te;
    if (HZ.here) {
      if (pr < 0 && r >= 0 && !P.ridgeRise && tr > now - 60) P.ridgeRise = tr;
      if (pr >= 0 && r < 0 && !P.ridgeSet && tr > now - 60) P.ridgeSet = tr;
    }
    pe = e; pr = r;
  }
  P.sunOk = true;
}

void proTick(float dt) {
  const Senses& in = S.in;
  if (!P.init) { memset(&P, 0, sizeof(P)); resetPeaks(); P.fps = 30; P.sunAcc = 1e9f; P.init = true; }
  if (dt > 0.0005f) P.fps += (1.0f / dt - P.fps) * 0.05f;
  // sound
  P.peakDb = maxf(P.peakDb, in.levelDb);
  P.levelAcc += dt;
  if (P.levelAcc >= 0.5f) { P.levelAcc = 0; P.level.push(iround((in.levelDb + 100.0f) * 2.55f)); }
  if (in.onset) {
    P.knocks++;
    if (P.knockN == 16) { memmove(P.knockT, P.knockT + 1, sizeof(float) * 15); P.knockN = 15; }
    P.knockT[P.knockN++] = S.t;
  }
  // the hand
  P.peakG = maxf(P.peakG, in.accG);
  P.spinMax = maxf(P.spinMax, fabsf(in.spin));
  if (in.freefall && !P.wasFall) P.falls++;
  P.wasFall = in.freefall;
  P.accAcc += dt;
  if (P.accAcc >= 0.1f) { P.accAcc = 0; P.acc.push(iround(in.accG * 64.0f)); }
  P.wiresAcc += dt;
  if (P.wiresAcc >= 0.25f) { P.wiresAcc = 0; if (in.antenna >= 0) P.wires.push(iround(in.antenna * 255.0f)); }
  P.stepAcc += dt;
  if (P.stepAcc >= 10.0f || P.stepN == 0) {
    P.stepAcc = 0;
    P.stepT[P.stepI] = S.t; P.stepV[P.stepI] = in.steps;
    P.stepI = (P.stepI + 1) % 7; if (P.stepN < 7) P.stepN++;
    int old = (P.stepI - P.stepN + 7) % 7, last = (P.stepI + 6) % 7;
    float span = P.stepT[last] - P.stepT[old];
    P.cadence = span > 5.0f ? (float)(P.stepV[last] - P.stepV[old]) / span * 60.0f : 0.0f;
  }
  // the air
  if (in.etherFresh && in.apCount >= 0) P.ap.push(in.apCount);
  // the sky
  P.tideAcc += dt;
  if (P.tideAcc >= 30.0f) { P.tideRate = (S.scene.tide - P.tidePrev) / P.tideAcc; P.tidePrev = S.scene.tide; P.tideAcc = 0; }
  if (S.pro) {
    S.proAge += dt;
    P.sunAcc += dt;
    if (P.sunAcc >= 60.0f) { P.sunAcc = 0; sunTimes(); }
  } else P.sunAcc = 1e9f;                                      // recompute on the way in
}

// ============================================================ what a tool says
struct Card {
  char big[16]; char unit[8]; char sub[44];
  char k[6][10]; char v[6][24]; int rows;
  uint16_t tone;
  float bar;                                   // 0..1 under the value; < 0 none
  const Ring* spark; int sparkMax;
  void (*gfx)(int x, int y, int w, int h);
  bool live;
};
static void clearCard(Card& c) { memset(&c, 0, sizeof(c)); c.tone = P_TEXT; c.bar = -1; c.live = true; }
static void row(Card& c, const char* k, const char* fmt, ...) {
  if (c.rows >= 6) return;
  strncpy(c.k[c.rows], k, 9); c.k[c.rows][9] = 0;
  va_list ap; va_start(ap, fmt); vsnprintf(c.v[c.rows], sizeof(c.v[0]), fmt, ap); va_end(ap);
  c.rows++;
}
static void none(Card& c, const char* why) { strcpy(c.big, "--"); c.live = false; strncpy(c.sub, why, sizeof(c.sub) - 1); }

static const char* const DIR16[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
static const char* dir16(float az) { return DIR16[(int)(fmodf(az + 371.25f, 360.0f) / 22.5f) & 15]; }

static void hhmm(char* b, size_t n, double utc, bool secs) {
  double t = utc + (S.in.haveTz ? S.in.utcOffset : 0);
  long s = (long)floor(fmod(t, 86400.0)); if (s < 0) s += 86400;
  if (secs) snprintf(b, n, "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
  else snprintf(b, n, "%02ld:%02ld", s / 3600, (s / 60) % 60);
}
static void ymd(double utc, int& y, int& m, int& d) {
  long z = (long)floor((utc + (S.in.haveTz ? S.in.utcOffset : 0)) / 86400.0) + 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yoe + era * 400 + (m <= 2));
}
static const char* zone() { return S.in.haveTz ? "LOCAL" : "UTC"; }

// ------------------------------------------------------------- the pictures
static void spark(int x, int y, int w, int h, const Ring& r, int maxv, uint16_t c) {
  frame(x, y, w, h, P_EDGE);
  if (r.n < 2) return;
  int n = r.n < w - 2 ? r.n : w - 2;
  if (maxv <= 0) { maxv = 1; for (int i = 0; i < r.n; i++) if (r.at(i) > maxv) maxv = r.at(i); }
  int px_ = 0, py_ = 0;
  for (int i = 0; i < n; i++) {
    int v = r.at(r.n - n + i);
    int X = x + w - 1 - n + i, Y = y + h - 2 - (int)((long)v * (h - 3) / maxv);
    if (Y < y + 1) Y = y + 1;
    if (i) line(px_, py_, X, Y, c);
    px_ = X; py_ = Y;
  }
}

static void gSpectrum(int x, int y, int w, int h) {
  int bw = (w - 9) / 8;
  int top = h / 2 - 2;
  for (int b = 0; b < 8; b++) {
    int bh = (int)(clampf(S.in.bands[b], 0, 1) * top);
    rect(x + 1 + b * (bw + 1), y + top - bh, bw, bh + 1, P_ACC);
    hline(x + 1 + b * (bw + 1), y + top + 1, bw, P_EDGE);
  }
  text(x, y + top + 3, "LOW", P_DIM); textRight(x + w - 1, y + top + 3, "HIGH", P_DIM);
  spark(x, y + top + 12, w, h - top - 12, P.level, 255, P_ACC);
}

static float tunerCents(int* midi) {
  float m = 69.0f + 12.0f * log2f(S.in.pitchHz / 440.0f);
  int mi = iround(m); if (midi) *midi = mi;
  return (m - mi) * 100.0f;
}
static void gTuner(int x, int y, int w, int h) {
  int cx = x + w / 2, cy = y + h - 6, r = (w / 2 - 4) < (h - 12) ? (w / 2 - 4) : (h - 12);
  for (int c = -50; c <= 50; c += 10) {
    float a = (c / 50.0f) * 1.0f - 1.5708f;
    int len = c == 0 ? 8 : (c % 50 == 0 ? 6 : 3);
    line(cx + iround(cosf(a) * r), cy + iround(sinf(a) * r), cx + iround(cosf(a) * (r - len)), cy + iround(sinf(a) * (r - len)), c == 0 ? P_OK : P_EDGE);
  }
  text(cx - r - 2, cy - 4, "-", P_DIM); text(cx + r - 2, cy - 4, "+", P_DIM);
  if (S.in.pitchHz <= 20) { textCenter(cx, cy - r / 2, "NO TONE", P_DIM); return; }
  float cents = clampf(tunerCents(nullptr), -50, 50);
  float a = (cents / 50.0f) - 1.5708f;
  uint16_t c = fabsf(cents) < 5 ? P_OK : (fabsf(cents) < 20 ? P_ACC : P_WARN);
  line(cx, cy, cx + iround(cosf(a) * (r - 2)), cy + iround(sinf(a) * (r - 2)), c);
  disc(cx, cy, 2, c);
}

static float levX, levY; static bool levFlat;
static void computeLevel() {
  const float* g = S.in.grav;
  float gl = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]); if (gl < 0.2f) gl = 0.2f;
  levFlat = fabsf(g[1]) / gl > 0.8f;
  if (levFlat) {
    levX = atan2f(g[0], fabsf(g[1])) * 57.29578f;
    levY = atan2f(g[2], fabsf(g[1])) * 57.29578f;
  } else {
    levX = S.in.tilt;
    levY = atan2f(g[1], fabsf(g[2]) < 0.05f ? 0.05f : fabsf(g[2])) * 57.29578f;
  }
}
static void gLevel(int x, int y, int w, int h) {
  int cx = x + w / 2, cy = y + h / 2, r = (w < h ? w : h) / 2 - 2;
  ring(cx, cy, r, P_EDGE); ring(cx, cy, r / 3, P_DIM);
  hline(cx - r, cy, 2 * r + 1, P_EDGE); vline(cx, cy - r, 2 * r + 1, P_EDGE);
  float bx = clampf(-levX / 12.0f, -1, 1), by = clampf(levY / 12.0f, -1, 1);
  bool true_ = fabsf(levX) < 0.5f && fabsf(levY) < 0.5f;
  int X = cx + iround(bx * (r - 5)), Y = cy + iround(by * (r - 5));
  disc(X, Y, 4, true_ ? P_OK : P_ACC);
  ring(X, Y, 5, P_BG);
}

static void gEcho(int x, int y, int w, int h) {
  frame(x, y, w, h, P_EDGE);
  for (int i = 1; i < 4; i++) hline(x + 1, y + i * h / 4, w - 2, P_PANEL);
  const Sounding& so = S.sounding;
  if (!so.valid) { textCenter(x + w / 2, y + h / 2 - 4, "ENTER STRIKES", P_DIM); return; }
  int px_ = 0, py_ = 0;
  for (int i = 0; i < 60; i++) {
    int X = x + 1 + i * (w - 3) / 59, Y = y + h - 2 - so.env[i] * (h - 4) / 255;
    if (i) line(px_, py_, X, Y, P_ACC);
    px_ = X; py_ = Y;
  }
  int tx = x + 1 + (int)(clampf(so.tailMs / 480.0f, 0, 1) * (w - 3));
  for (int Y = y + 1; Y < y + h - 1; Y += 2) px(tx, Y, P_WARN);
  text(x + 2, y + h - 9, "0", P_DIM); textRight(x + w - 2, y + h - 9, "480MS", P_DIM);
}

static void gKnocks(int x, int y, int w, int h) {
  frame(x, y, w, h, P_EDGE);
  const float span = 8.0f;
  for (int s = 1; s < 8; s++) vline(x + w - 1 - s * (w - 2) / 8, y + 1, h - 2, P_PANEL);
  for (int i = 0; i < P.knockN; i++) {
    float age = S.t - P.knockT[i];
    if (age > span) continue;
    int X = x + w - 2 - (int)(age / span * (w - 3));
    vline(X, y + 3, h - 6, age < 0.3f ? P_TEXT : P_ACC);
  }
  textRight(x + w - 2, y + h - 9, "NOW", P_DIM); text(x + 2, y + h - 9, "-8S", P_DIM);
}

static void gWind(int x, int y, int w, int h) {
  int cx = x + w / 2, cy = y + h / 2, r = (w < h ? w : h) / 2 - 6;
  ring(cx, cy, r, P_EDGE);
  text(cx - 2, cy - r - 9 < y ? y : cy - r - 9, "N", P_DIM);
  for (int i = 0; i < 8; i++) {
    float a = i * 0.785398f;
    px(cx + iround(sinf(a) * (r - 2)), cy - iround(cosf(a) * (r - 2)), P_DIM);
  }
  if (!S.in.haveWx) return;
  float a = (S.in.windDir + 180.0f) * 0.0174533f;          // it blows toward here
  int tx = cx + iround(sinf(a) * (r - 3)), ty = cy - iround(cosf(a) * (r - 3));
  int fx = cx - iround(sinf(a) * (r - 3)), fy = cy + iround(cosf(a) * (r - 3));
  line(fx, fy, tx, ty, P_ACC);
  float l = a + 2.6f, rr = a - 2.6f;
  line(tx, ty, tx + iround(sinf(l) * 6), ty - iround(cosf(l) * 6), P_ACC);
  line(tx, ty, tx + iround(sinf(rr) * 6), ty - iround(cosf(rr) * 6), P_ACC);
}

// The whole horizon, unrolled: north at both ends. The land (if it is known), the
// sun and the moon, and brackets round the part the shore looks at.
static void gHorizon(int x, int y, int w, int h) {
  frame(x, y, w, h, P_EDGE);
  int base = y + h - 10, K = (h - 14) * 2;                  // 90 degrees would be K px; hills use the bottom
  hline(x + 1, base, w - 2, P_EDGE);
  for (int b = HZ.here ? HZ.bands - 1 : -1; b >= 0; b--) {
    uint16_t c = mix(P_ACC, P_PANEL, 60 + 150 * b / (HZ.bands > 1 ? HZ.bands - 1 : 1));
    for (int X = x + 1; X < x + w - 1; X++) {
      float az = (X - x - 1) * 360.0f / (w - 2);
      int hh = iround(horizonHeight(b, az) * K);
      if (hh > base - y - 2) hh = base - y - 2;
      if (hh > 0) vline(X, base - hh, hh, c);
    }
  }
  static const char* const NS[5] = {"N", "E", "S", "W", "N"};
  for (int i = 0; i < 5; i++) {
    int X = x + 1 + i * (w - 3) / 4;
    vline(X, base, 3, P_DIM);
    text(X - (i == 4 ? 5 : (i == 0 ? 0 : 2)), base + 2, NS[i], P_DIM);
  }
  // what the shore shows
  float c0 = S.scene.south ? 0.0f : 180.0f;
  for (int s = -1; s <= 1; s += 2) {
    float az = fmodf(c0 + s * 102.0f + 360.0f, 360.0f);
    int X = x + 1 + (int)(az / 360.0f * (w - 3));
    vline(X, y + 2, 4, P_DIM);
  }
  const SceneP& sc = S.scene;
  if (sc.sunEl > -2.0f) {
    int X = x + 1 + (int)(fmodf(sc.sunAz + 360.0f, 360.0f) / 360.0f * (w - 3));
    int Y = base - iround(sqrtf(clampf(sc.sunEl, 0, 90) / 90.0f) * K);
    if (Y < y + 4) Y = y + 4;
    bool behind = HZ.here && sc.sunEl < horizonAngle(sc.sunAz);
    ring(X, Y, 3, behind ? P_DIM : P_WARN);
    if (!behind) disc(X, Y, 1, P_WARN);
  }
  if (sc.moonEl > -2.0f) {
    int X = x + 1 + (int)(fmodf(sc.moonAz + 360.0f, 360.0f) / 360.0f * (w - 3));
    int Y = base - iround(sqrtf(clampf(sc.moonEl, 0, 90) / 90.0f) * K);
    if (Y < y + 4) Y = y + 4;
    disc(X, Y, 2, P_TEXT);
  }
}

static void gMoon(int x, int y, int w, int h) {
  int cx = x + w / 2, cy = y + h / 2, r = (w < h ? w : h) / 2 - 4;
  float ph = S.scene.moonPhase, kx = cosf(ph * 6.2831853f);
  bool waxing = ph < 0.5f; if (S.scene.south) waxing = !waxing;
  for (int dy = -r; dy <= r; dy++) {
    float wd = sqrtf((float)(r * r - dy * dy));
    for (int dx = -(int)wd; dx <= (int)wd; dx++) {
      float xn = waxing ? (float)dx : (float)-dx;
      px(cx + dx, cy + dy, xn > kx * wd ? P_TEXT : P_PANEL);
    }
  }
  ring(cx, cy, r, P_EDGE);
}

static void gTide(int x, int y, int w, int h) {
  frame(x, y, w, h, P_EDGE);
  int mid = y + h / 2;
  for (int X = x + 1; X < x + w - 1; X++) {
    float ph = (X - x) / (float)(w - 2) * 6.2831853f * 1.5f;
    px(X, mid - iround(sinf(ph) * (h / 2 - 4)), P_EDGE);
  }
  int lvl = mid - iround(S.scene.tide * (h / 2 - 4));
  hline(x + 1, lvl, w - 2, P_ACC);
  text(x + 3, y + 2, "HIGH", P_DIM); text(x + 3, y + h - 9, "LOW", P_DIM);
}

// ================================================================== the tools
static float battPct(float v) {
  static const float V[11] = {3.30f, 3.50f, 3.60f, 3.70f, 3.75f, 3.80f, 3.85f, 3.90f, 4.00f, 4.10f, 4.20f};
  static const float Pc[11] = {0, 5, 10, 25, 35, 45, 55, 65, 80, 92, 100};
  if (v <= V[0]) return 0; if (v >= V[10]) return 100;
  int i = 0; while (v > V[i + 1]) i++;
  return Pc[i] + (Pc[i + 1] - Pc[i]) * (v - V[i]) / (V[i + 1] - V[i]);
}

// ---- self
static void fPower(Card& c) {
  float v = S.in.battV;
  if (v < 2.5f) { none(c, "no reading yet"); return; }
  if (v > 4.32f) { strcpy(c.big, "EXT"); snprintf(c.sub, sizeof(c.sub), "on outside power, %.2f v", v); c.bar = 1; c.tone = P_OK; }
  else {
    float p = battPct(v);
    snprintf(c.big, sizeof(c.big), "%d", iround(p)); strcpy(c.unit, "%");
    snprintf(c.sub, sizeof(c.sub), "cell %.2f v", v);
    c.bar = p / 100.0f; c.tone = p < 15 ? P_BAD : (p < 30 ? P_WARN : P_TEXT);
  }
  row(c, "CELL", "%.2f V", v);
  row(c, "CHARGE", v > 4.32f ? "external" : "%d %%", iround(battPct(v)));
  row(c, "UNEASY", "below 3.45 V");
}
static void fMemory(Card& c) {
  Life& L = life();
  if (S.in.store == 1) { strcpy(c.big, "CARD"); snprintf(c.sub, sizeof(c.sub), "%.1f gb in use", S.in.cardGB); c.tone = P_OK; }
  else if (S.in.store == 2) { strcpy(c.big, "FLASH"); strcpy(c.sub, "1.5 mb of its own"); }
  else { strcpy(c.big, "NONE"); strcpy(c.sub, "nothing is kept"); c.tone = P_WARN; }
  if (S.in.card == 2) { strcpy(c.sub, "card: type format"); c.tone = P_WARN; }
  row(c, "STORE", S.in.store == 1 ? "card" : (S.in.store == 2 ? "flash" : "none"));
  if (S.in.card) row(c, "CARD", "%.1f GB %s", S.in.cardGB, S.in.card == 1 ? "in use" : "unread");
  else row(c, "CARD", "none");
  row(c, "WAKES", "%lu", (unsigned long)L.wakes);
  row(c, "WORDS", "%lu", (unsigned long)L.words);
  row(c, "SOUNDED", "%lu rooms", (unsigned long)L.soundings);
}
static void actPortal() { S.wantPortal = true; }
static void fLink(Card& c) {
  const uint8_t* ip = S.in.ip;
  if (S.in.online) {
    strcpy(c.big, "ONLINE"); c.tone = P_OK;
    snprintf(c.sub, sizeof(c.sub), "%d.%d.%d.%d  %d dbm", ip[0], ip[1], ip[2], ip[3], S.in.wifiRssi);
    c.bar = clampf((S.in.wifiRssi + 95) / 60.0f, 0, 1);
  } else { strcpy(c.big, "OFFLINE"); c.tone = P_DIM; strcpy(c.sub, "enter: wifi setup"); }
  if (S.in.online) row(c, "ADDRESS", "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  if (S.in.online) row(c, "SIGNAL", "%d dBm", S.in.wifiRssi);
  row(c, "NAME", "missingno.local");
  row(c, "UPDATES", S.in.ota ? "listening" : "off");
  row(c, "KIN", "esp-now, ch 1");
}
static void fSystem(Card& c) {
  uint32_t s = (uint32_t)S.t;
  snprintf(c.big, sizeof(c.big), "%lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60)); strcpy(c.unit, "UP");
  snprintf(c.sub, sizeof(c.sub), "%d fps  %lu kb", iround(P.fps), (unsigned long)(S.in.heapFree / 1024));
  row(c, "AWAKE", "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  row(c, "FRAMES", "%d / s", iround(P.fps));
  row(c, "HEAP", "%lu KB free", (unsigned long)(S.in.heapFree / 1024));
  row(c, "DRAWN", "%lu", (unsigned long)S.frame);
  row(c, "TOOLS", "%d", proToolCount());
}

// ---- hand
static void fLevel(Card& c) {
  computeLevel();
  float worst = maxf(fabsf(levX), fabsf(levY));
  snprintf(c.big, sizeof(c.big), "%.1f", levFlat ? sqrtf(levX * levX + levY * levY) : levX); strcpy(c.unit, "DEG");
  snprintf(c.sub, sizeof(c.sub), levFlat ? "x%+.*f y%+.*f" : "r%+.*f p%+.*f",
           fabsf(levX) < 9.95f ? 1 : 0, levX, fabsf(levY) < 9.95f ? 1 : 0, levY);
  c.tone = worst < 0.5f ? P_OK : P_TEXT;
  c.gfx = gLevel;
  row(c, "LYING", levFlat ? "flat" : "upright");
  row(c, levFlat ? "X" : "ROLL", "%+.1f deg", levX);
  row(c, levFlat ? "Y" : "PITCH", "%+.1f deg", levY);
  row(c, "TRUE", worst < 0.5f ? "yes" : "no");
}
static void actPeaks() { resetPeaks(); }
static void fMotion(Card& c) {
  snprintf(c.big, sizeof(c.big), "%.2f", S.in.accG); strcpy(c.unit, "G");
  snprintf(c.sub, sizeof(c.sub), "peak %.2f g", P.peakG);
  c.spark = &P.acc; c.sparkMax = 255;
  c.tone = S.in.freefall ? P_BAD : (S.in.motion > 0.7f ? P_WARN : P_TEXT);
  row(c, "NOW", "%.2f g", S.in.accG);
  row(c, "PEAK", "%.2f g", P.peakG);
  row(c, "SPIN", "%d deg/s", iround(S.in.spin));
  row(c, "FASTEST", "%d deg/s", iround(P.spinMax));
  row(c, "FALLS", "%d", P.falls);
  row(c, "HELD", S.in.motion > 0.7f ? "shaken" : (S.in.motion > 0.15f ? "handled" : "still"));
}
static void fSteps(Card& c) {
  snprintf(c.big, sizeof(c.big), "%lu", (unsigned long)S.in.steps); strcpy(c.unit, "WAKE");
  snprintf(c.sub, sizeof(c.sub), "%d/min", iround(P.cadence));
  row(c, "THIS WAKE", "%lu", (unsigned long)S.in.steps);
  row(c, "CADENCE", "%d / min", iround(P.cadence));
  row(c, "ABOUT", "%.2f km", S.in.steps * 0.00075f);
}
static void fWires(Card& c) {
  if (S.in.antenna < 0) { none(c, "no ext.io unit"); row(c, "UNIT", "absent"); return; }
  snprintf(c.big, sizeof(c.big), "%d", iround(S.in.antenna * 100)); strcpy(c.unit, "%");
  strcpy(c.sub, "hands, hum, static");
  c.spark = &P.wires; c.sparkMax = 255; c.tone = S.in.antenna > 0.3f ? P_WARN : P_TEXT;
  row(c, "AGITATION", "%d %%", iround(S.in.antenna * 100));
  row(c, "UNIT", "ext.io 0x45");
}

static void actTheremin() { thereminStart(); mindSay("tilt it to sing. raise it to swell. twist it to tremble.", 7.0f, 2, false); }
static void fTheremin(Card& c) {
  char sc[16]; strncpy(sc, thereminScaleName(S.thereScale), sizeof(sc) - 1); sc[sizeof(sc) - 1] = 0;
  for (char* q = sc; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
  strcpy(c.big, sc);
  if (S.thereOct) snprintf(c.unit, sizeof(c.unit), "OCT %+d", S.thereOct);
  strcpy(c.sub, "enter: it sings");
  c.tone = P_ACC;
  row(c, "PITCH", "tilt it");
  row(c, "LOUDER", "raise it");
  row(c, "TREMBLE", "twist it");
  row(c, "SCALE", "%s", thereminScaleName(S.thereScale));
  row(c, "HOLD", "enter or G0");
}

// ---- room
static void actMeter() { P.peakDb = -120; }
static void fMeter(Card& c) {
  snprintf(c.big, sizeof(c.big), "%d", iround(S.in.levelDb)); strcpy(c.unit, "DBFS");
  snprintf(c.sub, sizeof(c.sub), "%+d over quiet", iround(S.in.levelDb - S.in.floorDb));
  c.spark = &P.level; c.sparkMax = 255; c.gfx = gSpectrum;
  row(c, "LEVEL", "%.1f dBFS", S.in.levelDb);
  row(c, "QUIET", "%.1f dBFS", S.in.floorDb);
  row(c, "PEAK", "%.1f dBFS", P.peakDb);
  row(c, "OVER", "%+.1f dB", S.in.levelDb - S.in.floorDb);
  row(c, "OWN VOICE", "removed");
}
static void fTuner(Card& c) {
  static const char* const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  c.gfx = gTuner;
  if (S.in.pitchHz <= 20) { none(c, "hum a steady note"); row(c, "HEARS", "no tone"); return; }
  int mi; float cents = tunerCents(&mi);
  snprintf(c.big, sizeof(c.big), "%s%d", NN[((mi % 12) + 12) % 12], mi / 12 - 1);
  snprintf(c.unit, sizeof(c.unit), "%+d", iround(cents));
  snprintf(c.sub, sizeof(c.sub), "%.1f hz", S.in.pitchHz);
  c.tone = fabsf(cents) < 5 ? P_OK : P_TEXT;
  row(c, "NOTE", "%s", c.big);
  row(c, "FREQ", "%.2f Hz", S.in.pitchHz);
  row(c, "CENTS", "%+d", iround(cents));
  row(c, "A4", "440 Hz");
}
static void actEcho() {
  if (S.muted) { mindSay("i am muted. i cannot strike.", 3.0f, 2, false); return; }
  S.wantSounding = true;
}
static void fEcho(Card& c) {
  static const char* const VERD[5] = {"open air", "close", "a room", "a hall", "vaulted"};
  const Sounding& so = S.sounding;
  c.gfx = gEcho;
  if (!so.valid) { none(c, "enter strikes the room"); row(c, "STRUCK", "%lu times", (unsigned long)life().soundings); return; }
  snprintf(c.big, sizeof(c.big), "%d", (int)so.tailMs); strcpy(c.unit, "MS");
  snprintf(c.sub, sizeof(c.sub), "%s", VERD[so.verdict > 4 ? 4 : so.verdict]);
  row(c, "TAIL", "%d ms", (int)so.tailMs);
  row(c, "SPACE", "%s", VERD[so.verdict > 4 ? 4 : so.verdict]);
  row(c, "QUIET", "%.0f dBFS", so.floorDb);
  row(c, "STRUCK", "%lu times", (unsigned long)life().soundings);
}
static void actKnock() { P.knockN = 0; P.knocks = 0; }
static int knockBpm() {
  if (P.knockN < 3 || S.t - P.knockT[P.knockN - 1] > 4.0f) return 0;
  float iv[15]; int n = 0;
  for (int i = P.knockN - 1; i > 0 && n < 8; i--) {
    float d = P.knockT[i] - P.knockT[i - 1];
    if (d > 2.0f) break;
    iv[n++] = d;
  }
  if (n < 2) return 0;
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && iv[j] < iv[j - 1]; j--) { float t = iv[j]; iv[j] = iv[j - 1]; iv[j - 1] = t; }
  return iround(60.0f / iv[n / 2]);
}
static void fKnock(Card& c) {
  int bpm = knockBpm();
  c.gfx = gKnocks;
  if (bpm) { snprintf(c.big, sizeof(c.big), "%d", bpm); strcpy(c.unit, "BPM"); }
  else { snprintf(c.big, sizeof(c.big), "%lu", (unsigned long)P.knocks); strcpy(c.unit, "HEARD"); }
  snprintf(c.sub, sizeof(c.sub), bpm ? "%lu knocks heard" : "knock a steady beat", (unsigned long)P.knocks);
  row(c, "TEMPO", bpm ? "%d bpm" : "--", bpm);
  row(c, "KNOCKS", "%lu", (unsigned long)P.knocks);
  row(c, "LAST", P.knockN ? "%.1f s ago" : "--", P.knockN ? S.t - P.knockT[P.knockN - 1] : 0.0f);
}

// ---- radio
static void fEther(Card& c) {
  if (S.in.apCount < 0) { none(c, "census coming"); return; }
  snprintf(c.big, sizeof(c.big), "%d", S.in.apCount); strcpy(c.unit, "APS");
  snprintf(c.sub, sizeof(c.sub), S.in.apCount ? "strongest %d dbm" : "dead air", S.in.apRssi);
  c.spark = &P.ap; c.sparkMax = 0; c.tone = S.in.apCount >= 15 ? P_WARN : P_TEXT;
  row(c, "HEARD", "%d networks", S.in.apCount);
  if (S.in.apCount) row(c, "STRONGEST", "%d dBm", S.in.apRssi);
  row(c, "NEW AIR", "%d %%", iround(S.in.etherNovelty * 100));
  row(c, "MET", "%d times before", S.in.etherVisits);
  row(c, "KEEPS", "hashes only");
}
static void fKin(Card& c) {
  snprintf(c.big, sizeof(c.big), "%d", S.in.peers); strcpy(c.unit, "NEAR");
  if (S.in.peers) { snprintf(c.sub, sizeof(c.sub), "their floor %.1f hz", S.in.peerRootHz); c.tone = P_OK; }
  else { strcpy(c.sub, "none of its kind"); c.live = false; }
  row(c, "OTHERS", "%d", S.in.peers);
  if (S.in.peers) row(c, "THEIR ROOT", "%.1f Hz", S.in.peerRootHz);
  row(c, "OWN ROOT", "%.1f Hz", S.sound.rootHz);
  row(c, "REACH", "a few metres");
}

// ---- air
static void fTemp(Card& c) {
  if (!S.in.haveWx) { none(c, "needs wifi + place"); return; }
  float t = S.in.tempC, rh = clampf(S.in.humid, 1, 100);
  float g = logf(rh / 100.0f) + 17.62f * t / (243.12f + t), dew = 243.12f * g / (17.62f - g);
  snprintf(c.big, sizeof(c.big), "%.1f", t); strcpy(c.unit, "C");
  snprintf(c.sub, sizeof(c.sub), "%d%% rh  dew %d c", iround(rh), iround(dew));
  c.tone = t < 0 ? P_ACC : (t > 31 ? P_WARN : P_TEXT);
  row(c, "AIR", "%.1f C", t);
  row(c, "HUMIDITY", "%d %%", iround(rh));
  row(c, "DEW POINT", "%.1f C", dew);
  row(c, "FROM", "open-meteo");
}
static void fPress(Card& c) {
  if (!S.in.haveWx) { none(c, "needs wifi + place"); return; }
  float tr = S.in.pressTrend;
  const char* word = tr < -3.0f ? "falling fast" : (tr < -1.0f ? "falling" : (tr > 3.0f ? "rising fast" : (tr > 1.0f ? "rising" : "steady")));
  snprintf(c.big, sizeof(c.big), "%d", iround(S.in.pressHpa)); strcpy(c.unit, "HPA");
  snprintf(c.sub, sizeof(c.sub), "%+.1f/3h %s", tr, word);
  c.tone = tr < -1.5f ? P_WARN : P_TEXT;
  row(c, "SEA LEVEL", "%.1f hPa", S.in.pressHpa);
  row(c, "3 HOURS", "%+.1f hPa", tr);
  row(c, "TENDENCY", "%s", word);
}
static void fWind(Card& c) {
  c.gfx = gWind;
  if (!S.in.haveWx) { none(c, "needs wifi + place"); return; }
  static const float BF[12] = {1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
  int f = 0; while (f < 12 && S.in.windKmh >= BF[f]) f++;
  snprintf(c.big, sizeof(c.big), "%d", iround(S.in.windKmh)); strcpy(c.unit, "KM/H");
  snprintf(c.sub, sizeof(c.sub), "from %s, force %d", dir16(S.in.windDir), f);
  c.tone = f >= 7 ? P_WARN : P_TEXT;
  row(c, "SPEED", "%.0f km/h", S.in.windKmh);
  row(c, "FROM", "%s %d deg", dir16(S.in.windDir), iround(S.in.windDir));
  row(c, "BEAUFORT", "%d", f);
}
static const char* wmoText(int w) {
  if (w == 0) return "CLEAR"; if (w <= 2) return "FAIR"; if (w == 3) return "OVERCAST";
  if (w == 45 || w == 48) return "FOG"; if (w >= 51 && w <= 57) return "DRIZZLE";
  if (w >= 61 && w <= 67) return "RAIN"; if (w >= 71 && w <= 77) return "SNOW";
  if (w >= 80 && w <= 82) return "SHOWERS"; if (w == 85 || w == 86) return "SNOWFALL";
  if (w >= 95) return "THUNDER"; return "--";
}
static void fWeather(Card& c) {
  if (!S.in.haveWx) { none(c, "needs wifi + place"); return; }
  strcpy(c.big, wmoText(S.in.wmo));
  snprintf(c.sub, sizeof(c.sub), "cloud %d%%  %d km", iround(S.in.cloud * 100), iround(S.in.visKm));
  c.tone = S.in.wmo >= 95 ? P_WARN : P_TEXT;
  row(c, "SKY", "%s", wmoText(S.in.wmo));
  row(c, "CLOUD", "%d %%", iround(S.in.cloud * 100));
  row(c, "SIGHT", "%.1f km", S.in.visKm);
  row(c, "WMO CODE", "%d", S.in.wmo);
}

// ---- land
static void fPos(Card& c) {
  double lat, lon;
  if (!where(lat, lon)) { none(c, "no gps, no town"); return; }
  const char* src = S.in.gpsFix ? "gps" : (S.in.haveFix ? "named town" : "last known");
  snprintf(c.big, sizeof(c.big), "%.4f", lat);
  snprintf(c.sub, sizeof(c.sub), "lon %.4f", lon);
  c.tone = S.in.gpsFix ? P_OK : P_TEXT;
  row(c, "LAT", "%.5f", lat);
  row(c, "LON", "%.5f", lon);
  row(c, "FROM", "%s", src);
  if (S.in.gpsFix) { row(c, "ALTITUDE", "%d m", iround(S.in.altM)); row(c, "SPEED", "%.1f km/h", S.in.speedKmh); }
  else if (HZ.here) row(c, "GROUND", "%d m (map)", HZ.groundM);
}
static void fHorizon(Card& c) {
  c.gfx = gHorizon;
  if (!HZ.open) { strcpy(c.big, "NO MAP"); c.live = false; strcpy(c.sub, "tools/horizon.py"); row(c, "MAP", "none"); return; }
  if (!HZ.here) { strcpy(c.big, "OFF MAP"); c.live = false; strcpy(c.sub, "beyond its map"); row(c, "POINTS", "%lu", (unsigned long)HZ.points); return; }
  snprintf(c.big, sizeof(c.big), "%.1f", HZ.peakAng); strcpy(c.unit, "DEG");
  snprintf(c.sub, sizeof(c.sub), "%s, %d km off", dir16(HZ.peakAz), iround(HZ.peakKm));
  row(c, "HIGHEST", "%s %d deg", dir16(HZ.peakAz), iround(HZ.peakAz));
  row(c, "UP", "%.2f deg", HZ.peakAng);
  row(c, "AWAY", "%.1f km", HZ.peakKm);
  row(c, "GROUND", "%d m", HZ.groundM);
  row(c, "POINT", "%.1f km off", HZ.offKm);
  row(c, "FROM", "%s", HZ.sources == 1 ? "usgs 3dep" : (HZ.sources == 2 ? "copernicus" : "3dep+glo30"));
}
static void fDrift(Card& c) {
  snprintf(c.big, sizeof(c.big), "%lu", (unsigned long)S.cellsKnown); strcpy(c.unit, "CELLS");
  snprintf(c.sub, sizeof(c.sub), "%lu lines  %lu found", (unsigned long)S.linesCrossed, (unsigned long)S.placesFound);
  row(c, "CELLS", "%lu (1 km)", (unsigned long)S.cellsKnown);
  row(c, "LINES", "%lu crossed", (unsigned long)S.linesCrossed);
  row(c, "FOUND", "%lu places", (unsigned long)S.placesFound);
}

// ---- orbit
static void fGps(Card& c) {
  snprintf(c.big, sizeof(c.big), "%d", S.in.sats); strcpy(c.unit, "SATS");
  if (S.in.gpsFix) { snprintf(c.sub, sizeof(c.sub), "fix  %d m  %.0f km/h", iround(S.in.altM), S.in.speedKmh); c.tone = P_OK; }
  else { strcpy(c.sub, S.in.sats ? "searching" : "no receiver"); c.live = S.in.sats > 0; }
  row(c, "SATELLITES", "%d", S.in.sats);
  row(c, "FIX", S.in.gpsFix ? "yes" : "no");
  if (S.in.gpsFix) { row(c, "ALTITUDE", "%d m", iround(S.in.altM)); row(c, "SPEED", "%.1f km/h", S.in.speedKmh); }
  row(c, "ORBIT", "20200 km");
}
static void fClock(Card& c) {
  if (!S.in.haveTime) { none(c, "needs wifi or gps"); return; }
  hhmm(c.big, sizeof(c.big), S.in.utc, false); strcpy(c.unit, zone());
  int y, m, d; ymd(S.in.utc, y, m, d);
  int sh = (int)S.solarHour, sm = (int)((S.solarHour - sh) * 60.0f);
  snprintf(c.sub, sizeof(c.sub), "%04d-%02d-%02d", y, m, d);
  char b[12]; hhmm(b, sizeof(b), S.in.utc, true);
  row(c, zone(), "%s", b);
  if (S.in.haveTz) { char u[12]; long s = (long)fmod(S.in.utc, 86400.0); snprintf(u, sizeof(u), "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60); row(c, "UTC", "%s", u); }
  row(c, "BY THE SUN", "%02d:%02d", sh, sm);
  row(c, "DATE", "%04d-%02d-%02d", y, m, d);
}

// ---- sky
static void fSun(Card& c) {
  const SceneP& sc = S.scene;
  c.gfx = gHorizon;
  if (!S.in.haveTime) { none(c, "needs wifi or gps"); return; }
  snprintf(c.big, sizeof(c.big), "%.1f", sc.sunEl); strcpy(c.unit, "DEG");
  char r[8] = "--", s[8] = "--";
  if (P.sunOk && P.rise) hhmm(r, sizeof(r), P.rise, false);
  if (P.sunOk && P.set) hhmm(s, sizeof(s), P.set, false);
  bool up = sc.sunEl > -0.833f;
  snprintf(c.sub, sizeof(c.sub), "%s %s", up ? "sets" : "rises", up ? s : r);
  c.tone = up ? P_WARN : P_TEXT;
  row(c, "UP", "%.1f deg", sc.sunEl);
  row(c, "BEARING", "%s %d", dir16(sc.sunAz), iround(sc.sunAz));
  row(c, "RISES", "%s", r);
  row(c, "SETS", "%s", s);
  if (HZ.here && P.sunOk) {
    char a[8] = "--", b[8] = "--";
    if (P.ridgeRise) hhmm(a, sizeof(a), P.ridgeRise, false);
    if (P.ridgeSet) hhmm(b, sizeof(b), P.ridgeSet, false);
    row(c, "OVER LAND", "%s", a);
    row(c, "UNDER LAND", "%s", b);
  }
}
static void fMoon(Card& c) {
  const SceneP& sc = S.scene;
  static const char* const PH[8] = {"new", "waxing crescent", "first quarter", "waxing gibbous", "full", "waning gibbous", "last quarter", "waning crescent"};
  float illum = 50.0f * (1.0f - cosf(sc.moonPhase * 6.2831853f));
  const char* name = PH[(int)(sc.moonPhase * 8.0f + 0.5f) & 7];
  snprintf(c.big, sizeof(c.big), "%d", iround(illum)); strcpy(c.unit, "% LIT");
  snprintf(c.sub, sizeof(c.sub), "%s", name);
  c.gfx = gMoon;
  row(c, "LIT", "%d %%", iround(illum));
  row(c, "AGE", "%.1f days", sc.moonPhase * 29.53f);
  if (S.in.haveTime) { row(c, "UP", "%.1f deg", sc.moonEl); row(c, "BEARING", "%s %d", dir16(sc.moonAz), iround(sc.moonAz)); }
}
static void fTide(Card& c) {
  float t = S.scene.tide;
  snprintf(c.big, sizeof(c.big), "%+.2f", t);
  snprintf(c.sub, sizeof(c.sub), "%s, an ideal ocean", P.tideRate > 0.0002f ? "rising" : (P.tideRate < -0.0002f ? "falling" : "turning"));
  c.bar = (t + 1.0f) * 0.5f; c.gfx = gTide;
  row(c, "LEVEL", "%+.2f", t);
  row(c, "GOING", "%s", P.tideRate > 0.0002f ? "up" : (P.tideRate < -0.0002f ? "down" : "turning"));
  row(c, "FROM", "sun + moon");
  row(c, "NOT", "your harbour");
}

// =============================================================== the layout
struct Tool { const char* name; void (*fill)(Card&); void (*act)(); const char* actName; bool direct; };   // direct: enter acts at once
struct Band { const char* name; const char* reach; const char* what; uint8_t n; Tool t[5]; };
static const Band BANDS[] = {
  {"SELF", "0 M", "ITS OWN BODY", 4, {{"POWER", fPower, nullptr, nullptr}, {"MEMORY", fMemory, nullptr, nullptr},
                                      {"LINK", fLink, actPortal, "OPEN WIFI SETUP"}, {"SYSTEM", fSystem, nullptr, nullptr}}},
  {"HAND", "1 M", "HOW IT IS HELD", 5, {{"LEVEL", fLevel, nullptr, nullptr}, {"MOTION", fMotion, actPeaks, "RESET PEAKS"},
                                        {"STEPS", fSteps, nullptr, nullptr}, {"WIRES", fWires, nullptr, nullptr},
                                        {"THEREMIN", fTheremin, actTheremin, "PLAY IT", true}}},
  {"ROOM", "10 M", "THE ROOM AROUND IT", 4, {{"METER", fMeter, actMeter, "RESET PEAK"}, {"TUNER", fTuner, nullptr, nullptr},
                                             {"ECHO", fEcho, actEcho, "STRIKE THE ROOM"}, {"KNOCK", fKnock, actKnock, "RESET"}}},
  {"RF", "100 M", "THE RADIO AIR", 2, {{"ETHER", fEther, nullptr, nullptr}, {"KIN", fKin, nullptr, nullptr}}},
  {"AIR", "10 KM", "THE WEATHER OVERHEAD", 4, {{"TEMP", fTemp, nullptr, nullptr}, {"PRESSURE", fPress, nullptr, nullptr},
                                               {"WIND", fWind, nullptr, nullptr}, {"WEATHER", fWeather, nullptr, nullptr}}},
  {"LAND", "100 KM", "WHERE IT STANDS", 3, {{"POSITION", fPos, nullptr, nullptr}, {"HORIZON", fHorizon, nullptr, nullptr},
                                            {"DRIFT", fDrift, nullptr, nullptr}}},
  {"SAT", "20000 KM", "OVERHEAD, IN ORBIT", 2, {{"GPS", fGps, nullptr, nullptr}, {"CLOCK", fClock, nullptr, nullptr}}},
  {"SKY", "1 AU", "THE SUN AND THE MOON", 3, {{"SUN", fSun, nullptr, nullptr}, {"MOON", fMoon, nullptr, nullptr},
                                              {"TIDE", fTide, nullptr, nullptr}}},
};
static constexpr int NB = sizeof(BANDS) / sizeof(BANDS[0]);

int proToolCount() { int n = 0; for (const Band& b : BANDS) n += b.n; return n; }

static void click(int d) {
  Note n = {N_PLUCK, S.sound.rootHz * (d > 0 ? 6.0f : 4.0f), 0.28f};
  S.notes.push(n);
}

static void stepTool(int d) {                                // through every tool, in reach order
  int b = S.proBand, t = S.proTool + d;
  while (t < 0) { b = (b + NB - 1) % NB; t += BANDS[b].n; }
  while (t >= BANDS[b].n) { t -= BANDS[b].n; b = (b + 1) % NB; }
  S.proBand = (uint8_t)b; S.proTool = (uint8_t)t;
}

bool proKey(int c) {
  if (!S.pro || S.typed[0]) return false;                   // typing goes to the mind
  bool up = c == K_VOL_UP || c == ';', down = c == K_VOL_DOWN || c == '.';
  bool left = c == K_VIEW_PREV || c == ',', right = c == K_VIEW_NEXT || c == '/';
  const Band& b = BANDS[S.proBand];
  if (S.proOpen) {
    if (left || up) { stepTool(-1); click(-1); return true; }
    if (right || down) { stepTool(1); click(1); return true; }
    if (c == K_TAB) { S.proBand = (uint8_t)((S.proBand + 1) % NB); S.proTool = 0; click(1); return true; }
    if (c == K_ENTER) {
      const Tool& t = b.t[S.proTool];
      if (t.act) { t.act(); click(1); } else { S.proOpen = false; click(-1); }
      return true;
    }
    if (c == K_ESC || c == K_BACK) { S.proOpen = false; click(-1); return true; }
    return false;
  }
  if (left || right || c == K_TAB) {
    int d = left ? -1 : 1;
    S.proBand = (uint8_t)((S.proBand + NB + d) % NB);
    if (S.proTool >= BANDS[S.proBand].n) S.proTool = (uint8_t)(BANDS[S.proBand].n - 1);
    click(d); return true;
  }
  if (up || down) { S.proTool = (uint8_t)((S.proTool + b.n + (up ? -1 : 1)) % b.n); click(up ? -1 : 1); return true; }
  if (c == K_ENTER) {
    const Tool& t = b.t[S.proTool];
    if (t.direct && t.act) t.act(); else S.proOpen = true;
    click(1); return true;
  }
  if (c == K_BACK) return true;                              // nothing to delete; don't fall through
  return false;                                              // escape: the mind takes it back to the shore
}

bool proGoto(const char* name) {
  for (int b = 0; b < NB; b++)
    for (int t = 0; t < BANDS[b].n; t++) {
      const char* a = BANDS[b].t[t].name; const char* q = name; bool same = true;
      for (; *a || *q; a++, q++) {
        char x = *q >= 'a' && *q <= 'z' ? (char)(*q - 32) : *q;
        if (x != *a) { same = false; break; }
      }
      if (same) { S.proBand = (uint8_t)b; S.proTool = (uint8_t)t; S.proOpen = true; return true; }
    }
  return false;
}

// ---------------------------------------------------------------- drawing
static int fitText(int x, int y, const char* s, int maxw, uint16_t c) {
  int n = maxw / 6; char b[48];
  strncpy(b, s, sizeof(b) - 1); b[sizeof(b) - 1] = 0;
  if ((int)strlen(b) > n && n > 0) b[n] = 0;
  return text(x, y, b, c);
}

static void header() {
  rect(0, 0, W, 11, P_PANEL);
  drawMarkIcon(3, 1, P_ACC);
  int x = text(16, 2, "MISSINGNO", P_TEXT);
  text(x + 4, 2, "PRO", P_ACC);
  char b[24]; int xr = W - 3;
  if (S.in.battV > 2.5f) {
    bool ext = S.in.battV > 4.32f;
    int p = iround(battPct(S.in.battV));
    frame(xr - 13, 2, 12, 7, P_DIM); rect(xr - 1, 4, 1, 3, P_DIM);
    rect(xr - 12, 3, ext ? 10 : (p * 10 + 50) / 100, 5, ext ? P_OK : (p < 15 ? P_BAD : (p < 30 ? P_WARN : P_TEXT)));
    xr -= 18;
  }
  if (S.in.online) { for (int i = 0; i < 3; i++) rect(xr - 9 + i * 3, 8 - i * 2, 2, 1 + i * 2, S.in.wifiRssi > -60 - i * 10 ? P_TEXT : P_EDGE); xr -= 13; }
  if (S.in.haveTime) { hhmm(b, sizeof(b), S.in.utc, false); textRight(xr, 2, b, P_TEXT); }
  hline(0, 11, W, P_EDGE);
}

// Eight stops on a scale of distance, from itself to the sun.
static void ruler() {
  for (int i = 0; i < NB; i++) {
    int cx = 15 + i * 30;
    bool sel = i == S.proBand;
    textCenter(cx, 14, BANDS[i].name, sel ? P_ACC : P_DIM);
    vline(cx, 24, sel ? 4 : 3, sel ? P_ACC : P_EDGE);
    if (sel) { hline(cx - 13, 23, 27, P_ACC); }
  }
  hline(2, 25, W - 4, P_EDGE);
  for (int i = 0; i < NB - 1; i++) px(30 + i * 30, 25, P_DIM);
}

static void drawTile(const Tool& t, int x, int y, int w, int h, bool sel) {
  Card c; clearCard(c); t.fill(c);
  rect(x, y, w, h, P_PANEL);
  frame(x, y, w, h, sel ? P_ACC : P_EDGE);
  if (sel) rect(x, y, 2, h, P_ACC);
  text(x + 5, y + 3, t.name, sel ? P_ACC : P_DIM);
  if (c.unit[0]) textRight(x + w - 4, y + 3, c.unit, P_DIM);
  int k = h >= 60 ? 3 : 2;
  while (k > 1 && (int)strlen(c.big) * 6 * k > w - 10) k--;
  int by = y + (h >= 60 ? 16 : 13);
  int bx_end = textBig(x + 5, by, c.big, c.live ? c.tone : P_DIM, k);
  bool room = x + w - 6 - bx_end >= 34;
  if (c.spark && room) spark(bx_end + 6, by, x + w - 6 - bx_end - 4, 8 * k, *c.spark, c.sparkMax, P_ACC);
  if (c.bar >= 0) {
    hline(x + 5, y + h - 13, w - 10, P_EDGE);
    hline(x + 5, y + h - 13, (int)(clampf(c.bar, 0, 1) * (w - 10)), c.tone == P_TEXT ? P_ACC : c.tone);
  }
  if (h >= 60 && c.gfx) c.gfx(x + 5, by + 8 * k + 4, w - 10, y + h - 14 - (by + 8 * k + 4));
  fitText(x + 5, y + h - 10, c.sub, w - 10, P_DIM);
}

static void footer(const char* hint) {
  hline(0, 123, W, P_EDGE);
  rect(0, 124, W, 11, P_PANEL);
  int len = (int)strlen(S.typed);
  if (len) {
    int x = text(3, 126, ">", P_DIM);
    int off = len > 36 ? len - 36 : 0;
    x = text(x + 3, 126, S.typed + off, P_TEXT);
    if ((S.frame / 12) & 1) rect(x + 1, 126, 4, 7, P_TEXT);
    return;
  }
  if (S.say[0] && S.sayAge < S.sayHold) {                    // it is still here, and still talking
    drawMarkIcon(2, 125, C_PHOS);
    int n = (int)strlen(S.say), shown = (int)(S.sayAge * 30.0f);
    if (shown > n) shown = n;
    int start = shown > 36 ? shown - 36 : 0;
    if (start > 0 && shown == n) { while (start < n && S.say[start - 1] != ' ') start++; }
    textN(14, 126, S.say + start, shown - start, C_PHOS);
    return;
  }
  fitText(3, 126, hint, W - 6, P_DIM);
}

static void drawDashboard() {
  const Band& b = BANDS[S.proBand];
  text(3, 29, b.what, P_DIM);
  char r[24]; snprintf(r, sizeof(r), "REACH %s", b.reach);
  textRight(W - 4, 29, r, P_DIM);
  const int y0 = 39, hAll = 121 - y0;
  if (b.n == 5) {
    int w3 = (W - 8) / 3, w2 = (W - 6) / 2, h = (hAll - 2) / 2;
    for (int i = 0; i < 3; i++) drawTile(b.t[i], 2 + i * (w3 + 2), y0, w3, h, i == S.proTool);
    for (int i = 3; i < 5; i++) drawTile(b.t[i], 2 + (i - 3) * (w2 + 2), y0 + h + 2, w2, h, i == S.proTool);
  } else if (b.n == 4) {
    int w = (W - 6) / 2, h = (hAll - 2) / 2;
    for (int i = 0; i < 4; i++) drawTile(b.t[i], 2 + (i & 1) * (w + 2), y0 + (i >> 1) * (h + 2), w, h, i == S.proTool);
  } else if (b.n == 3) {
    int w = (W - 6) / 2, h = (hAll - 2) / 2;
    drawTile(b.t[0], 2, y0, w, h, S.proTool == 0);
    drawTile(b.t[1], 4 + w, y0, w, h, S.proTool == 1);
    drawTile(b.t[2], 2, y0 + h + 2, W - 4, h, S.proTool == 2);
  } else {
    int w = (W - 6) / 2;
    for (int i = 0; i < b.n; i++) drawTile(b.t[i], 2 + i * (w + 2), y0, w, hAll, i == S.proTool);
  }
  footer(",/ REACH  ;. TOOL  ENTER OPEN  ESC OUT");
}

static void drawOpen() {
  const Band& b = BANDS[S.proBand];
  const Tool& t = b.t[S.proTool];
  Card c; clearCard(c); t.fill(c);
  int x = text(3, 29, b.name, P_DIM);
  x = text(x + 4, 29, ">", P_DIM);
  text(x + 4, 29, t.name, P_ACC);
  char r[24]; snprintf(r, sizeof(r), "REACH %s", b.reach);
  textRight(W - 4, 29, r, P_DIM);
  hline(2, 38, W - 4, P_EDGE);
  const int L = 4, LW = c.gfx || c.spark ? 112 : W - 8;
  // the reading
  int k = 3; while (k > 1 && (int)strlen(c.big) * 6 * k > LW) k--;
  int bx = textBig(L, 43, c.big, c.live ? c.tone : P_DIM, k);
  if (c.unit[0]) text(bx + 3, 43 + 8 * k - 8, c.unit, P_DIM);
  int y = 43 + 8 * k + 3;
  if (c.bar >= 0) { hline(L, y, LW, P_EDGE); hline(L, y, (int)(clampf(c.bar, 0, 1) * LW), c.tone == P_TEXT ? P_ACC : c.tone); y += 4; }
  fitText(L, y, c.sub, LW, P_DIM); y += 11;
  for (int i = 0; i < c.rows && y <= 113; i++, y += 10) {
    text(L, y, c.k[i], P_DIM);
    char v[24]; strncpy(v, c.v[i], sizeof(v)); v[23] = 0;
    int room = (LW - textWidth(c.k[i]) - 6) / 6;
    if (room >= 0 && room < 23) v[room] = 0;
    textRight(L + LW - 1, y, v, P_TEXT);
  }
  // the picture
  if (c.gfx) c.gfx(122, 42, W - 126, 78);
  else if (c.spark) spark(122, 42, W - 126, 78, *c.spark, c.sparkMax, P_ACC);
  char hint[64];
  if (t.act) snprintf(hint, sizeof(hint), "ENTER %s   DEL BACK", t.actName);
  else snprintf(hint, sizeof(hint), ", / NEXT TOOL   ENTER OR DEL BACK");
  footer(hint);
}

void drawPro() {
  clear(P_BG);
  header();
  ruler();
  if (S.proOpen) drawOpen(); else drawDashboard();
  // arriving: the screen resolves from the top, as if it had always been like this
  if (S.proAge < 0.35f) {
    int edge = (int)(S.proAge / 0.35f * H);
    for (int y = edge; y < H; y++)
      for (int x = 0; x < W; x++)
        if ((hash2(x >> 1, y, S.frame) & 3) == 0) fb[y * W + x] = (y - edge < 3) ? P_ACC : P_BG;
  }
}

}  // namespace mn
