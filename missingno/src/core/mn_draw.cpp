#include "mn_draw.h"
#include "mn_horizon.h"
#include "mn_pro.h"
#include "mn_theremin.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

namespace mn {

static CellKnownFn chartKnown = nullptr;
void setChartOracle(CellKnownFn fn) { chartKnown = fn; }

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline int iround(float v) { return (int)floorf(v + 0.5f); }

// =========================================================== the mark itself
// The site's SVG, viewBox 0 0 256 256:
//   body   M128,32 h80 V224 H48 V112 h80 Z     (a backwards L)
//   eyes   24x24 at (72,140) and (160,64)
//   mouth  a line (112,188)-(176,188), stroke 16
//   scraps 28x28 at (44,40) and 16x16 at (92,68), loose in the missing corner
// Drawn here as a signed-distance field so it survives lean, tear and scatter.

static inline float sdBox(float x, float y, float cx, float cy, float hx, float hy) {
  float qx = fabsf(x - cx) - hx, qy = fabsf(y - cy) - hy;
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float in = qx > qy ? qx : qy;
  return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0);
}

// Body = big box minus the missing corner. The corner box runs far off to the
// top-left so that only its two real edges are ever "near".
static inline float sdBody(float x, float y) {
  float dB = sdBox(x, y, 128, 128, 80, 96);
  float dN = sdBox(x, y, -136, -144, 264, 256);
  return dB > -dN ? dB : -dN;
}

static inline float sdSeg(float x, float y, float ax, float bx, float cy) {
  float px = x < ax ? ax : (x > bx ? bx : x);
  float dx = x - px, dy = y - cy;
  return sqrtf(dx * dx + dy * dy);
}

struct MarkCtx {
  int cx, baseY; float s; const FaceP* f; const float* bands; uint32_t frame; float t;
  float eyeH, gx, gy, mHalf;
};

// 0 = nothing, 1 = ink, 2 = interior (lit), 3 = interior (unlit)
static int markSample(const MarkCtx& m, float ux, float uy, int X, int Y) {
  const FaceP& f = *m.f;
  if (f.scatter > 0.01f) {
    int tx = (int)floorf(ux / 22.0f), ty = (int)floorf(uy / 22.0f);
    uint32_t h = hash2(tx, ty, 77u + (m.frame / 5));
    ux += (((int)(h & 255)) - 128) * (f.scatter * 0.42f);
    uy += (((int)((h >> 8) & 255)) - 128) * (f.scatter * 0.30f);
  }
  float d = sdBody(ux, uy);
  if (d > 8.0f) return 0;
  if (d > -8.0f) return 1;                                 // the outline stroke

  // eyes
  float ex = f.gazeX * 5.0f, ey = f.gazeY * 4.0f;
  if (fabsf(ux - (84 + ex)) <= 12 && fabsf(uy - (152 + ey)) <= m.eyeH) return 1;
  if (fabsf(ux - (172 + ex)) <= 12 && fabsf(uy - (76 + ey)) <= m.eyeH) return 1;

  // mouth
  float my = 188.0f;
  if (f.mouthWave > 0.01f) my += sinf(ux * 0.21f + m.t * 17.0f) * 6.0f * f.mouthWave;
  float md = sdSeg(ux, uy, 144 - m.mHalf, 144 + m.mHalf, my);
  float mr = 8.0f + 9.0f * f.mouthOpen;
  if (md <= mr && !(f.mouthOpen > 0.35f && md < mr - 6.0f)) return 1;

  // a clear margin round the features, so they read at any density
  if (md <= mr + 6.0f) return 3;
  if (fabsf(ux - (84 + ex)) <= 18 && fabsf(uy - (152 + ey)) <= m.eyeH + 6) return 3;
  if (fabsf(ux - (172 + ex)) <= 18 && fabsf(uy - (76 + ey)) <= m.eyeH + 6) return 3;

  // interior: the mark's 20% wash, and sound standing up inside it
  int level = iround(f.fill * 16.0f);
  if (m.bands) {
    int b = (int)((ux - 48.0f) * (8.0f / 160.0f));
    if (b < 0) b = 0; if (b > 7) b = 7;
    float hgt = m.bands[b] * 150.0f;
    if (224.0f - uy < hgt) {
      level += 4;
      if ((hash2(X, Y, m.frame >> 1) & 15) < 3) return 2;
    }
  }
  return bayer(X, Y, level) ? 2 : 3;
}

