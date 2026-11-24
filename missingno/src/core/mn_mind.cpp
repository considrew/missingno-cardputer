#include "mn_mind.h"
#include "mn_gfx.h"
#include "mn_draw.h"
#include "mn_astro.h"
#include "mn_seal.h"
#include "mn_drift.h"
#include "mn_synth.h"
#include "mn_lexicon.h"
#include "mn_horizon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mn {

static Rng R;
static Sky sky;

struct Later { float at; Note n; bool on; };

static struct Mind {
  // speech
  int sayPrio; const char* recent[8]; int recentI; float nextMuse; int lastShown; int helpN;
  // sky
  float skyAcc; bool skyInit, skyValid, havePos; float skyAge, prevSunEl; double lat, lon;
  // affect
  float loudFast, loudSlow, shock, keyHeat, still, asleepFor, openTarget; bool asleep;
  // place
  float placeAcc; int prevLevel; float omenRate, nextGeiger; bool noSkySaid;
  float carriedSaid, highSaid, walkSaid; uint32_t lastStepMark, lastSteps; int unsaved;
  // ether
  bool firstCensus, lastNovel;
  // hearing
  float pitchRef, pitchHold, pitchGone; bool resonated;
  float knocks[6]; int knockN; float lastKnock, knockSaid;
  // body
  float spinHold, shakeHold, shakenSaid, spunSaid; bool wasFreefall;
  // kin, power, weather
  int prevPeers; float lowSaid; int prevWx; float thunderIn;
  // face
  float blinkIn, blinkT, gazeIn, gtx, gty, startle, glitchPulse, scatterT, beat;
  // sound
  float modeHold; uint8_t mode; float root;
  // ridge / spiral
  uint8_t markNext, pendEv; float ridgeH;
  // the land that is really there
  bool hzSaid; int sunBehind;
  // commands
  float forgetArmed, formatArmed; uint32_t stepsAtBoot; int lines;
  Later later[12];
} M;

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float approach(float cur, float target, float dt, float tau) { return cur + (target - cur) * (1.0f - expf(-dt / tau)); }
static inline float maxf(float a, float b) { return a > b ? a : b; }

static void note(NoteKind k, float hz, float amp) { Note n = {(uint8_t)k, hz, amp}; S.notes.push(n); }
static void later(float delay, NoteKind k, float hz, float amp) {
  for (Later& l : M.later) if (!l.on) { l.at = S.t + delay; l.n = {(uint8_t)k, hz, amp}; l.on = true; return; }
}
static float deg(int d) { return M.root * 4.0f * modeRatio(M.mode, d); }
static void mark(uint8_t ev) { if (ev > M.pendEv) M.pendEv = ev; }
// Novelty widens the spiral, but never past its rim: each push covers part of what is left.
static void differ(float by) { S.difference = clampf(S.difference + by * (1.0f - S.difference) * 1.5f, 0.0f, 1.0f); }

static void wake() {
  M.still = 0;
  if (S.sleep > 0.3f && !S.in.faceDown) S.sleep = 0.3f;
}

void mindSay(const char* s, float hold, int prio, bool cipher) {
  if (!s || !s[0]) return;
  bool active = S.say[0] && S.sayAge < S.sayHold;
  if (active && prio < M.sayPrio) return;
  strncpy(S.say, s, sizeof(S.say) - 1); S.say[sizeof(S.say) - 1] = 0;
  if (strlen(S.say) > 78) S.say[78] = 0;
  S.sayAge = 0;
  S.sayHold = hold + (float)strlen(S.say) / (cipher ? 14.0f : 30.0f);
  S.sayCipher = cipher; M.sayPrio = prio; M.lastShown = 0;
}

static void sayPool(int pool, float hold = 6.5f, int prio = 1) {
  const char* s = nullptr;
  for (int tries = 0; tries < 5; tries++) {
    s = lexPick(pool, R.next());
    bool seen = false;
    for (const char* r : M.recent) if (r == s) seen = true;
    if (!seen) break;
  }
  bool active = S.say[0] && S.sayAge < S.sayHold;
  if (active && prio < M.sayPrio) return;
  M.recent[M.recentI] = s; M.recentI = (M.recentI + 1) % 8;
  mindSay(s, hold, prio, false);
}

// ---------------------------------------------------------------- life in/out
void mindLifeIn() {
  Life& L = life();
  memcpy(S.spiral, L.spiral, SPIRAL_N); memcpy(S.spiralEv, L.spiralEv, SPIRAL_N);
  S.spiralLen = (uint16_t)L.spiralLen; S.spiralTotal = L.spiralTotal;
  S.sound.master = L.volume; S.muted = L.muted != 0;
  M.root = (L.rootHz > 60 && L.rootHz < 200) ? L.rootHz : 110.0f;
  S.sound.rootHz = M.root;
  M.stepsAtBoot = L.steps;
  S.cellsKnown = L.cellCount; S.placesFound = L.foundCount; S.linesCrossed = L.linesCrossed;
}

void mindLifeOut() {
  Life& L = life();
  memcpy(L.spiral, S.spiral, SPIRAL_N); memcpy(L.spiralEv, S.spiralEv, SPIRAL_N);
  L.spiralLen = S.spiralLen; L.spiralTotal = S.spiralTotal;
  L.volume = S.sound.master; L.muted = S.muted ? 1 : 0; L.rootHz = M.root;
  L.steps = M.stepsAtBoot + S.in.steps;
  if (S.in.haveTime) { L.lastUtc = S.in.utc; if (L.bornUtc == 0) L.bornUtc = S.in.utc; }
}

void mindInit(uint32_t seed) {
  R.s = seed ? seed : 0x5EEDu;
  memset(&M, 0, sizeof(M));
  Life& L = life();
  if (L.spiralInterval < 1.0f) lifeReset();
  M.mode = 0; M.root = 110.0f;
  mindLifeIn();
  M.prevWx = -1; M.prevLevel = 0; M.firstCensus = true;
  M.carriedSaid = M.highSaid = M.walkSaid = M.shakenSaid = M.spunSaid = M.lowSaid = M.knockSaid = -1.0e6f;
  M.forgetArmed = M.formatArmed = -1.0e6f;
  M.nextMuse = R.range(28, 50); M.blinkIn = 2.0f; M.gazeIn = 1.0f; M.thunderIn = 6.0f;
  M.openTarget = 0.4f; M.omenRate = 1.0f / 22.0f; M.nextGeiger = 1.0f;
  S.view = V_SHORE; S.t = 0; S.frame = 0; S.hintAge = 0;
  S.say[0] = 0; S.sayAge = 99; S.typed[0] = 0;
  for (int x = 0; x < W; x++) { S.ridge[x] = (uint8_t)(3 + vnoise(x * 0.06f, 1.7f, seed) * 9.0f); S.ridgeMark[x] = 0; }
  S.ridgeMark[R.irange(20, 90)] = 2; S.ridgeMark[R.irange(150, 220)] = 3;
  M.ridgeH = S.ridge[W - 1];
  setChartOracle(driftKnown);
  L.wakes++;
  if (L.wakes > 4) S.hintAge = 99;
  if (L.wakes == 1) mindSay(lexPick(L_FIRST, 0), 9.0f, 2, true);
  else sayPool(L_WAKE, 6.0f, 1);
}

