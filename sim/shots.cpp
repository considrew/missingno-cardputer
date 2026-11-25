// Host-side look at the renderer: writes PPM frames for a set of staged scenes.
#include "../missingno/src/core/mn_draw.h"
#include "../missingno/src/core/mn_horizon.h"
#include "../missingno/src/core/mn_pro.h"
#include "../missingno/src/core/mn_theremin.h"
#include <vector>
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
static std::vector<uint8_t> hz;
static bool hzRead(uint32_t off, void* dst, uint32_t n) {
  if ((size_t)off + n > hz.size()) return false;
  memcpy(dst, hz.data() + off, n); return true;
}
int main(int argc, char** argv) {
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
  // 15.. the real horizon, if a horizon.bin is given: sunset alpenglow, moonrise over it, day, haze
  if (argc > 1) {
    FILE* f = fopen(argv[1], "rb");
    if (f) { int c; while ((c = fgetc(f)) != EOF) hz.push_back((uint8_t)c); fclose(f); }
    if (horizonOpen(hzRead, (uint32_t)hz.size())) {
      double lat = argc > 3 ? atof(argv[2]) : 0, lon = argc > 3 ? atof(argv[3]) : 0;
      if (argc <= 3) {   // the middle of the grid
        lat = ((int32_t)(hz[8] | hz[9] << 8 | hz[10] << 16 | hz[11] << 24) + (hz[24] / 2) * (int32_t)(hz[16] | hz[17] << 8 | hz[18] << 16 | hz[19] << 24)) * 1e-6;
        lon = ((int32_t)(hz[12] | hz[13] << 8 | hz[14] << 16 | hz[15] << 24) + (hz[26] / 2) * (int32_t)(hz[20] | hz[21] << 8 | hz[22] << 16 | hz[23] << 24)) * 1e-6;
      }
      horizonAt(true, lat, lon);
      S = State(); ridge(); S.t = 50; S.frame = 1500; S.hintAge = 99;
      S.scene.sunEl = 1.8f; S.scene.sunAz = 238; S.scene.moonEl = -10; S.scene.cloud = 0.15f; S.scene.tide = 0.2f;
      drawFrame(); dump("15_hz_alpenglow");
      S.scene.sunEl = -20; S.scene.moonEl = 1.6f; S.scene.moonAz = 151; S.scene.moonPhase = 0.5f; S.scene.cloud = 0.05f;
      drawFrame(); dump("16_hz_moonrise");
      S.scene.sunEl = 30; S.scene.sunAz = 200; S.scene.moonEl = -10; S.scene.cloud = 0.3f;
      drawFrame(); dump("17_hz_day");
      S.in.haveWx = true; S.in.visKm = 14; S.scene.cloud = 0.7f; S.scene.fog = 0.2f;
      drawFrame(); dump("18_hz_haze");
      S = State();
    }
  }
  // 20.. pro: every reach, then some tools opened
  {
    S = State(); S.t = 4000; S.frame = 99000; S.hintAge = 99; S.pro = true; S.proAge = 9;
    Senses& in = S.in;
    in.haveTime = true; in.utc = 1795548600.0; in.haveTz = true; in.utcOffset = -8 * 3600;
    in.haveFix = true; in.gpsFix = true; in.lat = 47.6062; in.lon = -122.3321; in.sats = 9; in.altM = 61; in.speedKmh = 4.2f;
    in.haveWx = true; in.wmo = 61; in.cloud = 0.85f; in.tempC = 7.4f; in.humid = 86; in.pressHpa = 1006.8f; in.pressTrend = -1.9f; in.windKmh = 24; in.windDir = 205; in.visKm = 11;
    in.apCount = 19; in.apRssi = -44; in.etherNovelty = 0.12f; in.etherVisits = 31; in.peers = 0;
    in.battV = 3.93f; in.online = true; in.ip[0] = 192; in.ip[1] = 168; in.ip[2] = 1; in.ip[3] = 40; in.wifiRssi = -58; in.ota = true; in.heapFree = 143000;
    in.store = 1; in.card = 1; in.cardGB = 62.2f; in.antenna = -1; in.steps = 2304;
    in.pitchHz = 221.6f; in.floorDb = -63; in.grav[0] = 0.03f; in.grav[1] = 0.99f; in.grav[2] = -0.05f; in.tilt = 1.7f; in.spin = 12;
    for (int b = 0; b < 8; b++) in.bands[b] = 0.25f + 0.5f * fabsf(sinf(b * 0.8f + 0.4f));
    S.scene.sunEl = 9.5f; S.scene.sunAz = 211; S.scene.moonEl = 22; S.scene.moonAz = 96; S.scene.moonPhase = 0.18f; S.scene.tide = 0.42f;
    S.sounding.valid = true; S.sounding.tailMs = 412; S.sounding.verdict = 3; S.sounding.floorDb = -64;
    for (int i = 0; i < 60; i++) S.sounding.env[i] = (uint8_t)(250 * expf(-i / 16.0f) + 10);
    S.solarHour = 12.9f; S.sound.rootHz = 98; S.cellsKnown = 212; S.linesCrossed = 3; S.placesFound = 2;
    for (int i = 0; i < 400; i++) {                         // some history
      in.levelDb = -52 + 14 * sinf(i * 0.07f) + 6 * sinf(i * 0.31f); in.accG = 1.0f + 0.25f * fabsf(sinf(i * 0.2f)) * (i > 300);
      in.onset = (i % 21) == 0 && i > 330; in.etherFresh = (i % 40) == 0; in.apCount = 14 + (i / 40) % 6;
      S.t += 1 / 30.0f; proTick(1 / 30.0f);
    }
    in.onset = false; in.etherFresh = false; in.levelDb = -41; in.accG = 1.02f;
    static const char* const BN[8] = {"self", "hand", "room", "rf", "air", "land", "sat", "sky"};
    for (int b = 0; b < 8; b++) {
      S.proBand = b; S.proTool = 0; S.proOpen = false;
      char nm[40]; snprintf(nm, sizeof(nm), "2%d_pro_%s", b, BN[b]); drawFrame(); dump(nm);
    }
    static const char* const OPEN[] = {"meter", "tuner", "level", "echo", "horizon", "sun", "wind", "moon"};
    for (int i = 0; i < 8; i++) {
      proGoto(OPEN[i]);
      char nm[40]; snprintf(nm, sizeof(nm), "3%d_open_%s", i, OPEN[i]); drawFrame(); dump(nm);
    }
    strcpy(S.typed, "pro tun"); S.proOpen = false; S.proBand = 2; S.proTool = 1; drawFrame(); dump("38_pro_typing");
    S.typed[0] = 0; strcpy(S.say, "the land rises to the south-east. the highest of it is 87 km away."); S.sayAge = 4; S.sayHold = 9; drawFrame(); dump("39_pro_speaking");
    S = State();
  }
  // 40.. the theremin: a slide with a tremble, a phrase in its scale, held an octave up; the hand reach in pro
  {
    S = State(); S.t = 300; S.frame = 9000; S.hintAge = 99; S.sound.rootHz = 110;
    auto pose = [](float roll, float lean, float spin) {
      float r = roll * 0.0174533f, l = lean * 0.0174533f;
      S.in.acc[0] = sinf(r); S.in.acc[1] = cosf(r) * sinf(l); S.in.acc[2] = cosf(r) * cosf(l);
      S.in.spin = spin; S.in.gyro[2] = spin;
    };
    thereminStart();
    for (int f = 0; f < 140; f++) {
      float t = f / 30.0f;
      pose(t < 2.5f ? -30 + 26 * t : 35 + 4 * sinf(t * 30.0f), 18 + 10 * sinf(t), t > 2.5f ? 160 : 0);
      S.t += 1 / 30.0f; S.frame++; thereminTick(1 / 30.0f);
    }
    S.thereAge = 30; drawFrame(); dump("40_theremin_glide");
    S.thereScale = 2; S.sound.mode = 1;
    static const float PH[] = {0, 2, 3, 5, 7, 8, 7, 5, 3, 2, 0, -2};
    for (int f = 0; f < 140; f++) {
      int k = (f / 12) % 12;
      pose(PH[k] / 18 * 55 + 1.5f * sinf(f * 0.4f), 22, 0);
      S.t += 1 / 30.0f; S.frame++; thereminTick(1 / 30.0f);
    }
    drawFrame(); dump("41_theremin_mood");
    S.thereOct = 1; S.thereHold = true;
    for (int f = 0; f < 60; f++) { pose(-40, 70, 0); S.t += 1 / 30.0f; S.frame++; thereminTick(1 / 30.0f); }
    drawFrame(); dump("42_theremin_held");
    S.thereAge = 2; S.thereHold = false; S.thereOct = 0; S.thereScale = 0;
    drawFrame(); dump("43_theremin_first");
    thereminStop();
    S.pro = true; S.proAge = 9; S.proBand = 1; S.proTool = 4; S.in.steps = 412; S.in.accG = 1.01f; S.in.grav[1] = 0.98f; S.in.antenna = -1;
    drawFrame(); dump("44_pro_hand");
    S = State();
  }
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