void drawMark(int cx, int baseY, float s, uint16_t ink, const FaceP& f,
              const float* bands, uint32_t frame, float t) {
  MarkCtx m{cx, baseY, s, &f, bands, frame, t, 0, 0, 0, 0};
  m.eyeH = f.eyeOpen <= 0.08f ? 1.6f : 12.0f * clampf(f.eyeOpen, 0.15f, 1.5f);
  m.mHalf = 32.0f * clampf(f.mouthW, 0.1f, 1.6f);
  const float inv = 1.0f / s;
  const float hgt = 192.0f * s;
  int by = baseY + iround(f.bob);
  int y0 = by - (int)hgt - 14, y1 = by + 5;
  int x0 = cx - (int)(88 * s) - 14 - (int)fabsf(f.shear), x1 = cx + (int)(88 * s) + 14 + (int)fabsf(f.shear);
  if (f.scatter > 0.01f) { x0 -= 24; x1 += 24; y0 -= 16; }
  uint16_t wash = mix(ink, C_VOID, 96);

  for (int Y = y0; Y <= y1; Y++) {
    if (Y < 0 || Y >= H) continue;
    float up = (float)(by - Y);                             // px above the feet
    float uy = 224.0f - up * inv;
    float sh = f.shear * (up / hgt);
    for (int X = x0; X <= x1; X++) {
      if (X < 0 || X >= W) continue;
      float ux = ((float)X - cx - sh) * inv + 128.0f;
      int k = markSample(m, ux, uy, X, Y);
      if (!k) continue;
      if (k == 1) fb[Y * W + X] = f.invert ? C_VOID : ink;
      else if (k == 2) fb[Y * W + X] = f.invert ? ink : wash;
      else if (f.invert) fb[Y * W + X] = ink;
    }
  }

  // the loose scraps
  int n = f.scraps < 2 ? 2 : (f.scraps > 9 ? 9 : f.scraps);
  for (int i = 0; i < n; i++) {
    float ux, uy, sz;
    if (i == 0) { ux = 58; uy = 54; sz = 28; }
    else if (i == 1) { ux = 100; uy = 76; sz = 16; }
    else {
      float a = f.scrapSpin * (0.6f + 0.13f * i) + i * 2.399f;
      float r = 118.0f + 22.0f * ((i * 37) % 5);
      ux = 128 + cosf(a) * r; uy = 118 + sinf(a) * r * 0.62f;
      sz = 9.0f + (float)((i * 53) % 12);
    }
    float a2 = f.scrapSpin + i * 1.9f;
    ux += cosf(a2) * 26.0f * f.scrapLift * (i < 2 ? 1.0f : 0.5f);
    uy += sinf(a2 * 1.3f) * 20.0f * f.scrapLift + sinf(t * 0.9f + i) * 3.0f;
    float up = (224.0f - uy) * s;
    int X = cx + iround((ux - 128.0f) * s + f.shear * (up / hgt));
    int Y = by - iround(up);
    int p = iround(sz * s); if (p < 2) p = 2;
    uint16_t c = i < 2 ? ink : mix(ink, C_VOID, 110);
    if (f.glitch > 0.3f && (hash2(i, frame / 3) & 7) == 0) continue;   // drops out
    rect(X - p / 2, Y - p / 2, p, p, c);
  }

  // tearing
  if (f.glitch > 0.02f) {
    int tears = 1 + (int)(f.glitch * 9.0f);
    uint32_t seed = frame / 2;
    for (int i = 0; i < tears; i++) {
      uint32_t h = hash2(i, (int)seed, 991);
      if ((h & 255) > (uint32_t)(40 + f.glitch * 215)) continue;
      int ry = y0 + (int)((h >> 8) % (uint32_t)(y1 - y0 + 1));
      int rh = 1 + (int)((h >> 16) & 3);
      int dx = (int)((h >> 20) % (uint32_t)(3 + f.glitch * 22)) - (int)(1 + f.glitch * 11);
      if (!dx) dx = 2;
      for (int Y = ry; Y < ry + rh && Y <= y1; Y++) {
        if (Y < 0 || Y >= H) continue;
        uint16_t row[W];
        memcpy(row, &fb[Y * W], sizeof(row));
        for (int X = x0 - 10; X <= x1 + 10; X++) {
          int sx = X - dx;
          if (X < 0 || X >= W || sx < 0 || sx >= W) continue;
          fb[Y * W + X] = row[sx];
        }
      }
    }
    // tile corruption: a square of somewhere else
    if (f.glitch > 0.45f) {
      uint32_t h = hash2((int)(frame / 4), 5, 313);
      int sx = x0 + (int)(h % (uint32_t)(x1 - x0 - 8)), sy = y0 + (int)((h >> 8) % (uint32_t)(y1 - y0 - 8));
      int tx = x0 + (int)((h >> 16) % (uint32_t)(x1 - x0 - 8)), ty = y0 + (int)((h >> 24) % (uint32_t)(y1 - y0 - 8));
      for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) px(tx + i, ty + j, get(sx + i, sy + j));
    }
  }
}

// its reflection: every other row, rippled, faint
static void drawMarkReflection(int cx, int baseY, float s, uint16_t ink, const FaceP& f, float t, int waterY) {
  MarkCtx m{cx, baseY, s, &f, nullptr, 0, t, 0, 0, 0, 0};
  m.eyeH = f.eyeOpen <= 0.08f ? 1.6f : 12.0f * clampf(f.eyeOpen, 0.15f, 1.5f);
  m.mHalf = 32.0f * clampf(f.mouthW, 0.1f, 1.6f);
  const float inv = 1.0f / s;
  int x0 = cx - (int)(88 * s) - 8, x1 = cx + (int)(88 * s) + 8;
  int start = baseY + 2 > waterY ? baseY + 2 : waterY;
  for (int Y = start; Y < H; Y++) {
    if ((Y & 1) == 0) continue;
    float up = (float)(Y - baseY) * 1.9f;
    float uy = 224.0f - up * inv;
    if (uy < 20) break;
    float rip = sinf(Y * 0.8f + t * 2.6f) * (1.0f + (Y - baseY) * 0.12f);
    int a = 96 - (Y - baseY) * 3; if (a < 12) a = 12;
    for (int X = x0; X <= x1; X++) {
      float ux = ((float)X - cx - rip) * inv + 128.0f;
      if (markSample(m, ux, uy, X, Y) == 1) blend(X, Y, ink, a);
    }
  }
}

void drawMarkIcon(int x, int y, uint16_t c) {
  frame(x + 4, y, 5, 9, c); frame(x, y + 4, 9, 5, c);
  rect(x + 5, y + 4, 3, 1, 0); px(x + 4, y + 4, c);
  px(x + 6, y + 2, c); px(x + 2, y + 6, c); px(x + 1, y + 1, c);
}

// ================================================================== the sky
struct RGB8 { float r, g, b; };
static const struct { float el; RGB8 top, hor; } SKY[] = {
  {-18, {5, 4, 10},    {20, 17, 40}},
  { -9, {9, 7, 20},    {44, 18, 52}},
  { -3, {20, 14, 36},  {124, 44, 42}},
  {  2, {40, 32, 54},  {170, 88, 44}},
  { 10, {60, 60, 78},  {130, 126, 130}},
  { 35, {74, 76, 92},  {152, 152, 156}},
};
static uint16_t skyTop, skyHor;
static float dayAmt;            // 0 night .. 1 full pale day

static void skyColors(const SceneP& sc) {
  const int n = sizeof(SKY) / sizeof(SKY[0]);
  float el = sc.sunEl;
  RGB8 t, h;
  if (el <= SKY[0].el) { t = SKY[0].top; h = SKY[0].hor; }
  else if (el >= SKY[n - 1].el) { t = SKY[n - 1].top; h = SKY[n - 1].hor; }
  else {
    int i = 0; while (i < n - 2 && el > SKY[i + 1].el) i++;
    float k = (el - SKY[i].el) / (SKY[i + 1].el - SKY[i].el);
    t = {lerpf(SKY[i].top.r, SKY[i + 1].top.r, k), lerpf(SKY[i].top.g, SKY[i + 1].top.g, k), lerpf(SKY[i].top.b, SKY[i + 1].top.b, k)};
    h = {lerpf(SKY[i].hor.r, SKY[i + 1].hor.r, k), lerpf(SKY[i].hor.g, SKY[i + 1].hor.g, k), lerpf(SKY[i].hor.b, SKY[i + 1].hor.b, k)};
  }
  dayAmt = clampf((el + 4.0f) / 14.0f, 0, 1);
  // overcast flattens everything toward slate
  float c = sc.cloud * 0.75f;
  RGB8 slate = {lerpf(9, 78, dayAmt), lerpf(9, 78, dayAmt), lerpf(13, 86, dayAmt)};
  t = {lerpf(t.r, slate.r, c), lerpf(t.g, slate.g, c), lerpf(t.b, slate.b, c)};
  h = {lerpf(h.r, slate.r * 1.25f, c), lerpf(h.g, slate.g * 1.25f, c), lerpf(h.b, slate.b * 1.2f, c)};
  // something near: the phosphor creeps in
  float g = sc.green * 0.4f;
  t = {lerpf(t.r, 6, g), lerpf(t.g, 36, g), lerpf(t.b, 22, g)};
  h = {lerpf(h.r, 14, g), lerpf(h.g, 70, g), lerpf(h.b, 44, g)};
  float fl = sc.flash * 0.85f;
  t = {lerpf(t.r, 210, fl), lerpf(t.g, 214, fl), lerpf(t.b, 235, fl)};
  h = {lerpf(h.r, 225, fl), lerpf(h.g, 225, fl), lerpf(h.b, 240, fl)};
  skyTop = rgb((uint8_t)clampf(t.r, 0, 255), (uint8_t)clampf(t.g, 0, 255), (uint8_t)clampf(t.b, 0, 255));
  skyHor = rgb((uint8_t)clampf(h.r, 0, 255), (uint8_t)clampf(h.g, 0, 255), (uint8_t)clampf(h.b, 0, 255));
}