// ------------------------------------------------------------------- the sky
static float lerpAngle(float a, float b, float k) {
  float d = fmodf(b - a + 540.0f, 360.0f) - 180.0f;
  float r = a + d * k;
  return r < 0 ? r + 360.0f : (r >= 360.0f ? r - 360.0f : r);
}

// Said once a wake, when it first knows the shape of the land; and the sun going behind
// a real ridge, which happens before (or after) the flat horizon says it should.
static const char* const DIRN[8] = {"north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"};
static void tickLand() {
  if (!HZ.here) { M.sunBehind = 0; return; }
  if (!M.hzSaid && S.t > 14.0f && S.sleep < 0.5f) {
    char b[84];
    if (HZ.peakAng >= 0.35f)
      snprintf(b, sizeof(b), "the land rises to the %s. the highest of it is %d km away.",
               DIRN[(int)((HZ.peakAz + 22.5f) / 45.0f) & 7], (int)(HZ.peakKm + 0.5f));
    else snprintf(b, sizeof(b), "the horizon here is level. nothing stands between me and the edge.");
    mindSay(b, 8.0f, 1, false);
    M.hzSaid = true;
  }
  if (!M.skyValid || M.skyAge < 20.0f || S.sleep >= 0.5f) { M.sunBehind = 0; return; }
  float ridge = horizonAngle(sky.sunAz);
  int behind = (sky.sunEl > 0.3f && sky.sunEl < ridge) ? 2 : (sky.sunEl >= ridge && sky.sunEl > 0.3f ? 1 : 0);
  if (M.sunBehind == 1 && behind == 2) {
    mindSay(sky.solarHour > 12.0f ? "the sun has gone behind the land. the sky has not noticed yet."
                                  : "the sun is up there somewhere, behind the land.", 8.0f, 1, false);
    note(N_BELL, deg(0), 0.5f); differ(0.06f);
  } else if (M.sunBehind == 2 && behind == 1) {
    mindSay(sky.solarHour < 12.0f ? "the sun has cleared the ridge." : "the sun came out from behind the land.", 7.0f, 1, false);
    note(N_BELL, deg(4), 0.5f); differ(0.06f);
  }
  if (behind) M.sunBehind = behind;
}

static void tickSky(float dt) {
  Senses& in = S.in; Life& L = life(); SceneP& sc = S.scene;
  M.skyAcc += dt;
  if (M.skyAcc >= 1.0f || !M.skyInit) {
    M.skyAcc = 0; M.skyInit = true;
    M.havePos = in.haveFix || L.haveLast;
    M.lat = in.haveFix ? in.lat : L.lastLat; M.lon = in.haveFix ? in.lon : L.lastLon;
    horizonAt(M.havePos, M.lat, M.lon);
    tickLand();
    if (in.haveTime && M.havePos) {
      astro(in.utc, M.lat, M.lon, sky);
      if (M.skyValid && M.skyAge > 20.0f && S.sleep < 0.5f) {
        if (M.prevSunEl < -0.8f && sky.sunEl >= -0.8f) { sayPool(L_DAWN, 8, 1); differ(0.1f); note(N_BELL, deg(4), 0.6f); }
        if (M.prevSunEl >= -0.8f && sky.sunEl < -0.8f) { sayPool(L_DUSK, 8, 1); differ(0.1f); note(N_BELL, deg(0), 0.6f); }
      }
      M.prevSunEl = sky.sunEl; M.skyValid = true;
    } else {
      // No clock, no place: it keeps a permanent deep twilight of its own.
      M.skyValid = false; M.skyAge = 0;
      sky = Sky();
      sky.moonPhase = in.haveTime ? moonPhaseAt(in.utc) : 0.3f;
      sky.tide = sinf(S.t / 190.0f) * 0.6f;
      sky.sunEl = -9; sky.sunAz = 265; sky.moonEl = 28; sky.moonAz = 150;
    }
    // things sealed to a day
    if (in.haveTime && S.t > 22.0f && S.sleep < 0.5f) {
      int month = sky.month, day = sky.day;
      if (!M.skyValid) {                                     // no place: use the plain UTC date
        Sky tmp; astro(in.utc, 0, 0, tmp); month = tmp.month; day = tmp.day;
      }
      int key = month * 100 + day;
      if (key != L.lastSealDay) {
        L.lastSealDay = key;
        Sealed s;
        if (sealDate(month, day, s) && s.text[0]) {
          mindSay(s.text, 13.0f, 3, true); note(N_BELL, deg(2), 0.8f); later(0.5f, N_BELL, deg(6), 0.6f);
          differ(0.15f); mark(1);
        }
        S.wantSave = true;
      }
    }
  }
  if (M.skyValid) M.skyAge += dt;
  float k = 1.0f - expf(-dt / 1.5f);
  sc.sunEl += (sky.sunEl - sc.sunEl) * k; sc.moonEl += (sky.moonEl - sc.moonEl) * k;
  sc.sunAz = lerpAngle(sc.sunAz, sky.sunAz, k); sc.moonAz = lerpAngle(sc.moonAz, sky.moonAz, k);
  sc.moonPhase = sky.moonPhase;
  sc.tide += (sky.tide - sc.tide) * k;
  sc.south = M.havePos && M.lat < 0;
}

static bool night() { return sky.sunEl < -6.0f; }
static bool witching() { return M.skyValid && sky.solarHour >= 2.5f && sky.solarHour <= 3.5f; }

