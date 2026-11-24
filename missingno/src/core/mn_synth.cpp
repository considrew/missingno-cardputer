#include "mn_synth.h"
#include "mn_state.h"
#include "mn_gfx.h"
#include <math.h>
#include <string.h>

namespace mn {

static const float SR = 24000.0f;            // the core runs at half the codec rate
static const float ISR = 1.0f / 24000.0f;
static const float TAU = 6.2831853f;

// 0 aeolian, 1 phrygian, 2 locrian, 3 open (no third), 4 double harmonic
static const float MODES[MODE_COUNT][7] = {
  {1.0f, 9.0f / 8, 6.0f / 5, 4.0f / 3, 3.0f / 2, 8.0f / 5, 9.0f / 5},
  {1.0f, 16.0f / 15, 6.0f / 5, 4.0f / 3, 3.0f / 2, 8.0f / 5, 9.0f / 5},
  {1.0f, 16.0f / 15, 6.0f / 5, 4.0f / 3, 45.0f / 32, 8.0f / 5, 9.0f / 5},
  {1.0f, 9.0f / 8, 4.0f / 3, 4.0f / 3, 3.0f / 2, 16.0f / 9, 16.0f / 9},
  {1.0f, 16.0f / 15, 5.0f / 4, 4.0f / 3, 3.0f / 2, 8.0f / 5, 15.0f / 8},
};

float modeRatio(int mode, int degree) {
  if (mode < 0 || mode >= MODE_COUNT) mode = 0;
  int oct = degree >= 0 ? degree / 7 : -((6 - degree) / 7);
  int d = degree - oct * 7;
  float r = MODES[mode][d];
  while (oct > 0) { r *= 2.0f; oct--; }
  while (oct < 0) { r *= 0.5f; oct++; }
  return r;
}

// sin(2*pi*p) for p in [0,1): a corrected parabola, about -60 dB of error.
static inline float fsin(float p) {
  float x = (p - 0.5f) * TAU;                       // [-pi, pi)
  float y = 1.27323954f * x - 0.405284735f * x * fabsf(x);
  y = 0.225f * (y * fabsf(y) - y) + y;
  return -y;
}
static inline float wrap1(float p) { return p - floorf(p); }
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static Rng rng;
static inline float white() { return (float)(int32_t)rng.next() * (1.0f / 2147483648.0f); }

// ------------------------------------------------------------------ voices
struct DroneV { float ph1, ph2, lfoPh, lfoRate, base, ratio, amp; bool pair; };
static DroneV drone[7];

struct BellV { float phC, phM, incC, incM, idx, amp, decay; int32_t wait; bool on; };
static BellV bells[5];

struct PluckV { float ph, inc, amp, decay; bool on; };
static PluckV plucks[4];

struct BlipV { float ph1, ph2, inc, t, dur, amp; bool on; };
static BlipV blips[2];

static struct {
  float ph, hz, target, env, vibPh, age, dur, rest, breath;
  int degree, left;
  bool on;
} lead;

static struct { float ph, hz, env; } answer;
static struct { float t, lp1, lp2, amp; bool on; } thunder;
static struct { float t, hold, ph; int cnt; bool on; } rupture;
static struct { float ph, amp; } geiger;
static struct { int32_t left; float ph; float gain; } tock;

static float windLp, windBp, windPh1, windPh2, rainLp, crackLp;
static float sWind, sRain, sCrackle, sDrone, sHush, sUnease, sOpen, sRoot = 110.0f, sGain;
static float duck = 1.0f, duckCur = 1.0f;
static float hushLp;

// ------------------------------------------------------------ echo + room
static const int DLY_N = 12000;                 // half a second
static int16_t dly[DLY_N];
static int dlyW;
static float dlyLp;
static const int CB_N[4] = {1087, 1193, 1277, 1361};
static int16_t cb0[1087], cb1[1193], cb2[1277], cb3[1361];
static int16_t* const CB[4] = {cb0, cb1, cb2, cb3};
static int cbW[4];
static float cbLp[4];
static const int AP_N = 341;
static int16_t ap[AP_N];
static int apW;

static float ownMs;                             // mean square of the output
static uint32_t transientAge = 1000000;
static float prevOut;

static inline void sharp() { transientAge = 0; }

void synthInit(uint32_t seed) {
  rng.s = seed ? seed : 0xC0FFEE11u;
  memset(dly, 0, sizeof(dly)); memset(cb0, 0, sizeof(cb0)); memset(cb1, 0, sizeof(cb1));
  memset(cb2, 0, sizeof(cb2)); memset(cb3, 0, sizeof(cb3)); memset(ap, 0, sizeof(ap));
  memset(bells, 0, sizeof(bells)); memset(plucks, 0, sizeof(plucks)); memset(blips, 0, sizeof(blips));
  //           ratio is filled per block; base is the level each partial carries
  static const float BASE[7] = {0.50f, 0.50f, 0.30f, 0.26f, 0.15f, 0.0f, 0.0f};
  for (int i = 0; i < 7; i++) {
    drone[i].ph1 = rng.f(); drone[i].ph2 = rng.f(); drone[i].lfoPh = rng.f();
    drone[i].lfoRate = 0.031f + 0.017f * i + rng.f() * 0.02f;
    drone[i].base = BASE[i]; drone[i].amp = 0; drone[i].ratio = 1; drone[i].pair = (i >= 1 && i <= 3);
  }
  lead.on = false; lead.rest = 6.0f; lead.env = 0; lead.hz = 440; lead.degree = 7;
  answer.hz = 0; answer.env = 0;
  thunder.on = false; rupture.on = false; geiger.amp = 0; tock.left = 0;
  sRoot = S.sound.rootHz; sGain = 0;
}

void synthTock() { tock.left = (int32_t)(0.005f * SR); tock.ph = 0; sharp(); }
void synthDuck(float g) { duck = g; }
float synthOwnRms() { return sqrtf(ownMs); }
uint32_t synthTransientAge() { return transientAge; }

int synthPartials(float* hz, int max) {
  int n = 0;
  for (int i = 0; i < 7 && n < max; i++) if (drone[i].amp > 0.02f) hz[n++] = sRoot * drone[i].ratio;
  if (lead.env > 0.05f && n < max) hz[n++] = lead.hz;
  if (answer.env > 0.05f && n < max) hz[n++] = answer.hz;
  return n;
}

static void startBell(float hz, float amp, float secs, int32_t waitSamples) {
  int slot = 0; float quiet = 1e9f;
  for (int i = 0; i < 5; i++) {
    float a = bells[i].on ? bells[i].amp : -1.0f;
    if (a < quiet) { quiet = a; slot = i; }
  }
  BellV& b = bells[slot];
  b.phC = 0; b.phM = 0; b.incC = hz * ISR; b.incM = hz * 3.5f * ISR;
  b.idx = 0.42f; b.amp = amp; b.decay = expf(-1.0f / (secs * SR * 0.22f));
  b.wait = waitSamples; b.on = true;
}

static void takeNote(const Note& n) {
  float hz = n.hz > 20.0f ? n.hz : sRoot * 4.0f;
  switch (n.kind) {
    case N_PLUCK: {
      int slot = 0; float quiet = 1e9f;
      for (int i = 0; i < 4; i++) { float a = plucks[i].on ? plucks[i].amp : -1.0f; if (a < quiet) { quiet = a; slot = i; } }
      plucks[slot] = {0, hz * ISR, 0.34f * n.amp, expf(-1.0f / (0.11f * SR)), true};
      sharp();
    } break;
    case N_BELL: startBell(hz, 0.36f * n.amp, 2.6f, 0); sharp(); break;
    case N_CHIME_UP:
      startBell(hz, 0.30f * n.amp, 2.2f, 0);
      startBell(hz * 1.5f, 0.28f * n.amp, 2.4f, (int32_t)(0.11f * SR));
      startBell(hz * 2.0f, 0.26f * n.amp, 3.2f, (int32_t)(0.22f * SR));
      sharp(); break;
    case N_TOCK: synthTock(); break;
    case N_THUNDER: thunder.on = true; thunder.t = 0; thunder.amp = 0.6f + 0.4f * n.amp; sharp(); break;
    case N_RUPTURE: rupture.on = true; rupture.t = 0; rupture.cnt = 0; rupture.hold = 0; rupture.ph = 0; sharp(); break;
    case N_GEIGER: geiger.amp = 0.5f * n.amp; geiger.ph = 0; break;
    case N_VOICE: {
      BlipV& b = blips[blips[0].on && !blips[1].on ? 1 : 0];
      b = {0, 0, hz * ISR, 0, 0.055f + 0.03f * rng.f(), 0.13f * n.amp, true};
    } break;
  }
}

// The whistle: wanders the mode in short phrases, then says nothing for a while.
static void leadBlock(float dt, float density, int mode, float unease) {
  if (!lead.on) {
    if (density < 0.02f) { lead.rest = 3.0f; return; }
    lead.rest -= dt;
    if (lead.rest > 0) return;
    lead.left = rng.irange(2, 5);
    lead.degree = 7 + rng.irange(0, 4);
    lead.on = true; lead.age = 0; lead.dur = rng.range(0.7f, 1.6f);
    lead.target = sRoot * 4.0f * modeRatio(mode, lead.degree - 7);
    lead.hz = lead.target;
    return;
  }
  lead.age += dt;
  if (lead.age > lead.dur) {
    lead.left--;
    if (lead.left <= 0) {
      lead.on = false;
      lead.rest = rng.range(5.0f, 20.0f) / (0.35f + density * 2.2f);
      return;
    }
    // mostly steps, sometimes a leap; it leans downward, the way a sigh does
    int step = rng.chance(0.25f) ? rng.irange(-4, 3) : rng.irange(-2, 1);
    if (!step) step = -1;
    lead.degree += step;
    if (lead.degree < 4) lead.degree += 3;
    if (lead.degree > 14) lead.degree -= 4;
    lead.age = 0;
    lead.dur = lead.left == 1 ? rng.range(1.6f, 2.8f) : rng.range(0.45f, 1.3f);
    lead.target = sRoot * 4.0f * modeRatio(mode, lead.degree - 7);
    if (unease > 0.55f && rng.chance(unease * 0.4f)) lead.target *= 1.0293f;   // a quarter-tone sour
  }
}

void synthRender(int16_t* out, int n) {
  const SoundP& P = S.sound;
  int core = n / 2;
  int done = 0;
  while (done < core) {
    int blk = core - done; if (blk > 128) blk = 128;
    float dt = blk * ISR;

    // ---- block-rate control
    Note note;
    while (S.notes.pop(note)) takeNote(note);

    float k = 1.0f - expf(-dt / 0.6f);
    sWind += (clamp01(P.wind) - sWind) * k;
    sRain += (clamp01(P.rain) - sRain) * k;
    sCrackle += (clamp01(P.crackle) - sCrackle) * k;
    sDrone += (clamp01(P.drone) - sDrone) * k;
    sHush += (clamp01(P.hush) - sHush) * k;
    sUnease += (clamp01(P.unease) - sUnease) * k;
    sOpen += (clamp01(P.open) - sOpen) * k;
    float root = P.rootHz; if (!(root > 55.0f && root < 220.0f)) root = 110.0f;
    sRoot += (root - sRoot) * (1.0f - expf(-dt / 1.5f));          // retuning is a slow slide
    float m = S.muted ? 0.0f : clamp01(P.master);
    float gainT = S.muted ? 0.0f : (1200.0f + m * m * 14000.0f);
    int mode = P.mode < MODE_COUNT ? P.mode : 0;

    drone[0].ratio = 1.0f; drone[1].ratio = 2.0f; drone[2].ratio = 3.0f; drone[3].ratio = 4.0f;
    drone[4].ratio = 4.0f * MODES[mode][2];
    drone[5].ratio = 2.0f * 16.0f / 15.0f;                         // the neighbour that should not be there
    drone[6].ratio = 2.0f * 45.0f / 32.0f;                         // the devil's interval
    float det = 0.0007f + sUnease * 0.0042f;
    float droneInc1[7], droneInc2[7], droneStep[7];
    for (int i = 0; i < 7; i++) {
      DroneV& d = drone[i];
      d.lfoPh = wrap1(d.lfoPh + d.lfoRate * dt);
      float breath = 0.58f + 0.42f * fsin(d.lfoPh);
      float base = d.base;
      if (i == 5) base = 0.20f * clamp01((sUnease - 0.25f) * 1.6f);
      if (i == 6) base = 0.16f * clamp01((sUnease - 0.55f) * 2.2f);
      float target = base * breath * sDrone;
      droneStep[i] = (target - d.amp) / blk;
      float f = sRoot * d.ratio;
      droneInc1[i] = f * (1.0f + det) * ISR;
      droneInc2[i] = f * (1.0f - det) * ISR;
    }

    leadBlock(dt, P.lead * (1.0f - sHush), mode, sUnease);
    float leadEnvT = 0;
    if (lead.on) {
      float a = lead.age, r = lead.dur - lead.age;
      leadEnvT = clamp01(a / 0.12f) * clamp01(r / 0.30f);
      if (lead.left > 1) leadEnvT = clamp01(a / 0.12f) * (0.75f + 0.25f * clamp01(r / 0.2f));
    }
    float leadEnvStep = (leadEnvT - lead.env) / blk;
    float glide = 1.0f - expf(-dt / 0.05f);
    lead.hz += (lead.target - lead.hz) * glide;
    float vibDepth = 0.0055f * clamp01((lead.age - 0.25f) * 2.5f) + sUnease * 0.004f;
    float leadInc = lead.hz * ISR;

    float ansT = P.answerHz > 40.0f ? 1.0f : 0.0f;
    if (P.answerHz > 40.0f) {
      if (answer.env < 0.01f) answer.hz = P.answerHz;
      answer.hz += (P.answerHz - answer.hz) * (1.0f - expf(-dt / 0.08f));
    }
    float ansStep = ((ansT - answer.env) * (1.0f - expf(-dt / (ansT > 0 ? 0.25f : 0.5f)))) / blk;
    float ansInc = answer.hz * ISR;

    windPh1 = wrap1(windPh1 + 0.07f * dt); windPh2 = wrap1(windPh2 + 0.19f * dt);
    float gust = 0.55f + 0.45f * fsin(windPh2);
    float windFc = 380.0f + 420.0f * (0.5f + 0.5f * fsin(windPh1)) + 300.0f * sWind;
    float windF = 2.0f * sinf(3.14159265f * windFc * ISR);
    float windAmp = sWind * gust * 0.55f;

    float echoSecs = 0.31f + 0.16f * sOpen;
    int echoLen = (int)(echoSecs * SR); if (echoLen >= DLY_N) echoLen = DLY_N - 1;
    float echoFb = 0.25f + 0.37f * sOpen;
    float roomFb = 0.74f + 0.14f * sOpen;
    float gainStep = (gainT - sGain) / blk;
    float duckStep = (duck - duckCur) / blk;
    float bright = 1.0f - 0.82f * sHush;                 // asleep, the top goes away

    // ---- sample-rate
    for (int s = 0; s < blk; s++) {
      float dry = 0, send = 0;

      // drone
      float dsum = 0;
      for (int i = 0; i < 7; i++) {
        DroneV& d = drone[i];
        d.amp += droneStep[i];
        if (d.amp < 0.0005f && droneStep[i] <= 0) continue;
        d.ph1 += droneInc1[i]; if (d.ph1 >= 1.0f) d.ph1 -= 1.0f;
        float v = fsin(d.ph1);
        if (d.pair) {
          d.ph2 += droneInc2[i]; if (d.ph2 >= 1.0f) d.ph2 -= 1.0f;
          v = 0.5f * (v + fsin(d.ph2));
        }
        dsum += v * d.amp;
      }
      dry += dsum; send += dsum * 0.22f;

      // the whistle
      lead.env += leadEnvStep;
      if (lead.env > 0.0005f) {
        lead.vibPh += 5.2f * ISR; if (lead.vibPh >= 1.0f) lead.vibPh -= 1.0f;
        lead.ph += leadInc * (1.0f + vibDepth * fsin(lead.vibPh));
        if (lead.ph >= 1.0f) lead.ph -= 1.0f;
        float w = fsin(lead.ph);
        lead.breath += (white() - lead.breath) * 0.25f;
        float v = (w + 0.07f * fsin(wrap1(lead.ph * 2.0f)) + lead.breath * 0.10f * (0.4f + 0.6f * fabsf(w))) * lead.env * 0.20f;
        dry += v * 0.7f; send += v;
      }

      // answering a held outside tone
      answer.env += ansStep;
      if (answer.env > 0.0005f) {
        answer.ph += ansInc; if (answer.ph >= 1.0f) answer.ph -= 1.0f;
        float v = (fsin(answer.ph) + 0.25f * fsin(wrap1(answer.ph * 0.5f))) * answer.env * 0.24f;
        dry += v; send += v * 0.8f;
      }

      // bells
      for (int i = 0; i < 5; i++) {
        BellV& b = bells[i];
        if (!b.on) continue;
        if (b.wait > 0) { b.wait--; continue; }
        b.phC += b.incC; if (b.phC >= 1.0f) b.phC -= 1.0f;
        b.phM += b.incM; if (b.phM >= 1.0f) b.phM -= 1.0f;
        float v = fsin(wrap1(b.phC + b.idx * fsin(b.phM))) * b.amp;
        b.amp *= b.decay; b.idx *= (b.decay * b.decay);
        if (b.amp < 0.0006f) b.on = false;
        dry += v * 0.8f; send += v;
      }

      // a touched key
      for (int i = 0; i < 4; i++) {
        PluckV& p = plucks[i];
        if (!p.on) continue;
        p.ph += p.inc; if (p.ph >= 1.0f) p.ph -= 1.0f;
        float v = (fsin(p.ph) + 0.3f * fsin(wrap1(p.ph * 3.0f))) * p.amp;
        p.amp *= p.decay;
        if (p.amp < 0.0006f) p.on = false;
        dry += v; send += v * 0.7f;
      }

      // syllables
      for (int i = 0; i < 2; i++) {
        BlipV& b = blips[i];
        if (!b.on) continue;
        b.t += ISR;
        float u = b.t / b.dur;
        if (u >= 1.0f) { b.on = false; continue; }
        float inc = b.inc * (1.04f - 0.10f * u);
        b.ph1 += inc; if (b.ph1 >= 1.0f) b.ph1 -= 1.0f;
        b.ph2 += inc * 2.7f; if (b.ph2 >= 1.0f) b.ph2 -= 1.0f;
        float win = 4.0f * u * (1.0f - u);
        float v = (fsin(b.ph1) + 0.5f * fsin(b.ph2)) * win * b.amp;
        dry += v; send += v * 0.5f;
      }

      // weather
      float wn = white();
      if (windAmp > 0.001f) {
        windLp += windF * windBp;
        float hp = wn - windLp - 0.42f * windBp;
        windBp += windF * hp;
        dry += windBp * windAmp;
      }
      if (sRain > 0.001f) {
        rainLp += (wn - rainLp) * 0.45f;
        float hiss = (wn - rainLp) * 0.16f * sRain;
        if ((rng.next() & 1023) < (uint32_t)(sRain * 30.0f)) hiss += white() * 0.5f * sRain;
        dry += hiss;
      }
      if (sCrackle > 0.001f) {
        float imp = 0;
        if ((rng.next() & 16383) < (uint32_t)(sCrackle * 26.0f)) imp = white() * 0.9f;
        crackLp += (imp - crackLp) * 0.55f;
        dry += crackLp * (0.35f + 0.5f * sCrackle);
      }
      if (thunder.on) {
        thunder.t += ISR;
        float t = thunder.t;
        float crack = expf(-t * 9.0f);
        float rumble = clamp01(t * 5.0f) * expf(-t * 0.85f);
        thunder.lp1 += (wn - thunder.lp1) * 0.035f;
        thunder.lp2 += (wn - thunder.lp2) * 0.30f;
        float v = (thunder.lp1 * 4.5f * rumble + thunder.lp2 * 0.9f * crack) * thunder.amp;
        if (t > 5.0f) thunder.on = false;
        dry += v; send += v * 0.5f;
      }
      if (rupture.on) {
        rupture.t += ISR;
        float u = rupture.t / 0.19f;
        if (u >= 1.0f) rupture.on = false;
        else {
          if (--rupture.cnt <= 0) { rupture.hold = white(); rupture.cnt = 2 + (int)(u * 26.0f); }
          rupture.ph = wrap1(rupture.ph + (1900.0f - 1700.0f * u) * ISR);
          float v = (rupture.hold * 0.5f + fsin(rupture.ph) * 0.35f) * (1.0f - u) * 0.6f;
          dry += v; send += v * 0.4f;
        }
      }
      if (geiger.amp > 0.001f) {
        geiger.ph = wrap1(geiger.ph + 2800.0f * ISR);
        dry += fsin(geiger.ph) * geiger.amp;
        geiger.amp *= 0.955f;
      }

      // echo: one long repeat, darker each time round
      int rd = dlyW - echoLen; if (rd < 0) rd += DLY_N;
      float echo = dly[rd] * (1.0f / 16384.0f);
      dlyLp += (echo - dlyLp) * 0.42f;
      float wr = send + dlyLp * echoFb;
      if (wr > 1.9f) wr = 1.9f; else if (wr < -1.9f) wr = -1.9f;
      dly[dlyW] = (int16_t)(wr * 16384.0f);
      if (++dlyW >= DLY_N) dlyW = 0;

      // room: four combs and an allpass
      float rin = (send + echo * 0.6f) * 0.32f, room = 0;
      for (int c = 0; c < 4; c++) {
        float y = CB[c][cbW[c]] * (1.0f / 16384.0f);
        cbLp[c] += (y - cbLp[c]) * 0.55f;
        float w = rin + cbLp[c] * roomFb;
        if (w > 1.9f) w = 1.9f; else if (w < -1.9f) w = -1.9f;
        CB[c][cbW[c]] = (int16_t)(w * 16384.0f);
        if (++cbW[c] >= CB_N[c]) cbW[c] = 0;
        room += y;
      }
      float apo = ap[apW] * (1.0f / 16384.0f);
      float apin = room * 0.25f + apo * 0.5f;
      if (apin > 1.9f) apin = 1.9f; else if (apin < -1.9f) apin = -1.9f;
      ap[apW] = (int16_t)(apin * 16384.0f);
      if (++apW >= AP_N) apW = 0;
      room = apo - apin * 0.5f;

      float mixv = dry * 0.78f + echo * 0.5f + room * (0.55f + 0.5f * sOpen);

      hushLp += (mixv - hushLp) * 0.12f;
      mixv = hushLp + (mixv - hushLp) * bright;
      mixv *= (1.0f - 0.8f * sHush);

      // soft ceiling
      float x = mixv * 0.95f;
      if (x > 3.0f) x = 1.0f; else if (x < -3.0f) x = -1.0f;
      else x = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);

      sGain += gainStep; duckCur += duckStep;
      float o = x * sGain * duckCur;

      if (tock.left > 0) {                              // dry, unducked: this is the measurement
        float u = 1.0f - (float)tock.left / (0.005f * SR);
        tock.ph = wrap1(tock.ph + 2200.0f * ISR);
        float tg = S.muted ? 0.0f : (9000.0f + m * m * 14000.0f);
        o += fsin(tock.ph) * 4.0f * u * (1.0f - u) * tg;
        tock.left--;
      }
      if (o > 32000.0f) o = 32000.0f; else if (o < -32000.0f) o = -32000.0f;

      ownMs += (o * o - ownMs) * 0.0008f;
      int idx = (done + s) * 2;
      out[idx] = (int16_t)((prevOut + o) * 0.5f);
      out[idx + 1] = (int16_t)o;
      prevOut = o;
    }
    done += blk;
  }
  transientAge += (uint32_t)n;
  if (transientAge > 100000000u) transientAge = 100000000u;
}

}  // namespace mn