static void drawSky() {
  const int BANDS = 20;
  for (int y = 0; y <= HORIZON_Y; y++) {
    float k = (float)y / HORIZON_Y; k = k * k * (0.6f + 0.4f * k);
    float fb_ = k * BANDS;
    int b = (int)fb_; int frac = (int)((fb_ - b) * 16.0f);
    uint16_t c0 = mix(skyTop, skyHor, b * 256 / BANDS);
    uint16_t c1 = mix(skyTop, skyHor, (b + 1) * 256 / BANDS);
    uint16_t* row = &fb[y * W];
    for (int x = 0; x < W; x++) row[x] = bayer(x, y, frac) ? c1 : c0;
  }
}

// The shore looks toward the equator: south in the north, north in the south, about
// 200 degrees across. East is on the left looking south and on the right looking north.
static constexpr float AZ_PER_PX = 90.0f / (W * 0.5f - 14);
static inline float azAtX(const SceneP& sc, float x) { return (sc.south ? 0.0f : 180.0f) + (x - W * 0.5f) * AZ_PER_PX; }
// Elevation runs up a square-root curve, so the first few degrees (where hills are) get room.
static constexpr float EL_SCALE = HORIZON_Y - 20;          // px for the full 90 degrees

static void placeBody(const SceneP& sc, float az, float el, int& x, int& y) {
  float d = az - (sc.south ? 0.0f : 180.0f);
  while (d > 180) d -= 360; while (d < -180) d += 360;
  float xf = W * 0.5f + d / AZ_PER_PX;
  x = iround(clampf(xf, 10, W - 10));
  // the centre of the disc sits on the same curve the land is drawn on, so a body
  // goes behind a ridge at the moment it is really that high
  if (el >= 0) y = HORIZON_Y - iround(sqrtf(el / 90.0f) * EL_SCALE);
  else y = HORIZON_Y - iround(el * 1.6f);
}

static void drawStars(const SceneP& sc, uint32_t frame) {
  float night = clampf((-sc.sunEl - 4.0f) / 8.0f, 0, 1) * (1.0f - sc.cloud * 0.85f) * (1.0f - sc.fog * 0.8f);
  if (night <= 0.02f) return;
  int shift = (int)sc.starShift;
  for (int i = 0; i < 72; i++) {
    uint32_t h = hash32(i * 2654435761u + 17);
    int x = (int)((h % W) + shift) % W; if (x < 0) x += W;
    int y = (int)((h >> 8) % (HORIZON_Y - 6));
    int mag = (h >> 20) & 7;                               // 0 bright .. 7 faint
    if (mag / 7.0f > night + 0.15f) continue;
    int br = 250 - mag * 26;
    if ((i % 9) == 0 && ((hash2(i, (int)(frame / 7)) & 3) == 0)) br = br * 5 / 8;   // twinkle
    br = (int)(br * night);
    uint16_t c = scale(C_BONE, br);
    if ((h >> 24) % 11 == 0) c = scale(rgb(150, 175, 255), br);
    else if ((h >> 24) % 13 == 0) c = scale(rgb(255, 160, 130), br);
    px(x, y, c);
    if (mag == 0 && night > 0.8f) { blend(x - 1, y, c, 70); blend(x + 1, y, c, 70); blend(x, y - 1, c, 70); blend(x, y + 1, c, 70); }
  }
}

static void drawMoon(const SceneP& sc, int& mx, int& my, float& lit) {
  lit = 0; mx = -99; my = 0;
  if (sc.moonEl < -5) return;
  placeBody(sc, sc.moonAz, sc.moonEl, mx, my);
  const int r = 7;
  float ph = sc.moonPhase;                                  // 0 new, .5 full
  float illum = 0.5f * (1.0f - cosf(ph * 6.2831853f));
  lit = illum * (1.0f - sc.cloud * 0.7f);
  bool waxing = ph < 0.5f;
  if (sc.south) waxing = !waxing;
  float vis = 1.0f - dayAmt * 0.7f;
  uint16_t bright = mix(skyTop, rgb(232, 228, 210), (int)(256 * vis));
  uint16_t dark = mix(skyTop, rgb(40, 38, 52), (int)(120 * vis));
  float kx = cosf(ph * 6.2831853f);                         // terminator: +1 new .. -1 full
  for (int dy = -r; dy <= r; dy++) {
    float w = sqrtf((float)(r * r - dy * dy));
    for (int dx = -(int)w; dx <= (int)w; dx++) {
      float xn = waxing ? (float)dx : (float)-dx;          // lit limb on +x when waxing
      bool on = xn > kx * w;
      uint16_t c = on ? bright : dark;
      // a little maria so it reads as a moon, not a lamp
      if (on && ((hash2(dx + 9, dy + 9, 4) & 7) == 0)) c = mix(c, skyTop, 70);
      px(mx + dx, my + dy, c);
    }
  }
  if (lit > 0.5f && dayAmt < 0.3f) {                        // halo
    for (int a = 0; a < 40; a++) {
      float an = a * 0.157f;
      blend(mx + iround(cosf(an) * (r + 3)), my + iround(sinf(an) * (r + 3)), bright, 40);
    }
  }
}