// --------------------------------------------------------------- the weather
static void tickWeather(float dt) {
  Senses& in = S.in; SceneP& sc = S.scene;
  int cls = 0; float fog = 0, rain = 0, snow = 0;
  if (in.haveWx) {
    int w = in.wmo;
    if (w == 45 || w == 48) { fog = 0.8f; cls = 1; }
    else if (in.visKm < 1.0f) fog = 0.7f; else if (in.visKm < 4.0f) fog = 0.35f; else if (in.visKm < 8.0f) fog = 0.15f;
    if (w >= 51 && w <= 57) { rain = 0.25f + 0.1f * (w - 51) * 0.5f; cls = 2; }
    else if (w >= 61 && w <= 67) { rain = w == 61 ? 0.45f : (w == 63 ? 0.7f : (w == 65 ? 1.0f : 0.6f)); cls = 2; }
    else if (w >= 80 && w <= 82) { rain = 0.5f + 0.25f * (w - 80); cls = 2; }
    else if (w >= 71 && w <= 77) { snow = w == 71 ? 0.35f : (w == 73 ? 0.6f : (w == 75 ? 0.9f : 0.3f)); cls = 3; }
    else if (w == 85 || w == 86) { snow = w == 85 ? 0.5f : 0.85f; cls = 3; }
    else if (w >= 95) { rain = 0.85f; cls = 4; }
  } else {
    fog = clampf((S.unease - 0.55f) * 0.8f, 0, 0.4f);
  }
  float cloudT = in.haveWx ? in.cloud : 0.2f + 0.22f * vnoise(S.t * 0.01f, 3.3f, 7);
  sc.cloud = approach(sc.cloud, cloudT, dt, 4.0f);
  sc.fog = approach(sc.fog, fog, dt, 5.0f);
  sc.rain = approach(sc.rain, rain, dt, 4.0f);
  sc.snow = approach(sc.snow, snow, dt, 4.0f);
  float windT = in.haveWx ? clampf(in.windKmh * 0.35f * sinf(in.windDir * 0.0174533f), -30.0f, 30.0f) : 2.0f;
  sc.wind = approach(sc.wind, windT, dt, 5.0f);
  if (cls == 4) {
    M.thunderIn -= dt;
    if (M.thunderIn <= 0) {
      sc.flash = 1.0f; M.thunderIn = R.range(7.0f, 38.0f); M.shock += 0.2f; M.startle = 0.5f;
      later(R.range(0.8f, 4.0f), N_THUNDER, 0, R.range(0.5f, 1.0f));
    }
  }
  sc.flash = maxf(0.0f, sc.flash - dt * 3.5f);
  if (in.haveWx) {
    if (M.prevWx >= 0 && cls != M.prevWx && S.sleep < 0.5f) {
      static const int POOLS[5] = {L_QUIET, L_FOG, L_RAIN, L_SNOW, L_STORM};
      sayPool(POOLS[cls], 7, 1); differ(0.1f);
    }
    M.prevWx = cls;
  }
}

// ------------------------------------------------------- where it is standing
static void apparition(const Sealed& s) {
  static const uint8_t OMEN[13] = {3, 2, 3, 6, 1, 3, 5, 6, 2, 4, 5, 0, 4};
  SceneP& sc = S.scene;
  mindSay(s.text[0] ? s.text : "here.", 16.0f, 3, true);
  bool isNew = foundAdd(s.tag);
  note(N_CHIME_UP, deg(0), 1.0f);
  M.glitchPulse = 1.0f; differ(0.4f); mark(3);
  sc.omen = OMEN[s.kind < 13 ? s.kind : 0]; sc.omenT = 0; M.omenRate = 1.0f / 24.0f;
  if (s.kind == 6) M.scatterT = 2.5f;                                  // it comes apart where it began
  if (s.kind == 1) for (int i = 0; i < 12; i++) later(i * 0.06f + R.f() * 0.05f, N_GEIGER, 0, 1.0f);
  if (s.kind == 11) S.sound.hush = 1.0f;
  if (isNew) S.wantSave = true;
}

static void tickPlace(float dt) {
  Senses& in = S.in; SceneP& sc = S.scene;
  if (sc.omen) { sc.omenT += dt * M.omenRate; if (sc.omenT >= 1.0f) { sc.omen = 0; sc.omenT = 0; } }

  // footsteps are small differences
  if (in.steps > M.lastSteps) {
    differ(0.0003f * (float)(in.steps - M.lastSteps));
    if (in.steps - M.lastStepMark >= 400) {
      M.lastStepMark = in.steps;
      if (S.sleep < 0.5f && S.t - M.walkSaid > 240.0f) { sayPool(L_WALK, 7, 0); M.walkSaid = S.t; }
    }
  }
  M.lastSteps = in.steps;

  // a place that ticks
  if (S.presence >= 1 && S.placeKind == 1) {
    M.nextGeiger -= dt;
    if (M.nextGeiger <= 0) {
      note(N_GEIGER, 0, 0.5f + 0.5f * R.f());
      M.nextGeiger = -logf(R.f() + 1e-4f) / (0.08f * S.presence * S.presence * S.presence);
    }
  } else if (in.antenna > 0.25f) {
    M.nextGeiger -= dt;
    if (M.nextGeiger <= 0) { note(N_GEIGER, 0, 0.3f + in.antenna * 0.6f); M.nextGeiger = -logf(R.f() + 1e-4f) / (in.antenna * 7.0f); }
  }

  M.placeAcc += dt;
  if (M.placeAcc < 2.5f) return;
  M.placeAcc = 0;
  if (!in.haveFix) return;

  DriftEvent ev = driftVisit(in.lat, in.lon, true);
  if (ev.newCell) {
    differ(0.05f); mark(1);
    if (S.sleep < 0.5f) { note(N_PLUCK, deg(R.irange(0, 4)), 0.5f); if (R.chance(0.2f)) sayPool(L_NEWCELL, 5, 0); }
    if (++M.unsaved >= 4) { S.wantSave = true; M.unsaved = 0; }
  }
  if (ev.line && in.gpsFix) {
    wake(); differ(0.2f); mark(2);
    sc.omen = 5; sc.omenT = 0; M.omenRate = 1.0f / 9.0f;
    note(N_BELL, deg(0), 0.9f); later(0.35f, N_BELL, deg(4), 0.7f);
    if (ev.special) mindSay(lexLine(ev.special), 10.0f, 2, false); else sayPool(L_LINE, 8, 2);
    S.wantSave = true;
  }
  if (in.gpsFix) {
    if (in.speedKmh > 400.0f && in.altM > 6000.0f) { if (S.t - M.carriedSaid > 900.0f) { sayPool(L_FLIGHT, 8, 1); M.carriedSaid = S.t; } }
    else if (in.speedKmh > 160.0f) { if (S.t - M.carriedSaid > 900.0f) { sayPool(L_CARRIED, 8, 1); M.carriedSaid = S.t; } }
    else if (in.altM > 2500.0f && S.t - M.highSaid > 3600.0f) { sayPool(L_HIGH, 8, 1); M.highSaid = S.t; }
    if (in.speedKmh > 3.0f) differ(0.001f);
  }

  Sealed s; sealLookup(in.lat, in.lon, s);
  int level = s.level; bool capped = false;
  if (!in.gpsFix && level > 2) { level = 2; capped = true; s.text[0] = 0; }   // it knows the region, not the spot
  if (!in.gpsFix && level == 2 && s.level == 2) capped = false;
  S.placeKind = level ? s.kind : 0;
  if (level > M.prevLevel) {
    wake(); differ(0.15f); mark(1);
    S.presence = level;
    if (level == 4) apparition(s);
    else {
      note(N_BELL, deg(level == 1 ? 0 : (level == 2 ? 2 : 4)), 0.7f);
      if (s.text[0]) mindSay(s.text, 9.0f, 3, true);
      else if (capped && !M.noSkySaid) { sayPool(L_NOSKY, 8, 2); M.noSkySaid = true; }
      else sayPool(level == 1 ? L_FAR : (level == 2 ? L_NEAR : L_CLOSE), 8, 2);
    }
  } else if (level < M.prevLevel) {
    S.presence = level;
    if (M.prevLevel == 4) { sc.omen = 0; if (S.sound.hush > 0.9f) S.sound.hush = 0; }
    if (level == 0) { sayPool(L_RECEDE, 7, 1); M.noSkySaid = false; }
  }
  M.prevLevel = level;
}

