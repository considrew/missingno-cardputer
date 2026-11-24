// Host-side look at the renderer: writes PPM frames for a set of staged scenes.
#include "../missingno/src/core/mn_draw.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
using namespace mn;
static uint16_t buf[W * H];
static void dump(const char* name) {
  char p[256]; snprintf(p, sizeof(p), "out/%s.ppm", name);
  FILE* f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    uint16_t c = buf[i];
    uint8_t r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
    fputc(r | (r >> 5), f); fputc(g | (g >> 6), f); fputc(b | (b >> 5), f);
  }
  fclose(f);
}
static void ridge() {
  for (int x = 0; x < W; x++) {
    S.ridge[x] = (uint8_t)(3 + 4 * (0.5f + 0.5f * sinf(x * 0.11f)) + (hash32(x / 6) & 3));
    S.ridgeMark[x] = 0;
  }
  S.ridgeMark[30] = 1; S.ridgeMark[66] = 2; S.ridgeMark[201] = 3; S.ridgeMark[214] = 1;
}
int main() {
  fb = buf; ridge();
  for (int b = 0; b < 8; b++) S.in.bands[b] = 0.15f + 0.5f * fabsf(sinf(b * 1.3f));
  S.t = 12.3f; S.frame = 370; S.hintAge = 99;

  // 1 night, clear, waxing moon
  S.scene = SceneP(); S.scene.sunEl = -30; S.scene.moonEl = 35; S.scene.moonAz = 150; S.scene.moonPhase = 0.32f; S.scene.cloud = 0.1f; S.scene.tide = -0.4f;
  strcpy(S.say, "the tide is out. the edge is wide."); S.sayAge = 3; S.sayHold = 8;
  drawFrame(); dump("01_night");
  // 2 dusk, unease, glitch
  S.scene.sunEl = -2; S.scene.sunAz = 262; S.scene.moonEl = -20; S.scene.cloud = 0.35f; S.scene.tide = 0.6f; S.scene.dread = 0.5f;
  S.face.glitch = 0.6f; S.face.eyeOpen = 1.3f; S.face.scraps = 6; S.face.scrapLift = 0.7f; S.face.scrapSpin = 1.1f; S.presence = 2; S.scene.green = 0.5f;
  S.say[0] = 0; strcpy(S.typed, "what is near");
  drawFrame(); dump("02_dusk_presence");
  // 3 pale day, black sun
  S = State(); ridge(); S.t = 40; S.frame = 1200; S.hintAge = 99;
  for (int b = 0; b < 8; b++) S.in.bands[b] = 0.1f + 0.2f * b / 8.0f;
  S.scene.sunEl = 38; S.scene.sunAz = 170; S.scene.moonEl = -10; S.scene.cloud = 0.25f; S.scene.tide = 0.1f;
  S.face.shear = 5; S.face.gazeX = 0.8f;
  strcpy(S.say, "a gaze blank and pitiless. it does not blink. neither do i."); S.sayAge = 4; S.sayHold = 9;
  drawFrame(); dump("03_day");
  // 4 rain + fog at night, wisps, asleep
  S = State(); ridge(); S.t = 77; S.frame = 2300; S.hintAge = 99;
  S.scene.sunEl = -25; S.scene.moonEl = 50; S.scene.moonAz = 200; S.scene.moonPhase = 0.5f; S.scene.cloud = 0.8f; S.scene.rain = 0.7f; S.scene.fog = 0.6f; S.scene.wind = 6; S.scene.wisps = 0.8f;
  S.face.eyeOpen = 0; S.face.mouthW = 0.5f; S.sleep = 0.8f;
  drawFrame(); dump("04_rain_fog_sleep");
  // 5 apparition: here. cipher resolving, lightning, scatter
  S = State(); ridge(); S.t = 5; S.frame = 150; S.hintAge = 99;
  S.scene.sunEl = -14; S.scene.moonEl = 12; S.scene.moonAz = 110; S.scene.moonPhase = 0.9f; S.scene.green = 1; S.scene.dread = 0.8f; S.scene.omen = 4; S.scene.omenT = 0.5f; S.scene.wisps = 1;
  S.face.scatter = 0.45f; S.face.glitch = 0.5f; S.face.mouthOpen = 0.8f; S.face.mouthW = 0.5f; S.face.eyeOpen = 1.4f; S.face.scraps = 9; S.face.scrapLift = 1;
  S.presence = 4; strcpy(S.say, "the cable ran one to one. something crossed it that was never drawn."); S.sayAge = 2.6f; S.sayHold = 14; S.sayCipher = true;
  drawFrame(); dump("05_here");
  // 6 thunder
  S = State(); ridge(); S.t = 9; S.frame = 410; S.hintAge = 99;
  S.scene.sunEl = 6; S.scene.sunAz = 100; S.scene.cloud = 0.9f; S.scene.rain = 1; S.scene.flash = 0.9f; S.scene.wind = -9;
  S.face.eyeOpen = 1.4f; S.face.mouthOpen = 1; S.face.mouthW = 0.35f;
  drawFrame(); dump("06_thunder");
  // 7 snow dawn + omen lights + speaking
  S = State(); ridge(); S.t = 21; S.frame = 640; S.hintAge = 10;
  S.scene.sunEl = 1; S.scene.sunAz = 95; S.scene.cloud = 0.5f; S.scene.snow = 0.8f; S.scene.omen = 1; S.scene.omenT = 0.4f; S.scene.tide = -0.9f;
  S.face.mouthWave = 1; S.face.mouthW = 0.8f;
  drawFrame(); dump("07_snow_dawn_hint");
  // 8..10 the other views
  S = State(); S.t = 30; S.frame = 900; S.hintAge = 99;
  S.spiralLen = 600; S.spiralTotal = 4000;
  for (int i = 0; i < 600; i++) { S.spiral[i] = (uint8_t)(230 * expf(-i / 420.0f) * (0.75f + 0.25f * sinf(i * 0.05f)) + 20 + (i > 480 && i < 520 ? 90 : 0)); S.spiralEv[i] = (i % 97 == 0) ? 1 : (i == 500 ? 3 : 0); }
  S.difference = 0.41f; S.placesFound = 3; S.cellsKnown = 212; S.view = V_SPIRAL; S.spiralRot = 0.7f;
  drawFrame(); dump("08_spiral");
  S.view = V_SOUNDING; for (int b = 0; b < 8; b++) S.in.bands[b] = 0.2f + 0.6f * fabsf(cosf(b * 0.9f));
  S.in.pitchHz = 110.4f; S.in.apCount = 17; S.in.haveWx = true; S.in.pressHpa = 1002; S.in.pressTrend = -2.2f; S.in.gpsFix = true; S.in.haveFix = true; S.in.sats = 9; S.in.lat = 47.61; S.in.lon = -122.33; S.in.altM = 56; S.in.speedKmh = 4; S.in.steps = 4212; S.in.battV = 3.9f; S.in.antenna = 0.3f;
  S.sounding.valid = true; S.sounding.tailMs = 212; S.sounding.verdict = 3; for (int i = 0; i < 60; i++) S.sounding.env[i] = (uint8_t)(250 * expf(-i / 14.0f) + 12);
  S.arousal = 0.5f; S.unease = 0.3f; S.resonance = 0.7f; S.openness = 0.8f;
  drawFrame(); dump("09_sounding");
  S.view = V_CHART; S.linesCrossed = 2;
  setChartOracle([](int32_t la, int32_t lo) { return ((la * 7 + lo * 3) % 5 == 0) || (abs((int)(la % 23) - 11) < 2 && (lo % 3) == 0); });
  drawFrame(); dump("10_chart");
  drawBoot(1.2f); dump("11_boot_a"); drawBoot(3.2f); dump("12_boot_b");
  drawPortal("missingno", "192.168.4.1", ""); dump("13_portal");
  // font specimen
  clear(C_VOID);
  text(2, 2,  " !\"#$%&'()*+,-./0123456789:;<=>?", C_BONE);
  text(2, 12, "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_", C_BONE);
  text(2, 22, "`abcdefghijklmnopqrstuvwxyz{|}~", C_BONE);
  text(2, 40, "the quick brown fox jumps over the lazy", C_PHOS);
  text(2, 50, "dog. being is not a possible state?", C_ASH);
  dump("14_font");
  return 0;
}