// The sun here is a hole with a rim. A gaze blank and pitiless.
static void drawBlackSun(const SceneP& sc, int& sx, int& sy) {
  sx = -99; sy = 0;
  if (sc.sunEl < -5) return;
  placeBody(sc, sc.sunAz, sc.sunEl, sx, sy);
  const int r = 8;
  float low = clampf(1.0f - sc.sunEl / 10.0f, 0, 1);       // 1 at the horizon
  uint16_t rim = mix(rgb(236, 232, 224), C_EMBER, (int)(low * 256));
  float veil = 1.0f - sc.cloud * 0.6f - sc.fog * 0.5f; if (veil < 0.15f) veil = 0.15f;
  for (int dy = -(r + 6); dy <= r + 6; dy++)
    for (int dx = -(r + 6); dx <= r + 6; dx++) {
      float d = sqrtf((float)(dx * dx + dy * dy));
      if (d <= r - 0.5f) px(sx + dx, sy + dy, mix(C_VOID, C_BLOOD, (int)(low * 90)));
      else if (d <= r + 1.2f) blend(sx + dx, sy + dy, rim, (int)(255 * veil));
      else if (d <= r + 6) {
        int lvl = (int)((1.0f - (d - r - 1.2f) / 5.0f) * 9.0f * veil);
        if (bayer(sx + dx, sy + dy, lvl)) blend(sx + dx, sy + dy, rim, 110);
      }
    }
  if (sc.omen == 2) {                                       // it looks back
    int o = iround(sinf(sc.omenT * 3.14159f) * 3.0f);
    hline(sx - 4, sy, 9, rim); if (o > 0) rect(sx - 1, sy - o, 3, 2 * o + 1, rim);
    px(sx, sy, C_VOID);
  }
}

static void drawClouds(const SceneP& sc, float t) {
  if (sc.cloud < 0.06f) return;
  float thr = 1.0f - sc.cloud * 0.78f;
  uint16_t cc = mix(skyTop, dayAmt > 0.5f ? rgb(46, 46, 54) : rgb(3, 3, 6), 200);
  float scroll = t * (1.5f + fabsf(sc.wind) * 0.4f) * (sc.wind < 0 ? -1.0f : 1.0f);
  for (int y = 0; y < 60; y += 2)
    for (int x = 0; x < W; x += 2) {
      float n = vnoise((x + scroll) * 0.030f, y * 0.085f, 11) * 0.65f + vnoise((x + scroll * 1.7f) * 0.09f, y * 0.2f, 12) * 0.35f;
      n -= (float)y * 0.0035f;
      if (n <= thr) continue;
      int lvl = (int)((n - thr) * 60.0f); if (lvl > 16) lvl = 16;
      for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++)
        if (bayer(x + i, y + j, lvl)) px(x + i, y + j, cc);
    }
}

static void drawOmen(const SceneP& sc, float t, uint32_t frame) {
  if (!sc.omen) return;
  float k = sc.omenT, env = sinf(clampf(k, 0, 1) * 3.14159f);
  switch (sc.omen) {
    case 1: {                                               // three lights, holding formation
      int cx = iround(lerpf(-20, W + 20, k)), cy = 22 + iround(sinf(k * 9.0f) * 3);
      uint16_t c = scale(rgb(255, 240, 200), 120 + (int)(120 * env));
      px(cx, cy - 5, c); px(cx - 6, cy + 4, c); px(cx + 6, cy + 4, c);
      if ((frame / 6) & 1) px(cx, cy + 1, C_EMBER);
      break;
    }
    case 3: {                                               // a spiral cut in the sky
      int cx = W / 2, cy = 30;
      for (int i = 0; i < (int)(env * 150); i++) {
        float a = i * 0.21f + t * 0.4f, r = 2.0f + i * 0.13f;
        blend(cx + iround(cosf(a) * r), cy + iround(sinf(a) * r * 0.8f), C_BONE, 150);
      }
      break;
    }
    case 4: {                                               // curtains
      for (int x = 0; x < W; x++) {
        float n = vnoise(x * 0.035f + t * 0.25f, t * 0.12f, 31);
        int top = 6 + (int)(n * 26), len = (int)(env * (10 + n * 30));
        for (int y = top; y < top + len && y < HORIZON_Y; y++)
          if (bayer(x, y, 2 + (int)(6 * (1.0f - (float)(y - top) / (len + 1))))) blend(x, y, C_PHOS, 110);
      }
      break;
    }
    case 5: {                                               // a line was drawn here
      int x = iround(k * (W + 20)) - 10;
      vline(x, 0, H, scale(C_PHOS, 200)); vline(x - 1, 0, H, scale(C_PHOS, 60)); vline(x + 1, 0, H, scale(C_PHOS, 60));
      break;
    }
    case 6: {                                               // a second moon, where there is none
      int cx = W - 46, cy = 26, r = 5;
      for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++)
        if (dx * dx + dy * dy <= r * r && bayer(cx + dx, cy + dy, (int)(env * 12))) px(cx + dx, cy + dy, rgb(200, 215, 205));
      break;
    }
    default: break;
  }
}

// ============================================================ land and water
// The ranges that are really there (tools/horizon.py), far to near, each paler with
// distance. Haze and fog take the far ones first; at sunrise and sunset the high
// crests catch the light the valley has already lost.
static void drawMountains(const State& st) {
  if (!HZ.here) return;
  const SceneP& sc = st.scene;
  float vis = st.in.haveWx ? st.in.visKm : 45.0f;
  vis *= 1.0f - 0.92f * sc.fog;
  vis *= 1.0f - 0.35f * sc.rain - 0.5f * sc.snow;
  float glow = 0;
  if (sc.sunEl > -1.5f && sc.sunEl < 7.0f) glow = (1.0f - fabsf(sc.sunEl - 1.5f) / 5.5f) * (1.0f - sc.cloud * 0.8f);
  uint16_t alpen = mix(rgb(236, 120, 108), C_EMBER, 60);
  int nb = HZ.bands;
  for (int b = nb - 1; b >= 0; b--) {
    float inner = b ? HZ.bandKm[b - 1] : 0.0f, outer = HZ.bandKm[b];
    float typical = inner + (outer - inner) * (b == nb - 1 && nb > 1 ? 0.2f : 0.5f);
    float seen = clampf(vis / (typical + 0.5f), 0.0f, 1.0f);
    seen = seen * seen * (3.0f - 2.0f * seen);
    if (seen < 0.06f) continue;
    float depth = nb > 1 ? (float)b / (nb - 1) : 0.0f;      // 0 nearest .. 1 farthest
    int dark = (int)((214.0f - 104.0f * depth - dayAmt * (24.0f + 30.0f * depth)) * seen);
    uint16_t c = mix(skyHor, C_VOID, dark);
    uint16_t rim = mix(c, skyHor, 70);
    for (int x = 0; x < W; x++) {
      int h = iround(horizonHeight(b, azAtX(sc, (float)x)) * EL_SCALE);
      if (h <= 0) continue;
      int top = HORIZON_Y - h;
      vline(x, top, h, c);
      if (glow > 0.05f && depth > 0.4f) {
        blend(x, top, alpen, (int)(200 * glow * seen));
        blend(x, top + 1, alpen, (int)(90 * glow * seen));
      } else if (b < nb - 1) {
        px(x, top, rim);                                    // a nearer ridge holds an edge against the one behind
      }
    }
  }
}