// ------------------------------------------------------------------ the ether
static void tickEther() {
  Senses& in = S.in;
  if (!in.etherFresh) return;
  in.etherFresh = false;
  differ(in.etherNovelty * 0.2f);
  bool awake = S.sleep < 0.5f;
  if (in.apCount > 0 && in.etherNovelty > 0.85f) {
    if (awake && R.chance(0.8f)) sayPool(L_NEWAIR, 7, 1);
    M.lastNovel = true; mark(1);
  } else if (in.etherVisits >= 3 && in.etherNovelty < 0.35f && (M.firstCensus || M.lastNovel)) {
    if (awake) sayPool(L_KNOWNAIR, 7, 1);
    M.lastNovel = false;
  } else if (in.apCount == 0 && M.firstCensus && awake && R.chance(0.5f)) {
    sayPool(L_DEADAIR, 7, 0);
  }
  M.firstCensus = false;
}

// ---------------------------------------------------------------- what it hears
static void tickHearing(float dt) {
  Senses& in = S.in;
  M.loudFast = approach(M.loudFast, in.loud, dt, 0.8f);
  M.loudSlow = approach(M.loudSlow, in.loud, dt, 60.0f);
  bool awake = S.sleep < 0.5f;
  if (in.onset) {
    if (M.loudSlow < 0.15f) M.shock += 0.12f;
    wake(); M.startle = 0.45f; M.gtx = R.range(-1.0f, 1.0f); M.gazeIn = 1.2f;
    S.arousal = clampf(S.arousal + 0.12f, 0, 1); differ(0.02f);
    M.markNext = 1;
    if (M.knockN < 6) M.knocks[M.knockN++] = S.t;
    M.lastKnock = S.t;
  }
  // call and response: after a pause it knocks the same rhythm back
  if (M.knockN > 0 && S.t - M.lastKnock > 0.9f) {
    if (M.knockN >= 2 && M.knocks[M.knockN - 1] - M.knocks[0] < 4.0f && awake) {
      for (int i = 0; i < M.knockN; i++) later(0.05f + M.knocks[i] - M.knocks[0], N_PLUCK, M.root * 2.0f, 1.0f);
      if (S.t - M.knockSaid > 45.0f && R.chance(0.35f)) { sayPool(L_KNOCK, 5, 1); M.knockSaid = S.t; }
    }
    M.knockN = 0;
  }

  // a held tone: it answers a fifth above, and if you stay, it retunes to you
  if (in.pitchHz > 0) {
    M.pitchGone = 0;
    if (M.pitchRef > 0 && fabsf(in.pitchHz / M.pitchRef - 1.0f) < 0.045f) {
      M.pitchHold += dt; M.pitchRef += (in.pitchHz - M.pitchRef) * 0.2f;
    } else { M.pitchRef = in.pitchHz; M.pitchHold = 0; }
    if (M.pitchHold > 0.45f) {
      float a = M.pitchRef * 1.5f;
      while (a < 260.0f) a *= 2.0f;
      while (a > 1040.0f) a *= 0.5f;
      S.sound.answerHz = a; wake();
      if (M.pitchHold > 3.0f && !M.resonated) {
        float r = M.pitchRef;
        while (r < 82.4f) r *= 2.0f;
        while (r >= 164.8f) r *= 0.5f;
        M.root = r; life().rootHz = r; life().resonances++;
        S.resonance = 1.0f; M.resonated = true;
        note(N_CHIME_UP, a, 1.0f); sayPool(L_RESONANCE, 7, 2);
        differ(0.25f); mark(1); S.wantSave = true;
      }
    }
  } else {
    M.pitchGone += dt;
    if (M.pitchGone > 0.35f) { S.sound.answerHz = 0; M.pitchHold = 0; }
    if (M.pitchGone > 2.5f) { M.pitchRef = 0; M.resonated = false; }
  }
}

// ---------------------------------------------------------------- how it is held
static void tickBody(float dt) {
  Senses& in = S.in;
  S.face.shear = approach(S.face.shear, clampf(in.tilt * 0.25f, -9.0f, 9.0f), dt, 0.15f);
  S.spiralRot += in.spin * dt * 0.0087f;
  S.scene.starShift += in.spin * dt * 0.15f;
  if (fabsf(in.spin) > 220.0f) M.spinHold += dt; else M.spinHold = maxf(0.0f, M.spinHold - dt * 2.0f);
  if (M.spinHold > 1.2f && S.t - M.spunSaid > 40.0f) { sayPool(L_SPUN, 6, 1); M.spunSaid = S.t; M.spinHold = 0; differ(0.05f); }
  if (in.freefall && !M.wasFreefall) {
    wake(); note(N_RUPTURE, 0, 1.0f); M.glitchPulse = 1.0f; M.shock += 0.35f;
    sayPool(L_FREEFALL, 5, 2); differ(0.15f); mark(2);
  }
  M.wasFreefall = in.freefall;
  if (in.motion > 0.7f) M.shakeHold += dt; else M.shakeHold = maxf(0.0f, M.shakeHold - dt);
  if (M.shakeHold > 0.5f) {
    M.scatterT = maxf(M.scatterT, 0.8f);
    if (S.t - M.shakenSaid > 25.0f) { sayPool(L_SHAKEN, 5, 1); M.shakenSaid = S.t; note(N_RUPTURE, 0, 0.6f); differ(0.05f); }
  }
  if (in.motion > 0.15f) wake();
}

