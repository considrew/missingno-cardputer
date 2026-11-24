#include "mn_ear.h"
#include <math.h>
#include <string.h>

namespace mn {

// --------------------------------------------------------- audio-thread side
static const int DEC_N = 2048;                 // 12 kHz ring, ~170 ms
static int16_t dec[DEC_N];
static volatile int decW;
static float dc, decAcc; static int decCnt;

static volatile float micMs, ownMsSm;          // smoothed powers
static float slowMs;
static volatile bool onsetFlag;
static uint32_t sinceOnset = 1000000;

// sounding
enum { SD_IDLE = 0, SD_FADE, SD_QUIET, SD_LISTEN, SD_DONE };
static volatile int sdState;
static int32_t sdCount;
static const int HOP = 384;                    // 8 ms
static const int HOPS = 72;
static float hopE[HOPS];
static int hopI, hopFill; static float hopAcc;
static double floorAcc; static int32_t floorN;
static volatile float sdFloor;
static float fadeWin, fadeHist[5]; static int32_t fadeWinN, fadeTotal, fadeK;

// ---------------------------------------------------------- main-thread side
static const int NB = 16;
static const float GF[NB] = {90, 115, 147, 188, 240, 307, 392, 501, 640, 818, 1045, 1335, 1706, 2180, 2786, 3560};
static float gCoef[NB];
static float bandNoise[8], bandCeil[8];
static float coupling = 0.02f;                 // mic power per unit of its own output power
static float loudFloorDb = 30.0f;
static float pitchLast; static int pitchRun;
static float xs[1024];

void earInit() {
  memset(dec, 0, sizeof(dec)); decW = 0; dc = 0; decAcc = 0; decCnt = 0;
  micMs = 0; ownMsSm = 0; slowMs = 1; onsetFlag = false;
  sdState = SD_IDLE;
  for (int i = 0; i < NB; i++) gCoef[i] = 2.0f * cosf(6.2831853f * GF[i] / 12000.0f);
  for (int i = 0; i < 8; i++) { bandNoise[i] = 20.0f; bandCeil[i] = 50.0f; }
  pitchLast = 0; pitchRun = 0;
}

float earDuck() { int s = sdState; return (s == SD_FADE || s == SD_QUIET || s == SD_LISTEN) ? 0.0f : 1.0f; }

bool earPush(const int16_t* mic, int n, float ownRms, uint32_t ownTransientAge) {
  bool strike = false;
  float ms = 0;
  int w = decW;
  for (int i = 0; i < n; i++) {
    float x = (float)mic[i];
    dc += (x - dc) * 0.002f;
    x -= dc;
    ms += x * x;
    decAcc += x;
    if (++decCnt == 4) {
      float v = decAcc * 0.25f;
      if (v > 32767) v = 32767; else if (v < -32767) v = -32767;
      dec[w] = (int16_t)v; w = (w + 1) & (DEC_N - 1);
      decAcc = 0; decCnt = 0;
    }
    if (sdState == SD_LISTEN) {
      hopAcc += x * x;
      if (++hopFill == HOP) {
        if (hopI < HOPS) hopE[hopI++] = hopAcc / HOP;
        hopAcc = 0; hopFill = 0;
        if (hopI >= HOPS) sdState = SD_DONE;
      }
    } else if (sdState == SD_QUIET) {
      floorAcc += (double)x * x; floorN++;
    }
  }
  decW = w;
  ms /= (n > 0 ? n : 1);

  // a knock: this block towers over the recent past, and it was not us
  sinceOnset += (uint32_t)n;
  if (ms > slowMs * 7.0f && ms > 250.0f * 250.0f && sinceOnset > 7200 && ownTransientAge > 9600 && sdState == SD_IDLE) {
    onsetFlag = true; sinceOnset = 0;
  }
  slowMs += (ms - slowMs) * 0.05f;
  micMs = micMs + (ms - micMs) * 0.09f;
  float o2 = ownRms * ownRms;
  ownMsSm = ownMsSm + (o2 - ownMsSm) * 0.09f;

  if (sdState == SD_FADE) {
    // Hold its breath and wait for its own echoes to drain. A dead room is
    // quiet at once; a cathedral keeps ringing, so it waits (up to 2.5 s)
    // until the level has stopped falling.
    fadeTotal += n; fadeWin += ms * n; fadeWinN += n;
    if (fadeWinN >= 2880) {                                  // 60 ms windows
      float wv = fadeWin / fadeWinN;
      fadeWin = 0; fadeWinN = 0;
      // settled = no quieter than it was 300 ms ago
      bool settled = fadeK >= 5 && wv > fadeHist[fadeK % 5] * 0.8f;
      fadeHist[fadeK % 5] = wv; fadeK++;
      if (settled || fadeTotal >= 120000) { sdState = SD_QUIET; sdCount = 7200; floorAcc = 0; floorN = 0; }
    }
  } else if (sdState == SD_QUIET) {
    sdCount -= n;
    if (sdCount <= 0) {
      sdFloor = floorN > 0 ? (float)(floorAcc / floorN) : 1.0f;
      hopI = 0; hopFill = 0; hopAcc = 0;
      sdState = SD_LISTEN;
      strike = true;
    }
  }
  return strike;
}

void earStartSounding() {
  if (sdState != SD_IDLE) return;
  fadeWin = 0; fadeWinN = 0; fadeTotal = 0; fadeK = 0;
  sdState = SD_FADE;
}
bool earSoundingBusy() { return sdState != SD_IDLE; }

bool earTakeSounding(Sounding& out) {
  if (sdState != SD_DONE) return false;
  float fl = sdFloor; if (fl < 1.0f) fl = 1.0f;
  int p = 0;
  for (int i = 1; i < 24; i++) if (hopE[i] > hopE[p]) p = i;
  float peak = hopE[p];
  out = Sounding();
  out.floorDb = 10.0f * log10f(fl / (32768.0f * 32768.0f));
  out.valid = peak > fl * 100.0f;                              // 20 dB over the room's own noise
  float peakDb = 10.0f * log10f(peak + 1.0f), floorDb = 10.0f * log10f(fl);
  float span = peakDb - floorDb; if (span < 6.0f) span = 6.0f;
  for (int i = 0; i < 60; i++) {
    int j = p + i;
    float e = j < HOPS ? hopE[j] : fl;
    float db = 10.0f * log10f(e + 1.0f);
    float v = (db - floorDb) / span;
    out.env[i] = (uint8_t)(v <= 0 ? 0 : (v >= 1 ? 255 : (int)(v * 255.0f)));
  }
  // What came back after the strike itself (two hops). Rooms answer in
  // scattered reflections, so look at a five-hop average and forgive gaps.
  static float sm[HOPS];
  int a0 = p + 2, last = -1, gap = 0;
  for (int j = a0; j + 4 < HOPS; j++) {
    sm[j] = (hopE[j] + hopE[j + 1] + hopE[j + 2] + hopE[j + 3] + hopE[j + 4]) * 0.2f;
    if (sm[j] > fl * 4.0f) { last = j; gap = 0; }               // 6 dB proud of the floor
    else if (++gap > 6) break;
  }
  // Fit a line to the decay: its slope is the reverberation time, however loud the strike was.
  float rt = 0;
  int n = last >= a0 ? last - a0 + 1 : 0;
  if (n >= 5) {
    float sx = 0, sy = 0, sxx = 0, sxy = 0; int m = 0;
    for (int j = a0; j <= last; j++) {
      if (sm[j] < fl * 4.0f) continue;
      float x = (float)(j - a0) * 0.008f, y = 10.0f * log10f(sm[j] - fl);   // the floor taken out
      sx += x; sy += y; sxx += x * x; sxy += x * y; m++;
    }
    float den = m * sxx - sx * sx;
    float slope = (m >= 4 && den > 1e-9f) ? (m * sxy - sx * sy) / den : 0;   // dB per second, negative
    rt = slope < -12.0f ? -60.0f / slope : 5.0f;
    if (rt > 5.0f) rt = 5.0f;
    if (rt < 0.03f) rt = 0.03f;
  } else if (n >= 1) rt = 0.024f * (n + 2);                      // something came back, briefly
  out.tailMs = rt * 1000.0f;
  // thresholds found on the bench; expect to retune them against real rooms
  out.verdict = rt <= 0 ? 0 : rt < 0.10f ? 1 : rt < 0.95f ? 2 : rt < 2.0f ? 3 : 4;
  sdState = SD_IDLE;
  return true;
}

static float pitchOf(const float* x, int n, float& clarity) {
  // McLeod's normalised square difference: first strong peak, not the tallest.
  const int LO = 12, HI = 160;                 // 1000 Hz .. 75 Hz at 12 kHz
  const int Wn = n - HI - 2;
  static float nsdf[HI + 2];
  float e0 = 0;
  for (int i = 0; i < Wn; i++) e0 += x[i] * x[i];
  if (e0 < 1e-3f) { clarity = 0; return 0; }
  float eT = e0;                               // energy of the shifted window, slid along
  float best = 0;
  for (int t = 1; t <= HI + 1; t++) {
    eT += x[Wn + t - 1] * x[Wn + t - 1] - x[t - 1] * x[t - 1];
    if (t < LO - 1) continue;
    float acf = 0;
    for (int i = 0; i < Wn; i += 2) acf += x[i] * x[i + t];       // every other product is plenty
    float v = 4.0f * acf / (e0 + eT + 1e-6f);
    nsdf[t] = v;
    if (t >= LO && t <= HI && v > best) best = v;
  }
  clarity = best;
  if (best < 0.6f) return 0;
  float thr = best * 0.85f;
  for (int t = LO; t < HI; t++) {
    if (nsdf[t] > thr && nsdf[t] >= nsdf[t - 1] && nsdf[t] >= nsdf[t + 1]) {
      float a = nsdf[t - 1], b = nsdf[t], c = nsdf[t + 1];
      float den = a - 2 * b + c;
      float off = fabsf(den) > 1e-6f ? 0.5f * (a - c) / den : 0;
      return 12000.0f / ((float)t + off);
    }
  }
  return 0;
}

void earAnalyze(Senses& in, float dt, const float* ownPartials, int nPartials, float ownRootHz) {
  // snapshot the last 1024 decimated samples
  int w = decW;
  for (int i = 0; i < 1024; i++) xs[i] = (float)dec[(w - 1024 + i) & (DEC_N - 1)];

  // ---- where the energy sits
  const float* g = xs + 1024 - 256;
  float pw[NB];
  for (int k = 0; k < NB; k++) {
    float s1 = 0, s2 = 0, c = gCoef[k];
    for (int i = 0; i < 256; i++) { float s0 = g[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    pw[k] = (s1 * s1 + s2 * s2 - c * s1 * s2) * (1.0f / (128.0f * 128.0f));
  }
  for (int b = 0; b < 8; b++) {
    float p = pw[b * 2] + pw[b * 2 + 1];
    float db = 10.0f * log10f(p + 1.0f);
    if (db < bandNoise[b]) bandNoise[b] = db; else bandNoise[b] += dt * 0.6f;       // the floor creeps up to meet it
    if (db > bandCeil[b]) bandCeil[b] = db; else bandCeil[b] -= dt * 2.5f;
    float span = bandCeil[b] - bandNoise[b] - 5.0f; if (span < 26.0f) span = 26.0f;
    float v = (db - bandNoise[b] - 5.0f) / span;
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    in.bands[b] += (v - in.bands[b]) * (v > in.bands[b] ? 0.6f : 0.22f);
  }

  // ---- how loud the world is, without itself
  float mm = micMs, oo = ownMsSm;
  if (oo > 400.0f * 400.0f) {
    float ratio = mm / oo;
    if (ratio < coupling) coupling += (ratio - coupling) * 0.3f;
    else coupling *= 1.0f + dt * 0.02f;                           // drifts up; any quiet moment pulls it back
    if (coupling > 4.0f) coupling = 4.0f;
  }
  float ext = mm - 1.35f * coupling * oo; if (ext < 0) ext = 0;
  float db = 10.0f * log10f(ext + 1.0f);
  if (db < loudFloorDb) loudFloorDb += (db - loudFloorDb) * 0.2f; else loudFloorDb += dt * 0.25f;
  if (loudFloorDb < 18.0f) loudFloorDb = 18.0f;
  float loud = (db - loudFloorDb - 5.0f) / 38.0f;
  loud = loud < 0 ? 0 : (loud > 1 ? 1 : loud);
  in.loud += (loud - in.loud) * (loud > in.loud ? 0.5f : 0.15f);
  // the same, as a meter reads it: dB below full scale (a full-scale square wave reads 90.3 dB here)
  float dbfs = db - 90.31f;
  in.levelDb += (dbfs - in.levelDb) * (dbfs > in.levelDb ? 0.5f : 0.15f);
  in.floorDb = loudFloorDb - 90.31f;

  // ---- a knock
  in.onset = onsetFlag; onsetFlag = false;

  // ---- a held tone that is not its own
  float clarity = 0, f = 0;
  bool outside = ext > 1.2f * coupling * oo + 120.0f * 120.0f;    // more in the air than it put there
  if (outside && sdState == SD_IDLE) f = pitchOf(xs, 1024, clarity);
  if (f > 0) {
    bool own = false;
    for (int i = 0; i < nPartials && !own; i++) {
      float r = f / ownPartials[i];
      if (fabsf(r - 1.0f) < 0.025f || fabsf(r - 0.5f) < 0.0125f || fabsf(r - 2.0f) < 0.05f) own = true;
    }
    if (!own && ownRootHz > 20.0f && nPartials > 0) {              // its drone is one harmonic series
      float r = f / ownRootHz, k = roundf(r);
      if (k >= 1 && k <= 6 && fabsf(r - k) < 0.025f * k) own = true;
    }
    if (own) f = 0;
  }
  if (f > 0 && pitchLast > 0 && fabsf(f / pitchLast - 1.0f) < 0.04f) pitchRun++; else pitchRun = f > 0 ? 1 : 0;
  pitchLast = f;
  in.pitchHz = pitchRun >= 3 ? f : 0;
}

}  // namespace mn
