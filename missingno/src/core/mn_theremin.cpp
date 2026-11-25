#include "mn_theremin.h"
#include "mn_state.h"
#include "mn_gfx.h"
#include "mn_draw.h"
#include "mn_mind.h"
#include "mn_synth.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace mn {

static constexpr float RANGE_DEG = 55.0f;     // roll from the middle to either end
static constexpr float RANGE_ST = 18.0f;      // semitones from the middle to either end
static constexpr int SCALES = 8;
static const char* const SCALE_NAME[SCALES] = {"glide", "chromatic", "its mood", "aeolian", "phrygian", "locrian", "open", "byzantine"};
static constexpr int TRAIL = 96;

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int iround(float v) { return (int)floorf(v + 0.5f); }

static struct {
  bool init;
  float ax, ay, az;               // the accelerometer, smoothed just enough to be steady
  float home;                     // degrees: how it was held when it began (about the left/right axis)
  float settle;                   // seconds since it began, until the pose is taken as home
  float roll, lean;               // degrees: left/right, and back from upright
  float pos;                      // where the hand is, semitones from the middle
  float note;                     // what is played, semitones from the middle
  int   noteScale;                // the scale `note` was chosen in (-1: none yet)
  float vol, fade, vib, bright;
  float hz;
  uint8_t tx[TRAIL], ta[TRAIL]; int head, n;
} T;

const char* thereminScaleName(int s) { return SCALE_NAME[s >= 0 && s < SCALES ? s : 0]; }
float thereminNoteHz() { return S.theremin && S.sound.thereAmp > 0.01f ? T.hz : 0.0f; }

// Semitones from the middle of degree d of a scale.
static float degreeSemis(int scale, int d) {
  if (scale <= 1) return (float)d;
  int mode = scale == 2 ? S.sound.mode : scale - 3;
  return 12.0f * log2f(modeRatio(mode, d));
}

// The nearest note of the scale, keeping the one it has until another is clearly nearer.
static float quantize(int scale, float pos) {
  if (scale == 0) { T.noteScale = 0; return pos; }
  int lim = scale == 1 ? 26 : 16;
  float best = 0, bd = 1e9f;
  for (int d = -lim; d <= lim; d++) {
    float s = degreeSemis(scale, d), dd = fabsf(s - pos);
    if (dd < bd) { bd = dd; best = s; }
  }
  if (T.noteScale == scale && fabsf(pos - T.note) < bd + 0.22f) return T.note;
  T.noteScale = scale;
  return best;
}

void thereminStart() {
  S.theremin = true; S.thereAge = 0; S.thereHold = false;
  T.fade = 0; T.n = 0; T.noteScale = -1; T.init = false; T.settle = 0;
}

void thereminStop() {
  S.theremin = false; S.thereHold = false;
  S.sound.thereAmp = 0;
}