static void tickKin() {
  Senses& in = S.in;
  if (in.peers > 0 && M.prevPeers == 0) {
    wake(); sayPool(L_KIN, 8, 2); note(N_CHIME_UP, deg(0), 0.9f);
    S.resonance = maxf(S.resonance, 0.6f); differ(0.3f); mark(1);
  } else if (in.peers == 0 && M.prevPeers > 0) {
    sayPool(L_KINLOST, 7, 1);
  }
  // two of them settle on one floor: the lower
  if (in.peers > 0 && in.peerRootHz > 60.0f && in.peerRootHz < M.root - 0.3f) M.root = in.peerRootHz;
  M.prevPeers = in.peers;
}

static bool lowBattery() { return S.in.battV > 2.5f && S.in.battV < 3.45f; }

// ----------------------------------------------------------------- the dials
static void tickAffect(float dt) {
  Senses& in = S.in;
  M.keyHeat = maxf(0.0f, M.keyHeat - dt / 4.0f);
  M.shock = maxf(0.0f, M.shock - M.shock * dt / 8.0f);
  if (M.shock > 1.0f) M.shock = 1.0f;

  float at = clampf(0.15f + in.loud * 0.6f + in.motion * 0.8f + M.keyHeat * 0.5f + S.presence * 0.08f + M.shock * 0.5f, 0, 1);
  S.arousal = approach(S.arousal, at, dt, at > S.arousal ? 1.5f : 12.0f);

  float u = 0.04f;
  if (in.haveWx) {
    u += clampf((-in.pressTrend - 1.0f) / 4.0f, 0, 1) * 0.35f;
    if (in.wmo >= 95) u += 0.35f;
  }
  if (in.apCount >= 15) u += 0.12f;
  static const float NEARNESS[5] = {0.0f, 0.04f, 0.14f, 0.26f, 0.40f};
  u += NEARNESS[S.presence < 5 ? S.presence : 4];
  if (witching()) u += 0.18f;
  if (M.skyValid && night()) { u += 0.05f; if (sky.moonPhase < 0.04f || sky.moonPhase > 0.96f) u += 0.08f; }
  if (lowBattery()) u += 0.25f;
  if (in.antenna >= 0) u += in.antenna * 0.4f;
  u += S.scene.fog * 0.15f + M.shock * 0.6f;
  if (in.peers > 0) u -= 0.1f;
  u -= S.resonance * 0.2f;
  S.unease = approach(S.unease, clampf(u, 0, 1), dt, 6.0f);

  S.resonance = maxf(0.0f, S.resonance - S.resonance * dt / 60.0f);
  S.openness = approach(S.openness, M.openTarget, dt, 5.0f);

  // difference: opens with novelty, closes with repetition
  differ(fabsf(M.loudFast - M.loudSlow) * 0.0015f * dt + in.motion * 0.002f * dt);
  S.difference -= (S.difference - 0.05f) * dt / 900.0f;

  if (lowBattery() && S.t - M.lowSaid > 300.0f) { sayPool(L_LOWBATT, 9, 2); M.lowSaid = S.t; mark(2); S.wantSave = true; }
}

static void tickSleep(float dt) {
  Senses& in = S.in;
  bool calm = in.motion < 0.05f && in.loud < 0.12f && M.keyHeat <= 0.0f;
  M.still = calm ? M.still + dt : maxf(0.0f, M.still - dt * 6.0f);
  float target = (M.still > 240.0f || in.faceDown) ? 1.0f : 0.0f;
  S.sleep = approach(S.sleep, target, dt, target > 0.5f ? (in.faceDown ? 1.0f : 6.0f) : 0.5f);
  bool asleep = S.sleep > 0.5f;
  if (asleep && !M.asleep) { M.asleepFor = 0; if (!in.faceDown) sayPool(L_SLEEP, 4, 0); S.wantSave = true; }
  if (!asleep && M.asleep) {
    if (M.asleepFor > 90.0f) { sayPool(L_WAKE, 6, 1); note(N_BELL, deg(0), 0.5f); }
    M.blinkT = 0.3f;
  }
  if (asleep) M.asleepFor += dt;
  M.asleep = asleep;
}

static void tickMuse(float dt) {
  Senses& in = S.in;
  M.nextMuse -= dt * (0.6f + S.arousal);
  if (M.nextMuse > 0) return;
  M.nextMuse = R.range(38.0f, 115.0f);
  bool speaking = S.say[0] && S.sayAge < S.sayHold + 6.0f;
  if (S.sleep > 0.3f || speaking || S.typed[0]) return;
  int cand[20]; int n = 0;
  if (M.skyValid) {
    if (sky.sunEl > 6.0f) cand[n++] = L_DAY;
    else if (night()) cand[n++] = L_NIGHT;
    else cand[n++] = sky.solarHour < 12.0f ? L_DAWN : L_DUSK;
    if (night() && sky.moonEl > 0 && sky.moonPhase > 0.46f && sky.moonPhase < 0.54f) cand[n++] = L_FULLMOON;
    if (night() && (sky.moonPhase < 0.04f || sky.moonPhase > 0.96f)) cand[n++] = L_NEWMOON;
    if (sky.tide > 0.6f) cand[n++] = L_HIGHTIDE;
    if (sky.tide < -0.6f) cand[n++] = L_LOWTIDE;
    if (witching()) { cand[n++] = L_WITCHING; cand[n++] = L_WITCHING; }
  }
  if (S.scene.rain > 0.2f) cand[n++] = L_RAIN;
  if (S.scene.fog > 0.3f) cand[n++] = L_FOG;
  if (S.scene.snow > 0.2f) cand[n++] = L_SNOW;
  if (in.haveWx) {
    if (in.windKmh > 30.0f) cand[n++] = L_WIND;
    if (in.wmo >= 95) cand[n++] = L_STORM;
    if (in.pressTrend < -1.5f) cand[n++] = L_PRESSURE;
    if (in.tempC < -2.0f) cand[n++] = L_COLD;
    if (in.tempC > 31.0f) cand[n++] = L_HOT;
  }
  if (in.apCount >= 15) cand[n++] = L_CROWDED;
  if (in.apCount == 0) cand[n++] = L_DEADAIR;
  if (M.still > 120.0f) cand[n++] = L_STILL;
  if (M.loudSlow > 0.5f) cand[n++] = L_LOUD;
  if (in.antenna > 0.3f) cand[n++] = L_ANTENNA;
  int pool = (n > 0 && R.chance(0.6f)) ? cand[R.irange(0, n - 1)] : L_GEN;
  sayPool(pool, 7.0f, 0);
}