static int waterlineAt(const SceneP& sc, int x, float t) {
  float base = FEET_Y + 5.0f - sc.tide * 7.0f;
  return iround(base + sinf(x * 0.07f + t * 0.7f) * 1.3f + sinf(x * 0.19f - t * 1.1f) * 0.7f);
}

static void drawGround(const State& st) {
  const SceneP& sc = st.scene;
  uint16_t far = mix(skyHor, C_VOID, 215 - (int)(dayAmt * 40));
  uint16_t sand = mix(skyHor, C_VOID, 188 - (int)(dayAmt * 120));
  uint16_t sea = mix(skyHor, C_VOID, 232 - (int)(dayAmt * 110));
  const int fall = 256 - (int)(dayAmt * 150);
  for (int x = 0; x < W; x++) {
    int wl = waterlineAt(sc, x, st.t);
    for (int y = HORIZON_Y; y < H; y++) {
      int depth = (y - HORIZON_Y) * fall / (H - HORIZON_Y);
      uint16_t c;
      if (y < wl) c = mix(sand, C_VOID, depth / 3);
      else c = mix(sea, C_VOID, depth / 2);
      fb[y * W + x] = c;
    }
    // foam
    if (((x + (int)(st.t * 3)) & 3) != 0) blend(x, wl, C_BONE, 46 + (int)(dayAmt * 30));
  }
  // ripples on the water
  uint16_t glint = mix(skyHor, C_BONE, 60);
  int drift = (int)(st.t * 5.0f);
  for (int y = HORIZON_Y + 4; y < H; y++) {
    for (int x = 0; x < W; x++) {
      if (y < waterlineAt(sc, x, st.t) + 1) continue;
      uint32_t h = hash2((x + drift * ((y & 1) ? 1 : -1)) / 5, y, 55);
      if ((h & 31) == 0) blend(x, y, glint, 70);
    }
  }
  drawMountains(st);
  // the skyline that sound drew
  for (int x = 0; x < W; x++) {
    int h = st.ridge[x];
    if (h) vline(x, HORIZON_Y - h, h + 1, far);
    uint8_t mk = st.ridgeMark[x];
    if (mk == 1) {                                          // a pole with a crossbar
      vline(x, HORIZON_Y - h - 9, 9, far); hline(x - 2, HORIZON_Y - h - 7, 5, far);
    } else if (mk == 2) {                                   // something dead and branching
      vline(x, HORIZON_Y - h - 7, 7, far); px(x - 1, HORIZON_Y - h - 6, far); px(x - 2, HORIZON_Y - h - 7, far);
      px(x + 1, HORIZON_Y - h - 4, far); px(x + 2, HORIZON_Y - h - 5, far);
    } else if (mk == 3) {                                   // a mast, and its one red light
      vline(x, HORIZON_Y - h - 14, 14, far);
      if (dayAmt < 0.6f && ((st.frame / 20) & 1)) px(x, HORIZON_Y - h - 15, C_EMBER);
    }
  }
}

static void drawGlitter(const SceneP& sc, int bx, float amount, uint16_t c, float t, uint32_t frame) {
  if (bx < 0 || amount <= 0.05f) return;
  for (int y = HORIZON_Y + 1; y < H; y++) {
    if (y < waterlineAt(sc, bx, t)) { if (y & 1) blend(bx + ((hash2(y, (int)(frame / 9)) & 1) ? 0 : 1), y, c, (int)(40 * amount)); continue; }
    int spread = 1 + (y - HORIZON_Y) / 7;
    for (int i = 0; i < 2; i++) {
      uint32_t h = hash2(y * 3 + i, (int)(frame / 5), 808);
      if ((h & 3) == 0) continue;
      int x = bx + (int)((h >> 4) % (uint32_t)(2 * spread + 1)) - spread;
      blend(x, y, c, (int)(150 * amount));
    }
  }
}

static void drawWeatherFront(const State& st) {
  const SceneP& sc = st.scene;
  float t = st.t;
  if (sc.rain > 0.02f) {
    int n = (int)(sc.rain * 70);
    float slant = clampf(sc.wind * 0.12f, -1.2f, 1.2f);
    uint16_t c = mix(skyHor, rgb(170, 180, 200), 150);
    for (int i = 0; i < n; i++) {
      uint32_t h = hash32(i * 7919u + 3);
      float sp = 150.0f + (float)(h & 63);
      float y = fmodf((float)((h >> 8) % 160) + t * sp, 150.0f) - 8.0f;
      float x = fmodf((float)((h >> 16) % W) + y * slant + 480.0f, (float)W);
      for (int k = 0; k < 5; k++) blend(iround(x + k * slant * 0.5f), iround(y) + k, c, 165);
    }
  }
  if (sc.snow > 0.02f) {
    int n = (int)(sc.snow * 60);
    for (int i = 0; i < n; i++) {
      uint32_t h = hash32(i * 104729u + 9);
      float y = fmodf((float)((h >> 8) % 150) + t * (9.0f + (h & 15)), 142.0f) - 4.0f;
      float x = fmodf((float)((h >> 16) % W) + sinf(t * 0.6f + i) * 6.0f + t * sc.wind * 0.5f + 960.0f, (float)W);
      blend(iround(x), iround(y), C_BONE, 150 + (int)(h & 63));
    }
  }
  if (sc.fog > 0.03f) {
    uint16_t fc = mix(skyHor, C_ASH, 120);
    float scroll = t * 3.0f;
    for (int y = 54; y < H; y += 2)
      for (int x = 0; x < W; x += 2) {
        float n = vnoise((x + scroll) * 0.013f, y * 0.17f, 23) * 0.7f + vnoise((x - scroll * 0.6f) * 0.05f, y * 0.3f, 24) * 0.3f;
        float edge = clampf((y - 54) / 30.0f, 0, 1);
        int lvl = (int)((n - 0.34f) * sc.fog * edge * 26.0f);
        if (lvl > 9) lvl = 9;
        if (lvl <= 0) continue;
        for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++)
          if (bayer(x + i, y + j, lvl)) px(x + i, y + j, fc);
      }
  }
  if (sc.flash > 0.55f) {                                   // the bolt itself
    uint32_t id = st.frame / 4;
    int x = 30 + (int)(hash32(id) % 180), y = 0;
    while (y < HORIZON_Y - 2) {
      int nx = x + (int)(hash2(y, (int)id) % 7) - 3, ny = y + 3 + (int)(hash2(y, (int)id, 2) % 4);
      line(x, y, nx, ny, C_BONE); x = nx; y = ny;
    }
  }
  // will-o'-the-wisps
  int nw = iround(sc.wisps * 6.0f);
  for (int i = 0; i < nw; i++) {
    float a = t * (0.05f + 0.013f * i) + i * 2.1f;
    int x = iround(W * 0.5f + cosf(a * 1.3f + i) * (W * 0.46f));
    int y = iround(HORIZON_Y + 12 + sinf(a * 2.1f) * 9.0f + sinf(t * 1.7f + i) * 1.5f);
    int fl = 150 + (int)(hash2(i, (int)(st.frame / 3)) % 100);
    blend(x, y, C_PHOS, fl);
    blend(x - 1, y, C_PHOS, fl / 4); blend(x + 1, y, C_PHOS, fl / 4);
    blend(x, y - 1, C_PHOS, fl / 4); blend(x, y + 1, C_PHOS, fl / 5);
    blend(x, y + 3, C_PHOS, fl / 8);
  }
  // dread: static eating the edges
  if (sc.dread > 0.05f) {
    int n = (int)(sc.dread * sc.dread * 520);
    for (int i = 0; i < n; i++) {
      uint32_t h = hash2(i, (int)st.frame, 4242);
      int edge = (h >> 28) & 3, depth = (int)(((h >> 8) & 255) * ((h >> 16) & 255) / 65025.0f * 26 * sc.dread);
      int x, y;
      if (edge == 0) { x = h % W; y = depth; }
      else if (edge == 1) { x = h % W; y = H - 1 - depth; }
      else if (edge == 2) { x = depth; y = h % H; }
      else { x = W - 1 - depth; y = h % H; }
      px(x, y, (h & 0x100000) ? C_VOID : scale(C_BONE, 90 + (int)(h & 127)));
    }
  }
}