void thereminTick(float dt) {
  SoundP& p = S.sound;
  if (!S.theremin) { p.thereAmp = 0; return; }
  const Senses& in = S.in;
  S.thereAge += dt;
  float k = 1.0f - expf(-dt / 0.06f);
  if (!T.init) { T.ax = in.acc[0]; T.ay = in.acc[1]; T.az = in.acc[2]; T.init = true; }
  T.ax += (in.acc[0] - T.ax) * k; T.ay += (in.acc[1] - T.ay) * k; T.az += (in.acc[2] - T.az) * k;
  // Axes as the body gives them: held up in front of you, gravity runs along z,
  // x is the sideways lean, y goes through the screen.
  // Roll (left/right) is the same in any pose. Forward/back is measured from
  // however it was held when the word was typed, so no pose is the wrong one.
  T.roll = atan2f(T.ax, sqrtf(T.ay * T.ay + T.az * T.az)) * 57.29578f;
  float pitchAng = atan2f(T.ay, T.az) * 57.29578f;
  if (T.settle < 0.6f) { T.settle += dt; T.home = pitchAng; }
  float d = pitchAng - T.home;
  while (d > 180.0f) d -= 360.0f; while (d < -180.0f) d += 360.0f;
  T.lean = fabsf(d);                                           // 0 as it began .. tipped away either way
  if (!S.thereHold) {
    T.pos = clampf(T.roll / RANGE_DEG, -1.0f, 1.0f) * RANGE_ST;
    T.note = quantize(S.thereScale, T.pos);
    float v = clampf((55.0f - T.lean) / 45.0f, 0.0f, 1.0f);   // full within 10 degrees of home, gone by 55
    T.vol = v * v;
  }
  T.fade = clampf(T.fade + dt / 1.2f, 0, 1);                 // never starts with a shout

  // the gyroscope: twisting about the vertical trembles; any turning sharpens
  float spin = fabsf(in.spin);
  float w = sqrtf(in.gyro[0] * in.gyro[0] + in.gyro[1] * in.gyro[1] + in.gyro[2] * in.gyro[2]);
  float vibT = clampf((spin - 15.0f) / 170.0f, 0, 1), brT = 0.18f + 0.82f * clampf(w / 260.0f, 0, 1);
  T.vib += (vibT - T.vib) * (1.0f - expf(-dt / (vibT > T.vib ? 0.06f : 0.45f)));
  T.bright += (brT - T.bright) * (1.0f - expf(-dt / (brT > T.bright ? 0.04f : 0.7f)));

  // its root, folded into one octave, then the middle set near a4: a small
  // speaker gives little below a few hundred hertz
  float root = p.rootHz > 20.0f && p.rootHz < 4000.0f ? p.rootHz : 110.0f;
  while (root < 98.0f) root *= 2.0f; while (root >= 196.0f) root *= 0.5f;
  T.hz = 4.0f * root * powf(2.0f, S.thereOct + T.note / 12.0f);
  p.thereHz = T.hz;
  p.thereAmp = in.faceDown ? 0.0f : T.vol * T.fade;
  p.thereVib = T.vib;
  p.thereBright = T.bright;
  p.thereGlide = S.thereScale == 0 ? 0.03f : 0.06f;

  // the melody, for the screen
  float x = W * 0.5f + T.note / RANGE_ST * (W * 0.5f - 12.0f);
  T.tx[T.head] = (uint8_t)clampf(x, 0, W - 1);
  T.ta[T.head] = (uint8_t)(p.thereAmp * 255.0f);
  T.head = (T.head + 1) % TRAIL; if (T.n < TRAIL) T.n++;
}

static void pluck(int up) { Note n = {N_PLUCK, S.sound.rootHz * (up ? 6.0f : 4.0f), 0.25f}; S.notes.push(n); }

bool thereminKey(int c) {
  if (!S.theremin || S.typed[0]) return false;
  if (c == K_VOL_UP || c == ';' || c == K_VOL_DOWN || c == '.') {
    int d = (c == K_VOL_UP || c == ';') ? 1 : -1;
    int o = S.thereOct + d; if (o < -3) o = -3; if (o > 2) o = 2;
    S.thereOct = (int8_t)o; pluck(d > 0);
    char b[24]; snprintf(b, sizeof(b), o ? "octave %+d." : "the middle octave.", o);
    mindSay(b, 1.5f, 2, false);
    return true;
  }
  if (c == K_VIEW_PREV || c == ',' || c == K_VIEW_NEXT || c == '/') {
    int d = (c == K_VIEW_NEXT || c == '/') ? 1 : -1;
    S.thereScale = (uint8_t)((S.thereScale + SCALES + d) % SCALES); T.noteScale = -1;
    pluck(d > 0);
    mindSay(SCALE_NAME[S.thereScale], 1.5f, 2, false);
    return true;
  }
  if (c == K_ENTER || c == K_TAB) { thereminButton(); return true; }
  if (c == K_BACK) return true;
  return false;                                    // escape: the mind ends it
}

void thereminButton() {
  S.thereHold = !S.thereHold;
  mindSay(S.thereHold ? "held." : "free.", 1.2f, 2, false);
}