// ------------------------------------------------------------------ the face
static void tickFace(float dt) {
  Senses& in = S.in; FaceP& f = S.face;
  bool asleep = S.sleep > 0.5f;

  M.blinkIn -= dt;
  if (M.blinkIn <= 0) { M.blinkT = 0.16f; M.blinkIn = R.range(1.8f, 7.0f) * (1.2f - S.arousal * 0.5f); }
  M.blinkT = maxf(0.0f, M.blinkT - dt);
  M.startle = maxf(0.0f, M.startle - dt);
  float open = 0.85f + S.arousal * 0.35f;
  if (M.startle > 0) open = 1.4f;
  if (S.sound.answerHz > 0) open = 0.55f;                       // listening hard
  open *= (1.0f - 0.93f * S.sleep);
  if (M.blinkT > 0) open *= 0.08f;
  f.eyeOpen = approach(f.eyeOpen, open, dt, 0.05f);

  M.gazeIn -= dt;
  if (M.gazeIn <= 0) {
    M.gazeIn = R.range(1.4f, 5.0f);
    if (R.chance(0.35f)) { M.gtx = 0; M.gty = 0; } else { M.gtx = R.range(-1.0f, 1.0f); M.gty = R.range(-0.6f, 0.6f); }
  }
  float gx = M.gtx + clampf(in.tilt * 0.02f, -0.5f, 0.5f), gy = M.gty;
  if (S.typed[0]) { gx = 0; gy = 0.8f; }                        // reading what is being typed
  if (in.flat && !in.faceDown) gy = -0.9f;                      // on its back: looking up
  if (asleep) { gx = 0; gy = 0.4f; }
  f.gazeX = approach(f.gazeX, clampf(gx, -1, 1), dt, 0.12f);
  f.gazeY = approach(f.gazeY, clampf(gy, -1, 1), dt, 0.12f);

  // speaking: the mouth moves and small syllables come out
  bool speaking = false;
  if (S.say[0] && S.sayAge < S.sayHold) {
    int len = (int)strlen(S.say);
    int shown = (int)(S.sayAge * (S.sayCipher ? 14.0f : 30.0f));
    speaking = shown < len;
    if (speaking && shown > M.lastShown) {
      for (int i = M.lastShown; i < shown && i < len; i++) {
        char ch = S.say[i];
        if (ch != ' ' && (i % 3) == 0) {
          float base = S.sayCipher ? 310.0f : 520.0f;
          note(N_VOICE, base * (1.0f + (hash32((uint32_t)ch * 97u + i) % 7) * 0.06f), 0.8f);
        }
      }
      M.lastShown = shown;
    }
  }
  f.mouthWave = approach(f.mouthWave, speaking ? 0.9f : 0.0f, dt, 0.08f);
  f.mouthOpen = approach(f.mouthOpen, S.sound.answerHz > 0 ? 1.0f : 0.0f, dt, 0.12f);
  f.mouthW = approach(f.mouthW, 1.0f - S.unease * 0.25f + S.resonance * 0.25f, dt, 0.8f);

  M.glitchPulse = maxf(0.0f, M.glitchPulse - dt / 0.7f);
  if (R.chance(dt * (0.03f + S.unease * 0.35f))) M.glitchPulse = maxf(M.glitchPulse, 0.5f);
  f.glitch = clampf(S.unease * 0.5f - 0.1f + M.glitchPulse + (lowBattery() ? 0.15f : 0.0f), 0, 1) * (1.0f - 0.7f * S.sleep);
  M.scatterT = maxf(0.0f, M.scatterT - dt);
  f.scatter = approach(f.scatter, clampf(M.scatterT, 0, 1), dt, 0.15f);
  f.bob = sinf(S.t * 1.1f) * 1.2f * (1.0f - S.sleep);
  f.fill = clampf(0.2f + M.loudFast * 0.35f, 0.2f, 0.6f);
  f.scraps = 2 + (int)(S.difference * 7.0f + 0.5f); if (f.scraps > 9) f.scraps = 9;
  f.scrapLift = clampf(0.2f + S.arousal * 0.6f + S.resonance * 0.2f, 0, 1) * (1.0f - 0.8f * S.sleep);
  f.scrapSpin += dt * (0.2f + S.arousal);
  f.invert = S.scene.flash > 0.8f;

  SceneP& sc = S.scene;
  static const float GREEN[5] = {0.0f, 0.08f, 0.3f, 0.55f, 1.0f};
  sc.green = approach(sc.green, GREEN[S.presence < 5 ? S.presence : 4], dt, 2.0f);
  sc.dread = approach(sc.dread, clampf((S.unease - 0.3f) * 1.3f, 0, 1), dt, 2.0f);
  sc.wisps = approach(sc.wisps, clampf(sc.fog * 0.8f + (witching() ? 0.5f : 0.0f) + (S.presence == 3 ? 0.4f : 0.0f), 0, 1), dt, 4.0f);
  sc.starShift += dt * 0.02f;
}

// ---------------------------------------------- the skyline that sound draws
static void tickRidge(float dt) {
  Senses& in = S.in;
  S.ridgeAcc += dt * (5.0f + S.arousal * 16.0f + clampf(in.speedKmh, 0, 120) * 0.25f) * (1.0f - 0.9f * S.sleep);
  while (S.ridgeAcc >= 1.0f) {
    S.ridgeAcc -= 1.0f;
    memmove(S.ridge, S.ridge + 1, W - 1); memmove(S.ridgeMark, S.ridgeMark + 1, W - 1);
    uint32_t col = S.frame + (uint32_t)(S.t * 31.0f);
    int band = (int)((col / 9) % 8);
    float target = 2.0f + in.bands[band] * 16.0f + in.loud * 7.0f + (hash32(col) & 3);
    M.ridgeH += (target - M.ridgeH) * 0.35f;
    S.ridge[W - 1] = (uint8_t)clampf(M.ridgeH, 0.0f, 30.0f);
    uint8_t mk = 0;
    if (M.markNext) { mk = M.markNext; M.markNext = 0; }
    else if ((R.next() % 70) == 0) mk = (uint8_t)(R.chance(0.2f) ? 3 : R.irange(1, 2));
    S.ridgeMark[W - 1] = mk;
  }
}

// --------------------------------------------------------------- having-been
static void tickSpiral(float dt) {
  Life& L = life();
  S.spiralAcc += dt;
  if (S.spiralAcc < L.spiralInterval) return;
  S.spiralAcc = 0;
  if (S.spiralLen >= SPIRAL_N) {                         // full: fold the past in half, and slow down
    for (int i = 0; i < SPIRAL_N / 2; i++) {
      S.spiral[i] = (uint8_t)((S.spiral[i * 2] + S.spiral[i * 2 + 1]) / 2);
      uint8_t a = S.spiralEv[i * 2], b = S.spiralEv[i * 2 + 1];
      S.spiralEv[i] = a > b ? a : b;
    }
    S.spiralLen = SPIRAL_N / 2;
    L.spiralInterval *= 2.0f;
  }
  S.spiral[S.spiralLen] = (uint8_t)(clampf(S.difference, 0, 1) * 255.0f);
  S.spiralEv[S.spiralLen] = M.pendEv; M.pendEv = 0;
  S.spiralLen++; S.spiralTotal++;
}