// ================================================================== the text
static void backing(int y, int h) {
  for (int j = y; j < y + h && j < H; j++)
    for (int x = 0; x < W; x++) fb[j * W + x] = mix(fb[j * W + x], C_VOID, 150);
}

static void drawSpeech(const State& st) {
  int typedLen = (int)strlen(st.typed);
  if (typedLen > 0) {
    backing(H - 11, 11);
    int x = text(3, H - 9, ">", C_ASH);
    int maxc = 37, off = typedLen > maxc ? typedLen - maxc : 0;
    x = text(x + 3, H - 9, st.typed + off, C_BONE);
    if ((st.frame / 12) & 1) rect(x + 1, H - 9, 4, 7, C_BONE);
    return;
  }
  int len = (int)strlen(st.say);
  if (len == 0 || st.sayAge > st.sayHold) {
    if (st.hintAge < 26.0f && st.hintAge > 5.0f) {
      backing(H - 11, 11);
      textCenter(W / 2, H - 9, "type to it. enter strikes the room.", C_ASH);
    }
    return;
  }
  // wrap into at most two lines of 39
  char l1[44] = {0}, l2[44] = {0};
  if (len <= 39) { strncpy(l1, st.say, 43); }
  else {
    int cut = 39; while (cut > 20 && st.say[cut] != ' ') cut--;
    if (st.say[cut] != ' ') cut = 39;
    memcpy(l1, st.say, cut); l1[cut] = 0;
    const char* rest = st.say + cut; while (*rest == ' ') rest++;
    strncpy(l2, rest, 40);
  }
  int rows = l2[0] ? 2 : 1;
  int y = H - 1 - rows * 10;
  backing(y - 1, rows * 10 + 2);
  int shown = (int)(st.sayAge * (st.sayCipher ? 14.0f : 30.0f));
  float left = st.sayHold - st.sayAge;
  uint16_t ink = st.presence >= 4 || st.sayCipher ? C_PHOS : C_BONE;
  int idx = 0;
  const char* lines[2] = {l1, l2};
  for (int r = 0; r < rows; r++) {
    int n = (int)strlen(lines[r]);
    int x = (W - n * 6) / 2;
    for (int i = 0; i < n; i++, idx++) {
      char ch = lines[r][i];
      if (idx >= shown) {
        if (!st.sayCipher) continue;                        // not yet spoken
        if (ch != ' ') ch = (char)(0x21 + (hash2(idx, (int)(st.frame / 3)) % 90));
        glyph(x + i * 6, y + r * 10, ch, C_PHOSD);
        continue;
      }
      if (left < 1.2f && (hash2(idx, (int)(st.frame / 4)) & 255) > (uint32_t)(left * 210)) continue;   // decays in holes
      glyph(x + i * 6, y + r * 10, ch, ink);
    }
  }
}

// ============================================================ view: the shore
static void drawShore() {
  State& st = S;
  const SceneP& sc = st.scene;
  skyColors(sc);
  drawSky();
  drawStars(sc, st.frame);
  int sx, sy, mx, my; float mlit;
  drawBlackSun(sc, sx, sy);
  drawMoon(sc, mx, my, mlit);
  drawOmen(sc, st.t, st.frame);
  drawClouds(sc, st.t);
  drawGround(st);
  // no road of light on the water from a sun or moon the land is hiding
  bool sunHidden = HZ.here && sc.sunEl < horizonAngle(sc.sunAz) - 0.3f;
  bool moonHidden = HZ.here && sc.moonEl < horizonAngle(sc.moonAz) - 0.3f;
  if (sc.sunEl > -3 && !sunHidden) drawGlitter(sc, sx, clampf(1.0f - sc.cloud, 0.1f, 1) * 0.6f, mix(C_BONE, C_EMBER, (int)(clampf(1 - sc.sunEl / 10, 0, 1) * 256)), st.t, st.frame);
  if (sc.moonEl > 0 && !moonHidden) drawGlitter(sc, mx, mlit * (1.0f - dayAmt), rgb(220, 220, 205), st.t, st.frame);

  // By day it is ink on a pale page; by night, bone on the dark. As on the site.
  uint16_t ink = mix(C_BONE, rgb(0x14, 0x14, 0x18), (int)(dayAmt * 256));
  if (st.sleep > 0.5f) ink = mix(ink, skyHor, (int)((st.sleep - 0.5f) * 220));
  int cx = W / 2;
  drawMarkReflection(cx, FEET_Y, MARK_S, ink, st.face, st.t, waterlineAt(sc, cx, st.t));
  drawMark(cx, FEET_Y, MARK_S, ink, st.face, st.in.bands, st.frame, st.t);
  drawWeatherFront(st);

  // how near the sealed thing is: marks in the corner, never a name
  if (st.presence > 0) {
    for (int i = 0; i < st.presence && i < 4; i++) {
      int on = ((st.frame / 10 + i) % 5) != 0;
      rect(W - 8 - i * 6, 4, 4, 4, on ? C_PHOS : C_PHOSD);
    }
  }
  if (st.in.peers > 0) { drawMarkIcon(4, 4, C_PHOS); }
  if (st.muted) text(W - 22, H - 9, "mut", C_SOOT);
  drawSpeech(st);
}