// ---------------------------------------------------------------- the screen
void drawTheremin() {
  clear(C_VOID);
  const int top = 12, bot = 104;
  auto xOf = [](float semis) { return iround(W * 0.5f + semis / RANGE_ST * (W * 0.5f - 12.0f)); };
  // the scale, as strings across the field
  int sc = S.thereScale;
  if (sc <= 1) {
    for (int s = -18; s <= 18; s++) {
      int x = xOf((float)s); bool tonic = (s % 12) == 0;
      for (int y = top; y < bot; y += tonic ? 1 : 3) px(x, y, tonic ? C_PHOSD : C_SOOT);
    }
  } else {
    for (int d = -16; d <= 16; d++) {
      float s = degreeSemis(sc, d);
      if (s < -RANGE_ST - 0.01f || s > RANGE_ST + 0.01f) continue;
      int x = xOf(s); bool tonic = ((d % 7) + 7) % 7 == 0;
      for (int y = top; y < bot; y += tonic ? 1 : 2) px(x, y, tonic ? C_PHOSD : C_SOOT);
    }
  }
  // the melody falling away behind it: newest at the top
  int px_ = -1, py_ = 0;
  for (int i = 0; i < T.n && i < bot - top; i++) {
    int j = (T.head - 1 - i + 2 * TRAIL) % TRAIL;
    int x = T.tx[j], y = top + i, a = T.ta[j];
    int fadeA = a * (bot - top - i) / (bot - top);
    if (a > 6) {
      if (px_ >= 0 && abs(x - px_) > 1) {
        uint16_t c = scale(C_PHOS, 40 + fadeA * 3 / 4);
        line(px_, py_, x, y, c);
      } else blend(x, y, C_PHOS, 40 + fadeA * 3 / 4);
      px_ = x; py_ = y;
    } else px_ = -1;
  }
  // where the hand is, and what sounds
  float amp = S.sound.thereAmp;
  int bx = xOf(T.note), hx = xOf(T.pos);
  if (hx != bx) for (int y = top; y < bot; y += 4) px(hx, y, C_ASH);   // the hand, between notes
  for (int y = top; y < bot; y++) blend(bx, y, S.thereHold ? C_EMBER : C_PHOS, 20 + (int)(amp * 150));
  int r = 2 + (int)(amp * 4);
  disc(bx, top + 1, r, S.thereHold ? C_EMBER : C_PHOS);
  hline(0, bot, W, C_SOOT);

  // header
  text(3, 2, "theremin", C_ASH);
  char b[48];
  if (S.thereOct) snprintf(b, sizeof(b), "%s  oct %+d%s", SCALE_NAME[sc], S.thereOct, S.thereHold ? "  held" : "");
  else snprintf(b, sizeof(b), "%s%s", SCALE_NAME[sc], S.thereHold ? "  held" : "");
  textRight(W - 4, 2, b, S.thereHold ? C_EMBER : C_ASH);

  // what is sounding
  static const char* const NN[12] = {"c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"};
  float m = 69.0f + 12.0f * log2f(T.hz / 440.0f);
  int mi = iround(m); int cents = iround((m - mi) * 100.0f);
  snprintf(b, sizeof(b), "%s%d", NN[((mi % 12) + 12) % 12], mi / 12 - 1);
  textBig(4, 110, b, amp > 0.02f ? C_BONE : C_SOOT, 2);
  snprintf(b, sizeof(b), "%d hz", iround(T.hz)); text(52, 109, b, C_ASH);
  snprintf(b, sizeof(b), "%+d cents", cents); text(52, 120, b, abs(cents) < 6 ? C_PHOS : C_SOOT);
  text(118, 109, "vol", C_SOOT); frame(138, 110, 46, 5, C_SOOT); rect(139, 111, (int)(amp * 44), 3, C_BONE);
  text(118, 120, "vib", C_SOOT); frame(138, 121, 46, 5, C_SOOT); rect(139, 122, (int)(T.vib * 44), 3, C_PHOS);

  // and the creature, singing it
  FaceP f; f.eyeOpen = 1.1f; f.gazeX = clampf(T.pos / RANGE_ST, -1, 1); f.gazeY = -0.4f;
  f.mouthOpen = clampf(amp * 1.3f, 0, 1); f.mouthW = 0.55f + 0.25f * amp; f.mouthWave = T.vib; f.scraps = 2;
  f.shear = clampf(T.roll * 0.15f, -6, 6);
  drawMark(W - 18, H - 2, 0.12f, C_BONE, f, nullptr, S.frame, S.t);

  if (S.thereAge < 9.0f && !S.typed[0]) {                     // how to hold it, at first
    rect(0, 106, 196, 29, C_VOID);
    text(3, 109, "tilt l/r: pitch  tip: fade", C_ASH);
    text(3, 120, "; . octave  , / scale  enter hold", C_SOOT);
  }
}

}  // namespace mn