// ------------------------------------------------------------------ the voice
static void tickSound(float dt) {
  Senses& in = S.in; SoundP& p = S.sound;
  M.modeHold -= dt;
  uint8_t want = 0;
  if (S.presence >= 3) want = 4;
  else if ((in.haveWx && in.wmo >= 95) || S.unease > 0.62f) want = 2;
  else if (S.unease > 0.38f) want = 1;
  else if (S.resonance > 0.5f || (S.openness > 0.7f && S.unease < 0.2f)) want = 3;
  if (want != M.mode && M.modeHold <= 0) { M.mode = want; M.modeHold = 20.0f; }
  p.mode = M.mode; p.rootHz = M.root;
  p.drone = 0.72f * (1.0f - 0.5f * M.loudFast);          // when the world speaks, it leans back to listen
  p.lead = clampf(0.25f + S.arousal * 0.3f + S.resonance * 0.3f + (M.skyValid && night() ? 0.1f : 0.0f) - S.sleep * 0.3f, 0, 1);
  p.unease = S.unease;
  p.open = clampf(S.openness - S.scene.snow * 0.25f, 0, 1);
  p.wind = in.haveWx ? clampf(in.windKmh / 45.0f, 0, 1) : 0.06f;
  p.rain = S.scene.rain;
  p.crackle = clampf(S.unease * 0.5f + (S.presence > 1 ? (S.presence - 1) * 0.12f : 0.0f) + (in.apCount >= 15 ? 0.1f : 0.0f) + (lowBattery() ? 0.3f : 0.0f), 0, 1);
  float hush = maxf(S.sleep * 0.8f, in.faceDown ? 1.0f : 0.0f);
  if (S.presence == 4 && S.placeKind == 11) hush = 1.0f;  // some places ask for silence
  p.hush = hush;
}

static void tickLight(float dt) {
  M.beat += dt * (0.75f + S.arousal * 0.9f);
  if (M.beat >= 1.0f) M.beat -= 1.0f;
  bool awake = S.sleep < 0.5f;
  S.ir = S.presence == 4 || (awake && (M.beat < 0.07f || (M.beat > 0.18f && M.beat < 0.24f)));
  float breathe = 0.5f + 0.5f * sinf(S.t * (0.5f + S.arousal * 1.5f));
  float u = clampf(S.unease * 1.2f, 0, 1);
  float r = 0x6C + (0xD8 - 0x6C) * u, g = 0xF0 + (0x5A - 0xF0) * u, b = 0xA0 + (0x28 - 0xA0) * u;
  float bright = (0.02f + 0.07f * breathe) * (1.0f - 0.92f * S.sleep);
  if (S.presence > 1 && S.presence < 4 && sinf(S.t * 3.0f * S.presence) > 0.6f) bright += 0.12f;
  if (S.presence == 4) { r = 0x6C; g = 0xF0; b = 0xA0; bright = 0.25f + 0.2f * breathe; }
  if (lowBattery()) { r = 0x7A; g = 0x12; b = 0x18; bright = 0.05f + 0.1f * breathe; }
  if (S.scene.flash > 0.5f) { r = g = b = 255; bright = 0.5f; }
  if (!awake && S.presence < 4) bright = fmodf(S.t, 9.0f) < 0.12f ? 0.04f : 0.0f;
  S.ledR = (uint8_t)(r * bright); S.ledG = (uint8_t)(g * bright); S.ledB = (uint8_t)(b * bright);
}

void mindTick() {
  Senses& in = S.in;
  float dt = clampf(in.dt, 0.001f, 0.25f);
  S.t += dt; S.frame++; S.hintAge += dt; S.sayAge += dt;
  for (Later& l : M.later) if (l.on && S.t >= l.at) { S.notes.push(l.n); l.on = false; }
  tickSky(dt); tickWeather(dt); tickPlace(dt); tickEther(); tickHearing(dt); tickBody(dt); tickKin();
  tickAffect(dt); tickSleep(dt); tickMuse(dt); tickFace(dt); tickRidge(dt); tickSpiral(dt); tickSound(dt); tickLight(dt);
  Life& L = life();
  S.cellsKnown = L.cellCount; S.placesFound = L.foundCount; S.linesCrossed = L.linesCrossed;
}

// ---------------------------------------------------------------- being touched
static void strike() {
  if (S.muted) { mindSay(lexVerdict(6), 5.0f, 2, false); return; }
  S.wantSounding = true;
}

void mindSounding(const Sounding& s) {
  S.sounding = s;
  if (!s.valid) { mindSay(lexVerdict(5), 6.0f, 2, false); return; }
  static const float OPEN[5] = {0.55f, 0.12f, 0.32f, 0.62f, 0.92f};
  M.openTarget = OPEN[s.verdict < 5 ? s.verdict : 0];
  mindSay(lexVerdict(s.verdict), 7.0f, 2, false);
  life().soundings++; differ(0.08f); mark(1);
}

void mindButton(bool longPress) {
  wake(); S.hintAge = 99;
  if (longPress) {
    S.muted = !S.muted; life().muted = S.muted ? 1 : 0; S.wantSave = true;
    mindSay(S.muted ? "muted." : "i can speak again.", 3.0f, 2, false);
    if (!S.muted) note(N_BELL, deg(0), 0.6f);
  } else strike();
}

void mindKinScrap(const char* s) {
  if (!s || !s[0]) return;
  char clean[LIFE_SCRAPLEN]; int n = 0;
  for (const char* p = s; *p && n < LIFE_SCRAPLEN - 1; p++) if (*p >= 0x20 && *p <= 0x7E) clean[n++] = *p;
  clean[n] = 0;
  if (!n) return;
  scrapKeep(clean);
  char b[84]; snprintf(b, sizeof(b), "the other one says: %s", clean);
  mindSay(b, 8.0f, 2, false);
}

const char* mindScrapToGive() {
  int n = 0; while (scrapAt(n)) n++;
  if (!n) return "";
  return scrapAt((int)(R.next() % (uint32_t)n));
}

static void setView(int v) {
  S.view = (View)(((v % V_COUNT) + V_COUNT) % V_COUNT);
  note(N_PLUCK, deg(S.view * 2), 0.5f);
}