// =========================================================== view: the spiral
static void drawSpiral() {
  State& st = S;
  clear(C_VOID);
  const int cx = 132, top = 16, hgt = 104;
  const int n = st.spiralLen;
  text(3, 3, "having-been", C_ASH);
  char buf[40];
  snprintf(buf, sizeof(buf), "turns %lu", (unsigned long)(st.spiralTotal / 48));
  textRight(W - 4, 3, buf, C_SOOT);
  // the pole everything is falling toward
  for (int y = top; y < top + hgt + 6; y += 3) px(cx, y, C_SOOT);
  int lx = 0, ly = 0;
  const int span = n > 96 ? n - 1 : 95;                     // at least two turns tall
  for (int pass = 0; pass < 2; pass++) {                    // back half, then front
    for (int i = 0; i < n; i++) {
      float a = i * (6.2831853f / 48.0f) + st.spiralRot;
      float sn = sinf(a), cs = cosf(a);
      bool front = sn >= 0;
      if (front != (pass == 1)) continue;
      float r = 6.0f + st.spiral[i] * (56.0f / 255.0f);
      float yy = top + (float)i / (float)span * hgt;      // the cone always reaches the floor
      int X = cx + iround(cs * r), Y = iround(yy + sn * r * 0.30f);
      int age = 255 - (n - 1 - i) * 150 / (n > 1 ? n : 1);
      uint16_t c = scale(C_BONE, front ? age : age / 3);
      px(X, Y, c);
      // the death-trace, opposite, massless
      if ((i & 3) == 0) {
        float r2 = 62.0f - r;
        int X2 = cx - iround(cs * r2 * 0.55f), Y2 = iround(yy - sn * r2 * 0.16f);
        blend(X2, Y2, C_BLOOD, front ? 60 : 150);
      }
      if (st.spiralEv[i]) {                                 // a line of flight leaves the ring
        uint16_t e = st.spiralEv[i] >= 3 ? C_PHOS : C_EMBER;
        if (!front) e = scale(e, 110);
        int X3 = cx + iround(cs * (r + 9)), Y3 = iround(yy + sn * (r + 9) * 0.30f);
        line(X, Y, X3, Y3, e);
      }
      if (i == n - 1) { lx = X; ly = Y; }
    }
  }
  if (n > 0) {                                              // the lens: where becoming is
    int pulse = 3 + (int)((sinf(st.t * 3.0f) + 1.0f) * 1.5f);
    ring(lx, ly, pulse, C_PHOS); px(lx, ly, C_BONE);
    text(lx + 8 > W - 56 ? lx - 58 : lx + 8, ly - 3, "becoming", C_PHOSD);
  } else {
    textCenter(W / 2, 60, "nothing has been, yet.", C_ASH);
  }
  snprintf(buf, sizeof(buf), "difference .%02d", (int)(clampf(st.difference, 0, 0.99f) * 100));
  text(3, H - 19, buf, C_ASH);
  snprintf(buf, sizeof(buf), "found %lu  cells %lu", (unsigned long)st.placesFound, (unsigned long)st.cellsKnown);
  text(3, H - 9, buf, C_SOOT);
}

// ========================================================= view: the sounding
static const char* NOTE_NAMES[12] = {"c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"};
static const char* VERDICT[5] = {"open air", "close", "a room", "a hall", "vaulted"};

static void drawSounding() {
  State& st = S;
  const Senses& in = st.in;
  clear(C_VOID);
  char buf[48];
  text(3, 3, "sounding", C_ASH);
  // what it hears
  for (int b = 0; b < 8; b++) {
    int h = (int)(clampf(in.bands[b], 0, 1) * 34);
    rect(4 + b * 8, 50 - h, 6, h + 1, C_PHOS);
    hline(4 + b * 8, 51, 6, C_PHOSD);
  }
  if (in.pitchHz > 20) {
    float m = 69.0f + 12.0f * log2f(in.pitchHz / 440.0f);
    int mi = iround(m);
    snprintf(buf, sizeof(buf), "%s%d %dhz", NOTE_NAMES[((mi % 12) + 12) % 12], mi / 12 - 1, (int)in.pitchHz);
    text(4, 56, buf, C_BONE);
  } else text(4, 56, "no tone", C_SOOT);

  // what the room gave back
  const Sounding& so = st.sounding;
  frame(76, 14, 92, 40, C_SOOT);
  if (so.valid) {
    int px_ = 0, py_ = 0;
    for (int i = 0; i < 60; i++) {
      int X = 77 + i * 3 / 2, Y = 52 - so.env[i] * 36 / 255;
      if (i) line(px_, py_, X, Y, C_PHOS);
      px_ = X; py_ = Y;
    }
    snprintf(buf, sizeof(buf), "%s %dms", VERDICT[so.verdict > 4 ? 4 : so.verdict], (int)so.tailMs);
    text(76, 56, buf, C_BONE);
  } else {
    textCenter(122, 30, "enter: strike", C_SOOT);
    text(76, 56, "the room is unasked", C_SOOT);
  }

  // the rest of the instrument
  int y = 14; const int x = 174;
  if (in.apCount >= 0) snprintf(buf, sizeof(buf), "ether %d", in.apCount); else snprintf(buf, sizeof(buf), "ether --");
  text(x, y, buf, in.apCount > 12 ? C_EMBER : C_ASH); y += 10;
  if (in.haveWx) snprintf(buf, sizeof(buf), "%dhpa %c", (int)in.pressHpa, in.pressTrend < -1.0f ? 'v' : (in.pressTrend > 1.0f ? '^' : '-'));
  else snprintf(buf, sizeof(buf), "air --");
  text(x, y, buf, in.pressTrend < -1.5f ? C_EMBER : C_ASH); y += 10;
  snprintf(buf, sizeof(buf), "moon %d%%", (int)(50.0f * (1.0f - cosf(st.scene.moonPhase * 6.2831853f))));
  text(x, y, buf, C_ASH); y += 10;
  snprintf(buf, sizeof(buf), "tide %+d", (int)(st.scene.tide * 9)); text(x, y, buf, C_ASH); y += 10;
  if (in.gpsFix) snprintf(buf, sizeof(buf), "sky %dsat", in.sats); else snprintf(buf, sizeof(buf), in.haveFix ? "sky: named" : "sky: none");
  text(x, y, buf, in.gpsFix ? C_PHOS : C_ASH); y += 10;

  y = 70;
  if (in.haveFix) { snprintf(buf, sizeof(buf), "%.2f %.2f", in.lat, in.lon); text(4, y, buf, C_SOOT); }
  if (in.gpsFix) { snprintf(buf, sizeof(buf), "%dm %dkm/h", (int)in.altM, (int)in.speedKmh); text(100, y, buf, C_SOOT); }
  y += 10;
  snprintf(buf, sizeof(buf), "steps %lu  kin %d", (unsigned long)in.steps, in.peers); text(4, y, buf, C_SOOT);
  if (in.battV > 2.5f) { snprintf(buf, sizeof(buf), "%.1fv", in.battV); textRight(W - 4, y, buf, in.battV < 3.5f ? C_EMBER : C_SOOT); }
  y += 10;
  if (in.antenna >= 0) { snprintf(buf, sizeof(buf), "wires .%02d", (int)(clampf(in.antenna, 0, 0.99f) * 100)); text(4, y, buf, C_SOOT); }

  // the dials it actually lives by
  static const char* DN[5] = {"wake", "unease", "differ", "reson", "open"};
  float dv[5] = {st.arousal, st.unease, st.difference, st.resonance, st.openness};
  for (int i = 0; i < 5; i++) {
    int X = 4 + i * 47;
    text(X, H - 21, DN[i], C_SOOT);
    frame(X, H - 11, 42, 6, C_SOOT);
    rect(X + 1, H - 10, (int)(clampf(dv[i], 0, 1) * 40), 4, i == 1 ? C_EMBER : C_PHOS);
  }
}