static void command(char* line) {
  // trim + lowercase copy for matching
  char lc[48]; int n = 0;
  for (char* p = line; *p && n < 47; p++) lc[n++] = (char)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
  while (n > 0 && lc[n - 1] == ' ') n--;
  lc[n] = 0;
  char* w = lc; while (*w == ' ') w++;
  Life& L = life();

  if (!strcmp(w, "help") || !strcmp(w, "?")) {
    static const char* const HELP[4] = {
      "type to it. enter alone strikes the room. tab turns the view.",
      "words it obeys: wifi. mute. vol 0-9. spiral. chart. ear. shore.",
      "hum and hold. knock a rhythm. carry it. forget, twice, erases.",
      "format, twice, erases a card it cannot read and makes it its own."};
    mindSay(HELP[M.helpN % 4], 9.0f, 2, false); M.helpN++; return;
  }
  if (!strcmp(w, "wifi")) { S.wantPortal = true; mindSay("opening a door.", 4.0f, 2, false); return; }
  if (!strcmp(w, "mute") || !strcmp(w, "hush")) { S.muted = true; L.muted = 1; S.wantSave = true; mindSay("muted.", 3.0f, 2, false); return; }
  if (!strcmp(w, "unmute") || !strcmp(w, "speak")) { S.muted = false; L.muted = 0; S.wantSave = true; mindSay("i can speak again.", 3.0f, 2, false); note(N_BELL, deg(0), 0.6f); return; }
  if (!strncmp(w, "vol", 3) && (w[3] == 0 || w[3] == ' ')) {
    const char* a = w + 3; while (*a == ' ') a++;
    if (*a >= '0' && *a <= '9') { S.sound.master = (*a - '0') / 9.0f; S.muted = false; L.muted = 0; S.wantSave = true; note(N_BELL, deg(0), 0.7f); }
    char b[24]; snprintf(b, sizeof(b), "volume %d.", (int)(S.sound.master * 9.0f + 0.5f));
    mindSay(b, 3.0f, 2, false); return;
  }
  if (!strcmp(w, "forget")) {
    if (S.t - M.forgetArmed < 20.0f) {
      lifeReset(); L.wakes = 1; mindLifeIn();
      S.spiralLen = 0; S.spiralTotal = 0; S.difference = 0.5f; M.forgetArmed = -1.0e6f;
      S.wantForget = true; note(N_RUPTURE, 0, 1.0f); M.scatterT = 1.5f;
      mindSay("gone. nothing has been, yet.", 7.0f, 3, false);
    } else { M.forgetArmed = S.t; mindSay("say it again and i let go of everything.", 7.0f, 3, false); }
    return;
  }
  if (!strcmp(w, "format")) {
    if (S.in.card == 1) { mindSay("the card is already mine.", 4.0f, 2, false); return; }
    if (S.in.card != 2) { mindSay("there is no card here to make mine.", 4.0f, 2, false); return; }
    if (S.t - M.formatArmed < 20.0f) {
      M.formatArmed = -1.0e6f; S.wantFormat = true;
      mindSay("making the card mine. this takes a while.", 9.0f, 3, false);
    } else {
      M.formatArmed = S.t;
      char b[96];
      snprintf(b, sizeof(b), "a %d gb card i cannot read. say format again and all of it is erased.", (int)(S.in.cardGB + 0.5f));
      mindSay(b, 9.0f, 3, false);
    }
    return;
  }
  if (!strcmp(w, "sound") || !strcmp(w, "strike")) { strike(); return; }
  if (!strcmp(w, "spiral")) { setView(V_SPIRAL); return; }
  if (!strcmp(w, "chart")) { setView(V_CHART); return; }
  if (!strcmp(w, "ear") || !strcmp(w, "sounding")) { setView(V_SOUNDING); return; }
  if (!strcmp(w, "shore") || !strcmp(w, "back")) { setView(V_SHORE); return; }
  if (!strcmp(w, "save")) { S.wantSave = true; mindSay("kept.", 3.0f, 2, false); return; }

  // anything else is offered to it
  OracleCtx c;
  c.sky = &sky; c.haveTime = S.in.haveTime; c.haveFix = S.in.haveFix; c.haveWx = S.in.haveWx;
  c.lat = S.in.lat; c.lon = S.in.lon; c.tempC = S.in.tempC; c.wmo = S.in.wmo; c.tide = sky.tide;
  c.presence = S.presence; c.found = L.foundCount; c.cells = L.cellCount; c.turns = S.spiralTotal / 48;
  char out[84];
  oracle(w, c, R.next(), out, sizeof(out));
  scrapKeep(w);
  L.words++; differ(0.03f);
  if (++M.lines % 3 == 0) S.wantSave = true;
  mindSay(out, 7.5f, 2, false);
}

void mindKey(int c) {
  wake(); S.hintAge = 99; M.keyHeat = 1.0f;
  int len = (int)strlen(S.typed);
  if (c == K_ENTER) {
    if (len == 0) { strike(); return; }
    char line[48]; strncpy(line, S.typed, sizeof(line) - 1); line[sizeof(line) - 1] = 0;
    S.typed[0] = 0;
    note(N_PLUCK, deg(0), 0.7f); later(0.07f, N_PLUCK, deg(4), 0.6f);
    command(line);
    return;
  }
  if (c == K_BACK) { if (len > 0) { S.typed[len - 1] = 0; note(N_PLUCK, deg(-3), 0.35f); } return; }
  if (c == K_ESC) {
    if (len > 0) S.typed[0] = 0; else if (S.view != V_SHORE) setView(V_SHORE); else S.sayAge = 99;
    return;
  }
  if (c == K_TAB || c == K_VIEW_NEXT) { setView(S.view + 1); return; }
  if (c == K_VIEW_PREV) { setView(S.view - 1); return; }
  if (c == K_VOL_UP || c == K_VOL_DOWN) {
    float v = S.sound.master + (c == K_VOL_UP ? 0.111f : -0.111f);
    S.sound.master = clampf(v, 0.0f, 1.0f); life().volume = S.sound.master;
    if (c == K_VOL_UP) S.muted = false;
    char b[24]; snprintf(b, sizeof(b), "volume %d.", (int)(S.sound.master * 9.0f + 0.5f));
    mindSay(b, 1.5f, 2, false); note(N_PLUCK, deg(0), 0.8f);
    return;
  }
  if (c >= 0x20 && c <= 0x7E && len < (int)sizeof(S.typed) - 1) {
    S.typed[len] = (char)c; S.typed[len + 1] = 0;
    note(N_PLUCK, deg((c * 7 + len) % 9), 0.55f);
    differ(0.001f);
  }
}

}  // namespace mn