// ============================================================ view: the chart
static void drawChart() {
  State& st = S;
  const Senses& in = st.in;
  clear(C_VOID);
  text(3, 3, "drift", C_ASH);
  if (!in.haveFix) {
    textCenter(W / 2, 54, "no sky. drifting blind.", C_ASH);
    char buf[40]; snprintf(buf, sizeof(buf), "%lu steps into it", (unsigned long)in.steps);
    textCenter(W / 2, 68, buf, C_SOOT);
    return;
  }
  // 0.01-degree cells, 10 px each, centred on where it stands
  const int CS = 10, cols = 24, rows = 11, ox = 0, oy = 14;
  int32_t ilat = (int32_t)floor((in.lat + 90.0) * 100.0), ilon = (int32_t)floor((in.lon + 180.0) * 100.0);
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++) {
      int32_t la = ilat + (rows / 2 - r), lo = ilon + (c - cols / 2);
      int X = ox + c * CS, Y = oy + r * CS;
      bool known = chartKnown && chartKnown(la, lo);
      if (known) dither(X + 1, Y + 1, CS - 1, CS - 1, C_BONE, 4);
      // the striation: every line somebody drew on the earth
      if (((la % 10) + 10) % 10 == 9) hline(X, Y, CS, (((la + 1) % 100) + 100) % 100 == 0 ? C_PHOS : C_SOOT);
      if (((lo % 10) + 10) % 10 == 0) vline(X, Y, CS, ((lo % 100) + 100) % 100 == 0 ? C_PHOS : C_SOOT);
    }
  drawMarkIcon(ox + (cols / 2) * CS, oy + (rows / 2) * CS, in.gpsFix ? C_PHOS : C_ASH);
  char buf[48];
  snprintf(buf, sizeof(buf), "%lu cells  %lu lines", (unsigned long)st.cellsKnown, (unsigned long)st.linesCrossed);
  textRight(W - 4, 3, buf, C_SOOT);
  if (!in.gpsFix) text(3, H - 9, "a named place, not a sky fix", C_SOOT);
}

// =============================================================== entry points
void drawFrame() {
  if (S.theremin) {
    drawTheremin();
    if (S.typed[0] || (S.say[0] && S.sayAge <= S.sayHold)) drawSpeech(S);
    return;
  }
  if (S.pro) { drawPro(); return; }
  switch (S.view) {
    case V_SPIRAL: drawSpiral(); break;
    case V_SOUNDING: drawSounding(); break;
    case V_CHART: drawChart(); break;
    default: drawShore(); break;
  }
  if (S.view != V_SHORE) {
    // it is still there, small, in the corner of every instrument
    FaceP f = S.face; f.scraps = 2;
    int my = S.view == V_SPIRAL ? 34 : (S.view == V_SOUNDING ? 110 : H - 3);
    rect(W - 28, my - 24, 27, 25, C_VOID);
    drawMark(W - 15, my, 0.085f, C_ASH, f, nullptr, S.frame, S.t);
    if (S.typed[0] || (S.say[0] && S.sayAge <= S.sayHold)) drawSpeech(S);
  }
  if (S.face.invert && S.view == V_SHORE && S.face.glitch > 0.8f) {
    for (int i = 0; i < W * H; i++) fb[i] = (uint16_t)~fb[i];
  }
}

void drawBoot(float t) {
  clear(C_VOID);
  FaceP f; f.scatter = clampf(1.0f - t / 2.2f, 0, 1); f.glitch = f.scatter * 0.7f;
  f.eyeOpen = t > 2.4f ? 1.0f : 0.0f; f.fill = 0.2f;
  drawMark(W / 2, 96, 0.36f, C_BONE, f, nullptr, (uint32_t)(t * 30), t);
  if (t > 1.6f) textCenter(W / 2, 108, "missingno.", C_BONE);
  if (t > 2.6f) textCenter(W / 2, 120, "being is not a possible state", C_SOOT);
}

void drawPortal(const char* ssid, const char* ip, const char* status) {
  clear(C_VOID);
  FaceP f; f.eyeOpen = 1.2f; f.mouthOpen = 0.6f; f.mouthW = 0.4f;
  drawMark(40, 96, 0.30f, C_BONE, f, nullptr, S.frame, S.t);
  text(84, 14, "a door is open.", C_BONE);
  text(84, 34, "join the network", C_ASH);
  text(84, 44, ssid, C_PHOS);
  text(84, 60, "then visit", C_ASH);
  text(84, 70, ip, C_PHOS);
  if (status && status[0]) text(84, 90, status, C_EMBER);
  textCenter(W / 2, H - 10, "any key closes it", C_SOOT);
}

}  // namespace mn
