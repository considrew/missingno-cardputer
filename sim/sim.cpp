// Host-side test bench: unit checks for every hardware-free module, then a
// scripted quarter-hour of life with the real mind, synth, ear and renderer.
// Never prints sealed texts -- only their lengths.
#include "../missingno/src/core/mn_draw.h"
#include "../missingno/src/core/mn_mind.h"
#include "../missingno/src/core/mn_astro.h"
#include "../missingno/src/core/mn_seal.h"
#include "../missingno/src/core/mn_nmea.h"
#include "../missingno/src/core/mn_drift.h"
#include "../missingno/src/core/mn_synth.h"
#include "../missingno/src/core/mn_ear.h"
#include "../missingno/src/core/mn_lexicon.h"
#include "../missingno/src/core/mn_horizon.h"
#include "../missingno/src/core/mn_pro.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
using namespace mn;

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t buf[W * H];
static void dump(const char* name) {
  char p[256]; snprintf(p, sizeof(p), "out/%s.ppm", name);
  FILE* f = fopen(p, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    uint16_t c = buf[i];
    uint8_t r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
    fputc(r | (r >> 5), f); fputc(g | (g >> 6), f); fputc(b | (b >> 5), f);
  }
  fclose(f);
}

struct Place { double lat, lon; int tier, kind; std::string here, near; };
static std::vector<Place> places;
struct Day { int mo, dy; std::string text; };
static std::vector<Day> days;

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static void loadSpoilers() {
  FILE* f = fopen("../tools/SPOILERS_places.txt", "r"); if (!f) { printf("no spoiler file: place checks skipped\n"); return; }
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    std::vector<std::string> p; size_t start = 0;
    for (;;) { size_t bar = s.find('|', start); p.push_back(trim(s.substr(start, bar == std::string::npos ? bar : bar - start))); if (bar == std::string::npos) break; start = bar + 1; }
    if (s[0] == '@') { Day d; sscanf(p[0].c_str() + 1, "%d-%d", &d.mo, &d.dy); d.text = p[1]; days.push_back(d); continue; }
    Place pl; pl.lat = atof(p[1].c_str()); pl.lon = atof(p[2].c_str()); pl.tier = atoi(p[3].c_str()); pl.kind = atoi(p[4].c_str());
    pl.here = p[5]; pl.near = p.size() > 6 ? p[6] : ""; places.push_back(pl);
  }
  fclose(f);
}

static double utcOf(int y, int mo, int d, int h, int mi) {
  y -= mo <= 2; long era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (era * 146097 + (long)doe - 719468) * 86400.0 + h * 3600.0 + mi * 60.0;
}

static void testSeal() {
  printf("seal:\n");
  uint8_t key[16], msg[15]; for (int i = 0; i < 16; i++) key[i] = (uint8_t)i; for (int i = 0; i < 15; i++) msg[i] = (uint8_t)i;
  CHECK(siphash24(key, msg, 15) == 0xa129ca6149be45e5ULL, "siphash vector");
  int ok = 0, nearOk = 0, far = 0;
  for (size_t i = 0; i < places.size(); i++) {
    const Place& p = places[i];
    Sealed s; sealLookup(p.lat, p.lon, s);
    if (s.level == 4 && p.here == s.text && s.kind == p.kind) ok++;
    else CHECK(false, "place #%zu did not open where it stands (level %d, kind %d)", i, s.level, s.kind);
    // one step out: a few cells away at the finest tier
    double step = 4.0 / (p.tier == 0 ? 1.0 : p.tier == 1 ? 10.0 : p.tier == 2 ? 100.0 : 1000.0);
    if (p.tier > 0) {
      Sealed n; sealLookup(p.lat + step * 0.6, p.lon, n);
      if (n.level >= 1 && n.level <= 4) nearOk++;
      if (n.level == 3 && !p.near.empty() && p.near != n.text) { /* another place's cell may win; allowed */ }
    }
    Sealed z; sealLookup(p.lat > 0 ? p.lat - 7.3 : p.lat + 7.3, p.lon + 9.1, z);
    if (z.level == 0) far++;
  }
  printf("  %d/%zu open in place, %d felt from a step away, %d silent from far away\n", ok, places.size(), nearOk, far);
  int dok = 0;
  for (const Day& d : days) { Sealed s; if (sealDate(d.mo, d.dy, s) && d.text == s.text) dok++; }
  CHECK(dok == (int)days.size(), "days: %d/%zu", dok, days.size());
  Sealed none; CHECK(!sealDate(4, 2, none), "an unmarked day stayed silent");
  // random ground must be silent
  Rng r; int noise = 0;
  for (int i = 0; i < 20000; i++) { Sealed s; double la = r.range(-60, 70), lo = r.range(-180, 180); sealLookup(la, lo, s); if (s.level >= 2) noise++; }
  printf("  %d days open; random ground at level>=2: %d of 20000\n", dok, noise);
  CHECK(noise < 30, "too much of the earth is marked");
  if (places.empty()) return;
}

static void testAstro() {
  printf("astro:\n");
  float ph = moonPhaseAt(utcOf(2000, 1, 6, 18, 14));
  CHECK(ph < 0.02f || ph > 0.98f, "new moon 2000-01-06: %.3f", ph);
  ph = moonPhaseAt(utcOf(2000, 1, 21, 4, 40));
  CHECK(fabsf(ph - 0.5f) < 0.02f, "full moon 2000-01-21: %.3f", ph);
  Sky s;
  astro(utcOf(2026, 6, 21, 12, 0), 0.0, 0.0, s);       // greenwich noon at the equator, june solstice
  CHECK(fabsf(s.sunEl - 66.56f) < 1.0f, "solstice noon elevation %.2f", s.sunEl);
  CHECK(fabsf(s.solarHour - 12.0f) < 0.1f, "solar hour %.2f", s.solarHour);
  astro(utcOf(2026, 11, 16, 21, 37), 40.7128, -74.006, s);   // new york, about sunset
  CHECK(fabsf(s.sunEl + 0.8f) < 1.5f, "nyc sunset elevation %.2f", s.sunEl);
  CHECK(s.sunAz > 235 && s.sunAz < 255, "nyc sunset azimuth %.1f", s.sunAz);
  CHECK(s.month == 11 && s.day == 16, "solar date %d-%d", s.month, s.day);
  astro(utcOf(2026, 3, 20, 12, 0), 52.0, 0.0, s);
  CHECK(fabsf(s.sunEl - 38.0f) < 1.0f, "equinox noon at 52N: %.2f", s.sunEl);
  astro(utcOf(2026, 11, 17, 3, 51), 40.0, -75.0, s);
  printf("  now-ish at 40N 75W: sun %.1f moon %.1f/%.0f phase %.2f tide %.2f solar %.2f\n", s.sunEl, s.moonEl, s.moonAz, s.moonPhase, s.tide, s.solarHour);
}

static void testNmea() {
  printf("nmea:\n");
  Nmea n;
  const char* a = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
  const char* b = "$GNRMC,035100.00,A,4000.0000,N,07500.0000,W,10.0,90.0,171126,,,A*";
  for (const char* p = a; *p; p++) n.feed(*p);
  CHECK(n.g.fix && fabs(n.g.lat - 48.1173) < 1e-3 && fabs(n.g.lon - 11.5167) < 1e-3 && n.g.sats == 8 && fabsf(n.g.altM - 545.4f) < 0.1f, "GGA %f %f", n.g.lat, n.g.lon);
  uint8_t sum = 0; for (const char* p = b + 1; *p != '*'; p++) sum ^= (uint8_t)*p;
  char full[160]; snprintf(full, sizeof(full), "%s%02X\r\n", b, sum);
  for (const char* p = full; *p; p++) n.feed(*p);
  CHECK(n.g.haveTime && fabs(n.g.utc - utcOf(2026, 11, 17, 3, 51)) < 1.0, "RMC time %f", n.g.utc);
  CHECK(fabs(n.g.lat - 40.0) < 1e-6 && fabs(n.g.lon + 75.0) < 1e-6 && fabsf(n.g.speedKmh - 18.52f) < 0.01f, "RMC pos");
  const char* bad = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48\r\n";
  uint32_t g0 = n.g.good; for (const char* p = bad; *p; p++) n.feed(*p);
  CHECK(n.g.good == g0 && n.g.bad == 1, "bad checksum rejected");
  const char garbage[] = "\xff\x00$$GP,,,,*\r\n$\r\n,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,\r\n";
  for (int i = 0; i < 160; i++) n.feed(garbage[i % (sizeof(garbage) - 1)]);
  printf("  good %u bad %u\n", n.g.good, n.g.bad);
}

static void testDrift() {
  printf("drift:\n");
  lifeReset();
  DriftEvent e = driftVisit(40.005, -75.005, true);
  CHECK(e.newCell && !e.line, "first cell");
  e = driftVisit(40.005, -75.005, true); CHECK(!e.newCell, "same cell");
  e = driftVisit(40.005, -74.995, true); CHECK(e.newCell && e.line, "crossed 75W: line=%d", e.line);
  CHECK(life().linesCrossed == 1, "line tally");
  e = driftVisit(0.004, -74.995, true); CHECK(!e.line, "a teleport is not a crossing");
  e = driftVisit(-0.004, -74.995, true); CHECK(e.special == 1, "equator special=%d", e.special);
  CHECK(driftKnown((int32_t)floor((40.005 + 90) * 100), (int32_t)floor((-75.005 + 180) * 100)), "chart knows the cell");
  for (int i = 0; i < 3000; i++) driftVisit(10.0 + i * 0.0101, 20.0, true);
  CHECK(life().cellCount <= (LIFE_CELLS * 3) / 4 + 1 && life().cellCount > 300, "porous: %u cells kept", life().cellCount);
  uint32_t h1[5] = {1, 2, 3, 4, 5}, h2[5] = {1, 2, 3, 9, 10}, h3[4] = {50, 51, 52, 53};
  float nov; int vis;
  etherMeet(h1, 5, nov, vis); CHECK(nov == 1.0f && vis == 1, "new air");
  etherMeet(h2, 5, nov, vis); CHECK(nov < 0.5f && vis == 2, "same place, shifted: nov %.2f vis %d", nov, vis);
  etherMeet(h3, 4, nov, vis); CHECK(nov == 1.0f && vis == 1, "elsewhere");
  size_t n; const uint8_t* b = lifeBytes(n);
  std::vector<uint8_t> copy(b, b + n);
  uint32_t cells = life().cellCount;
  lifeReset(); CHECK(lifeLoad(copy.data(), n) && life().cellCount == cells, "save/load round trip");
  copy[100] ^= 1; CHECK(!lifeLoad(copy.data(), n), "damage is detected");
  printf("  life block is %zu bytes\n", n);
  lifeReset();
}

// ---------------------------------------------------------- the acoustic world
static const int MICLAG = 1100;                 // samples between speaker and mic through the buffers
static std::vector<int16_t> ownHist;            // everything the synth has played
struct Tap { int d; float g; };
static std::vector<Tap> room;
static void makeRoom(float rtSeconds, float levelDb) {
  room.clear(); Rng r; r.s = 99;
  if (rtSeconds <= 0) return;
  // a real room is dense with reflections; a sparse one would only be an echo
  int taps = (int)(rtSeconds * 500.0f); if (taps < 60) taps = 60; if (taps > 1500) taps = 1500;
  float perHop = taps * 0.008f / (rtSeconds * 1.2f);
  float g0 = powf(10.0f, levelDb / 20.0f) / sqrtf(perHop > 1 ? perHop : 1);
  for (int i = 0; i < taps; i++) {
    float t = r.range(0.004f, rtSeconds * 1.2f);
    float g = g0 * powf(10.0f, -3.0f * t / rtSeconds) * (r.chance(0.5f) ? 1 : -1);
    room.push_back({(int)(t * 48000), g});
  }
}
static float coupling = 0.06f;
static float micAt(size_t n) {                  // what the mic hears of its own speaker at sample n
  float v = 0;
  if (n >= (size_t)MICLAG) v += coupling * ownHist[n - MICLAG];
  for (const Tap& t : room) { size_t d = (size_t)(MICLAG + t.d); if (n >= d) v += coupling * t.g * ownHist[n - d]; }
  return v;
}

static void testEar() {
  printf("ear:\n");
  earInit();
  Senses in; Rng r;
  int16_t blk[256];
  // 1. a held outside tone, with harmonics and noise
  double ph = 0; float got = 0;
  for (int b = 0; b < 400; b++) {
    for (int i = 0; i < 256; i++) { ph += 185.0 / 48000.0; blk[i] = (int16_t)(1500 * sin(6.2831853 * ph) + 600 * sin(12.5663706 * ph) + r.range(-80, 80)); }
    earPush(blk, 256, 0.0f, 1000000);
    if (b % 6 == 5) { earAnalyze(in, 0.033f, nullptr, 0, 110.0f); if (in.pitchHz > 0) got = in.pitchHz; }
  }
  CHECK(fabsf(got - 185.0f) < 3.0f, "pitch of a 185 Hz hum: %.1f", got);
  CHECK(in.loud > 0.3f, "loudness of the hum: %.2f", in.loud);
  printf("  hum 185 -> %.1f Hz, loud %.2f, bands %.2f %.2f %.2f %.2f\n", got, in.loud, in.bands[0], in.bands[1], in.bands[2], in.bands[3]);
  // 2. silence, then a knock
  int onsets = 0;
  for (int b = 0; b < 600; b++) {
    for (int i = 0; i < 256; i++) blk[i] = (int16_t)r.range(-40, 40);
    if (b == 400) for (int i = 0; i < 256; i++) blk[i] = (int16_t)(6000 * exp(-i / 60.0) * sin(i * 0.5));
    earPush(blk, 256, 0.0f, 1000000);
    if (b % 6 == 5) { earAnalyze(in, 0.033f, nullptr, 0, 110.0f); if (in.onset) onsets++; }
  }
  CHECK(onsets == 1, "one knock heard: %d", onsets);
  CHECK(in.loud < 0.1f && in.pitchHz == 0, "silence is quiet: loud %.2f pitch %.1f", in.loud, in.pitchHz);
  // 3. its own drone must not be mistaken for a visitor
  got = 0; float maxLoud = 0;
  for (int b = 0; b < 500; b++) {
    for (int i = 0; i < 256; i++) { ph += 220.0 / 48000.0; blk[i] = (int16_t)(900 * sin(6.2831853 * ph) + 500 * sin(6.2831853 * ph * 1.5) + r.range(-40, 40)); }
    float part[2] = {220.0f, 330.0f};
    earPush(blk, 256, 9000.0f, 1000000);
    if (b % 6 == 5) { earAnalyze(in, 0.033f, part, 2, 110.0f); if (in.pitchHz > 0) got = in.pitchHz; if (b > 200 && in.loud > maxLoud) maxLoud = in.loud; }
  }
  CHECK(got == 0, "own drone ignored as pitch: %.1f", got);
  CHECK(maxLoud < 0.15f, "own drone ignored as loudness: %.2f", maxLoud);
}

static float soundRoom(float rt, float levelDb, uint8_t* verdict) {
  // run the real synth + ear through a synthetic room and strike it
  makeRoom(rt, levelDb);
  S = State(); S.sound.master = 0.6f; lifeReset();
  synthInit(7); earInit(); ownHist.clear();
  Rng r; int16_t out[256], mic[256]; Sounding res; bool done = false;
  for (int b = 0; b < 2400 && !done; b++) {
    synthDuck(earDuck());
    synthRender(out, 256);
    size_t base = ownHist.size();
    ownHist.insert(ownHist.end(), out, out + 256);
    for (int i = 0; i < 256; i++) mic[i] = (int16_t)(micAt(base + i) + r.range(-2, 2));
    if (earPush(mic, 256, synthOwnRms(), synthTransientAge())) synthTock();
    if (b == 500) earStartSounding();
    if (earTakeSounding(res)) done = true;
  }
  if (getenv("DBGENV")) { printf("   rt %.2f floor %.1f dB: ", rt, res.floorDb); for (int i = 0; i < 60; i++) printf("%d ", res.env[i]); printf("\n"); }
  if (verdict) *verdict = res.verdict;
  return done && res.valid ? res.tailMs : -1.0f;
}

static void testSounding() {
  printf("sounding:\n");
  uint8_t v[5];
  float a = soundRoom(0.0f, 0, &v[0]);
  float b = soundRoom(0.25f, -30, &v[1]);
  float c = soundRoom(0.6f, -30, &v[2]);
  float d = soundRoom(1.4f, -26, &v[3]);
  float e = soundRoom(3.0f, -24, &v[4]);
  printf("  tail ms: none %.0f | rt .25 %.0f | rt .6 %.0f | rt 1.4 %.0f | rt 3 %.0f   verdicts %d %d %d %d %d\n", a, b, c, d, e, v[0], v[1], v[2], v[3], v[4]);
  CHECK(a == 0 && v[0] == 0, "a dead room reads as open: %.0f", a);
  CHECK(b > a && c > b && d > c && e >= d, "longer rooms read longer");
  CHECK(v[4] >= 3, "a cathedral reads as large: %d", v[4]);
}

// -------------------------------------------------------------- the long run
static FILE* wav; static uint32_t wavN;
static void wavOpen(const char* path) {
  wav = fopen(path, "wb"); uint8_t h[44] = {0}; fwrite(h, 1, 44, wav); wavN = 0;
}
static void wavClose() {
  uint32_t bytes = wavN * 2, sr = 48000, br = 96000, riff = bytes + 36, fmt = 16; uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
  fseek(wav, 0, SEEK_SET);
  fwrite("RIFF", 1, 4, wav); fwrite(&riff, 4, 1, wav); fwrite("WAVEfmt ", 1, 8, wav); fwrite(&fmt, 4, 1, wav);
  fwrite(&pcm, 2, 1, wav); fwrite(&ch, 2, 1, wav); fwrite(&sr, 4, 1, wav); fwrite(&br, 4, 1, wav); fwrite(&ba, 2, 1, wav); fwrite(&bits, 2, 1, wav);
  fwrite("data", 1, 4, wav); fwrite(&bytes, 4, 1, wav); fclose(wav); wav = nullptr;
}

static void typeLine(const char* s) { for (const char* p = s; *p; p++) mindKey(*p); mindKey(K_ENTER); }

// "format" erases a card only when one answers that it cannot read, and only when said twice.
static void testFormat() {
  printf("format:\n");
  mindInit(7);
  S.in.card = 0; typeLine("format"); typeLine("format");
  CHECK(!S.wantFormat, "no card: nothing to erase");
  S.in.card = 1; typeLine("format"); typeLine("format");
  CHECK(!S.wantFormat, "a card in use is left alone");
  S.in.card = 2; S.in.cardGB = 63.9f; typeLine("format");
  CHECK(!S.wantFormat && strstr(S.say, "64 gb"), "first word names the card and waits");
  typeLine("format");
  CHECK(S.wantFormat, "second word erases");
  printf("  %s\n", S.say);
  S.wantFormat = false; S.in.card = 0;
}

// The pre-rendered horizon: the file is read a point at a time, the nearest point wins,
// off the grid there are no mountains, and a damaged file is refused.
static std::vector<uint8_t> hzBytes;
static bool hzRead(uint32_t off, void* dst, uint32_t n) {
  if ((size_t)off + n > hzBytes.size()) return false;
  memcpy(dst, hzBytes.data() + off, n); return true;
}
static void put16(size_t at, int v) { hzBytes[at] = (uint8_t)v; hzBytes[at + 1] = (uint8_t)(v >> 8); }
static void put32(size_t at, int32_t v) { for (int i = 0; i < 4; i++) hzBytes[at + i] = (uint8_t)((uint32_t)v >> (8 * i)); }
static void makeHorizon(int ny, int nx) {
  const int L = 3, rec = 12 + L * HZ_NAZ;
  hzBytes.assign(64 + (size_t)ny * nx * rec, 0);
  memcpy(hzBytes.data(), "MNHZ", 4); hzBytes[4] = 1; hzBytes[5] = L; put16(6, HZ_NAZ);
  put32(8, 47000000); put32(12, -122000000); put32(16, 9000); put32(20, 13000);   // about a kilometre
  put16(24, ny); put16(26, nx); put16(28, rec); put16(30, 4); put16(32, 25); put16(34, 200);
  for (int i = 0; i < ny * nx; i++) {
    size_t b = 64 + (size_t)i * rec;
    put16(b, 100 + i); hzBytes[b + 2] = 1; put16(b + 4, 1500); put16(b + 6, 250); put16(b + 8, 870);
    for (int a = 0; a < HZ_NAZ; a++) hzBytes[b + 12 + 2 * HZ_NAZ + a] = 10;          // far: a low range all round
    hzBytes[b + 12 + 2 * HZ_NAZ + 150] = (uint8_t)(40 + i);                          // and one peak, south-east
    hzBytes[b + 12 + 90] = 60;                                                       // near: one hill to the east
  }
}
static void testHorizon() {
  printf("horizon:\n");
  makeHorizon(3, 3);
  CHECK(horizonOpen(hzRead, (uint32_t)hzBytes.size()), "a good file opens");
  CHECK(HZ.points == 9 && HZ.bands == 3 && HZ.bandKm[2] == 200, "header read");
  horizonAt(true, 47.0091, -121.9871);
  CHECK(HZ.here && HZ.index == 4 && HZ.groundM == 104, "nearest point is the middle one (index %d)", HZ.index);
  CHECK(fabsf(horizonHeight(2, 150.5f) - 44 / 255.0f) < 1e-4f, "a bin centre reads exactly");
  CHECK(fabsf(horizonHeight(2, 151.0f) - 27 / 255.0f) < 1e-4f, "between bins it interpolates");
  CHECK(fabsf(horizonHeight(2, 359.9f) - 10 / 255.0f) < 1e-4f && fabsf(horizonHeight(2, 0.2f) - 10 / 255.0f) < 1e-4f, "north wraps");
  int band = -1; float a = horizonAngle(90.5f, &band);
  CHECK(band == 0 && fabsf(a - powf(60 / 255.0f, 2) * 90.0f) < 1e-3f, "the near hill is the highest land to the east");
  horizonAt(true, 47.0, -122.0);
  CHECK(HZ.here && HZ.index == 0 && HZ.groundM == 100, "the south-west corner");
  horizonAt(true, 47.0275, -121.974);
  CHECK(HZ.here && HZ.index == 8, "just past the north-east corner still counts");
  horizonAt(true, 47.5, -121.0);
  CHECK(!HZ.here, "far off the grid: no mountains");
  horizonAt(false, 47.009, -121.987);
  CHECK(!HZ.here, "nowhere: no mountains");
  CHECK(horizonHeight(2, 150.5f) == 0, "and nothing is drawn");
  hzBytes[0] = 'X';
  CHECK(!horizonOpen(hzRead, (uint32_t)hzBytes.size()) && !HZ.open, "a damaged file is refused");
  makeHorizon(3, 3);
  CHECK(!horizonOpen(hzRead, (uint32_t)hzBytes.size() - 1), "a short file is refused");
  put16(28, 999);
  CHECK(!horizonOpen(hzRead, (uint32_t)hzBytes.size()), "a wrong record size is refused");
  makeHorizon(1, 1);
  CHECK(horizonOpen(hzRead, (uint32_t)hzBytes.size()), "a single point opens");
  horizonAt(true, 47.05, -122.05);
  CHECK(HZ.here && HZ.offKm < 10, "a single point stands for its town (%.1f km)", HZ.offKm);
  horizonAt(true, 47.3, -122.0);
  CHECK(!HZ.here, "but not for the next town");

  // the mind says what the land is like, once
  makeHorizon(3, 3); horizonOpen(hzRead, (uint32_t)hzBytes.size());
  mindInit(11);
  S.in = Senses(); S.in.haveFix = true; S.in.lat = 47.009; S.in.lon = -121.987;
  bool said = false;
  for (int i = 0; i < 30 * 20 && !said; i++) { S.in.dt = 1 / 30.0f; mindTick(); said = strstr(S.say, "south-east") != nullptr; }
  CHECK(said && strstr(S.say, "87 km"), "it says where the land rises: %s", S.say);
  printf("  %s\n", S.say);
  horizonClose();
}

// "pro": the word opens and closes it, the arrows walk the reaches and the tools,
// enter opens and uses, typing still reaches the mind, and every tool draws.
static void testPro() {
  printf("pro:\n");
  mindInit(21); fb = buf;
  S.in = Senses();
  typeLine("pro");
  CHECK(S.pro && !S.proOpen && S.proBand == 0, "pro opens the dashboard");
  CHECK(strstr(S.say, "instruments") != nullptr, "and says how many: %s", S.say);
  mindKey('/'); mindKey(K_VIEW_NEXT);
  CHECK(S.proBand == 2, "right walks outward (band %d)", S.proBand);
  mindKey(','); CHECK(S.proBand == 1, "left walks back");
  mindKey(K_VIEW_PREV); mindKey(K_VIEW_PREV); CHECK(S.proBand == 7, "and wraps to the sky");
  mindKey('.'); mindKey(K_VOL_DOWN); CHECK(S.proTool == 2, "down picks the next tool (%d)", S.proTool);
  mindKey(';'); CHECK(S.proTool == 1, "up the one before");
  float vol = S.sound.master; mindKey(K_VOL_UP);
  CHECK(S.sound.master == vol, "volume keys are arrows here");
  typeLine("pro echo");
  CHECK(S.pro && S.proOpen && S.proBand == 2 && S.proTool == 2, "pro echo goes straight to the echo");
  S.wantSounding = false; mindKey(K_ENTER);
  CHECK(S.wantSounding, "enter on the echo strikes the room");
  mindKey('/'); CHECK(S.proOpen && S.proBand == 2 && S.proTool == 3, "opened, right goes to the next tool");
  mindKey('/'); CHECK(S.proBand == 3 && S.proTool == 0, "and on across the reaches");
  mindKey(K_BACK); CHECK(!S.proOpen && S.pro, "backspace closes the tool");
  mindKey('h'); mindKey('i'); mindKey(',');
  CHECK(!strcmp(S.typed, "hi,"), "once typing has begun, , and / are letters (%s)", S.typed);
  mindKey(K_ESC); CHECK(!S.typed[0] && S.pro, "escape clears the line first");
  typeLine("pro nothing");
  CHECK(strstr(S.say, "no nothing") != nullptr, "an unknown tool is named back");
  S.wantPortal = false; typeLine("pro link"); mindKey(K_ENTER);
  CHECK(S.wantPortal, "enter on link opens the wifi door");
  mindKey(K_ESC); mindKey(K_ESC);
  CHECK(!S.pro, "escape twice: back to the shore");
  typeLine("pro"); typeLine("pro");
  CHECK(!S.pro, "pro again closes it");

  // every tool draws, with nothing known and with everything known
  makeHorizon(3, 3); horizonOpen(hzRead, (uint32_t)hzBytes.size());
  for (int pass = 0; pass < 2; pass++) {
    S.in = Senses();
    if (pass) {
      S.in.haveTime = true; S.in.utc = utcOf(2026, 11, 24, 21, 30); S.in.haveFix = true; S.in.gpsFix = true;
      S.in.lat = 47.009; S.in.lon = -121.987; S.in.sats = 8; S.in.altM = 410; S.in.speedKmh = 4;
      S.in.haveWx = true; S.in.wmo = 63; S.in.tempC = 6.5f; S.in.humid = 88; S.in.windKmh = 31; S.in.windDir = 240; S.in.visKm = 9;
      S.in.apCount = 14; S.in.apRssi = -48; S.in.etherFresh = true; S.in.peers = 1; S.in.peerRootHz = 98;
      S.in.battV = 3.86f; S.in.online = true; S.in.ip[0] = 10; S.in.wifiRssi = -61; S.in.ota = true; S.in.heapFree = 140000;
      S.in.store = 1; S.in.card = 1; S.in.cardGB = 62.2f; S.in.haveTz = true; S.in.utcOffset = -8 * 3600;
      S.in.pitchHz = 221.0f; S.in.levelDb = -38; S.in.floorDb = -61; S.in.antenna = 0.4f; S.in.accG = 1.04f; S.in.grav[1] = 0.99f;
      S.sounding.valid = true; S.sounding.tailMs = 333; S.sounding.verdict = 2;
    }
    S.pro = true; S.proOpen = false; S.proBand = 0; S.proTool = 0;
    for (int i = 0; i < 60; i++) { S.in.dt = 1 / 30.0f; S.in.onset = (i % 15) == 0; mindTick(); }
    int tools = proToolCount();
    for (int i = 0; i < tools; i++) {
      S.proOpen = false; drawFrame();
      S.proOpen = true; drawFrame();
      mindKey('/');
    }
    CHECK(S.proBand == 0 && S.proTool == 0, "%d tools walked round to the start", tools);
  }
  CHECK(proToolCount() >= 24, "there are %d tools", proToolCount());
  S.pro = false; S.proOpen = false; horizonClose();
}

static void longRun() {
  printf("life:\n");
  S = State(); lifeReset(); fb = buf;
  mindInit(1234); synthInit(1234); earInit(); ownHist.clear(); makeRoom(0.5f, -30);
  wavOpen("out/life.wav");
  Senses& in = S.in;
  const float dt = 1.0f / 30.0f;
  Rng r; r.s = 777;
  // somewhere to walk to: the first place sealed at about a kilometre
  const Place* target = nullptr;
  for (const Place& p : places) if (p.tier == 2 && p.lat > 30 && p.lat < 50) { target = &p; break; }
  char lastSay[84] = {0}; int lastPresence = 0, frames = 0; View lastView = V_SHORE;
  double peak = 0, sumSq = 0; uint64_t nS = 0; double humPh = 0; int lastSleep = 0;
  int16_t out[256], mic[256]; size_t audioDone = 0;
  const double utc0 = utcOf(2026, 11, 17, 3, 51);
  int shot = 0;
  auto snap = [&](const char* name) {
    char keep[84]; memcpy(keep, S.say, sizeof(keep));
    if (S.sayCipher && S.t > 30.0f) strcpy(S.say, "(sealed text, hidden in the simulator)");   // no spoilers on disk
    drawFrame(); memcpy(S.say, keep, sizeof(keep));
    char nm[64]; snprintf(nm, sizeof(nm), "L%02d_%s", shot++, name); dump(nm);
  };

  for (int tick = 0; tick < 30 * 900; tick++) {
    float t = tick * dt;
    in.dt = dt;
    in.steps = (uint32_t)(t > 90 && t < 420 ? (t - 90) * 1.6f : (t >= 420 ? 528 : 0));
    in.motion = (t > 380 && t < 383) ? 0.9f : ((t > 90 && t < 420) ? 0.2f : 0.01f);
    in.tilt = 12.0f * sinf(t * 0.3f) * (t < 420 ? 1.0f : 0.0f);
    in.spin = (t > 500 && t < 503) ? 300.0f : 0.0f;
    in.freefall = (t > 400.0f && t < 400.3f);
    in.faceDown = (t > 430 && t < 445); in.flat = in.faceDown;
    in.battV = t > 850 ? 3.4f : 3.9f;
    if (t > 40) { in.haveTime = true; in.utc = utc0 + t; }
    if (t > 40 && t <= 90) { in.haveFix = true; in.gpsFix = false; in.lat = 40.0; in.lon = -75.0; }
    if (t > 90 && target) {
      // approach from 0.3 degrees out, arrive at t=170, linger, then cross a degree line at t=470
      float u = (t - 90) / 80.0f; if (u > 1) u = 1;
      in.haveFix = true; in.gpsFix = true; in.sats = 9; in.altM = 120; in.speedKmh = u < 1 ? 60 : 3;
      in.lat = target->lat + 0.3 * (1 - u); in.lon = target->lon + 0.2 * (1 - u);
      if (t > 300) { float w = (t - 300) / 200.0f; if (w > 1) w = 1; in.lon = target->lon + w * 1.2; in.speedKmh = w < 1 ? 90 : 0; }
    }
    if (t > 60) { in.haveWx = true; in.wmo = t < 120 ? 3 : (t < 200 ? 95 : (t < 260 ? 71 : 1)); in.cloud = t < 260 ? 0.8f : 0.2f; in.tempC = t < 260 ? 1 : 8; in.windKmh = t < 200 ? 35 : 8; in.windDir = 250; in.pressTrend = t < 200 ? -2.5f : 0.5f; in.visKm = 12; }
    if (tick == 30 * 300) { in.apCount = 6; in.etherFresh = true; in.etherNovelty = 1.0f; in.etherVisits = 1; }
    if (tick == 30 * 310) { in.apCount = 6; in.etherFresh = true; in.etherNovelty = 0.1f; in.etherVisits = 4; }
    in.peers = (t > 330 && t < 360) ? 1 : 0; in.peerRootHz = 98.0f;
    if (tick == 30 * 340) mindKinScrap("the tide was out all day");

    if (tick == 30 * 5) typeLine("hello");
    if (tick == 30 * 12) mindKey(K_ENTER);                       // strike the room
    if (tick == 30 * 26) typeLine("what are you");
    if (tick == 30 * 52) typeLine("the harbour was empty this morning");
    if (tick == 30 * 62) typeLine("where");
    if (tick == 30 * 70) typeLine("time");
    if (tick == 30 * 228) typeLine("is anything out there?");
    if (tick == 30 * 236) typeLine("moon");
    if (tick == 30 * 244) typeLine("help");
    if (tick == 30 * 252) typeLine("copper lanterns");
    if (tick == 30 * 270) typeLine("forget");
    if (tick == 30 * 280) mindKey(K_VOL_UP);
    if (tick == 30 * 510) mindKey(K_TAB);
    if (tick == 30 * 514) mindKey(K_TAB);
    if (tick == 30 * 518) mindKey(K_TAB);
    if (tick == 30 * 522) mindKey(K_TAB);
    if (tick == 30 * 524) for (const char* p = "is it"; *p; p++) mindKey(*p);
    if (tick == 30 * 528) mindKey(K_ESC);

    // audio for this tick: the real synth into a fake room into the real ear
    size_t want = (size_t)((tick + 1) * dt * 48000.0);
    while (audioDone + 256 <= want) {
      synthDuck(earDuck());
      synthRender(out, 256);
      size_t base = ownHist.size();
      ownHist.insert(ownHist.end(), out, out + 256);
      for (int i = 0; i < 256; i++) {
        double ts = (base + i) / 48000.0;
        float ext = r.range(-3, 3);
        if (ts > 30.0 && ts < 36.5) { humPh += 172.0 / 48000.0; ext += 2600 * sinf(6.2831853f * (float)humPh) + 900 * sinf(12.5663706f * (float)humPh); }
        for (double k : {20.0, 20.4, 20.8, 800.0}) { double d = ts - k; if (d >= 0 && d < 0.02) ext += 9000 * exp(-d * 300) * sin(d * 9000); }
        float v = micAt(base + i) + ext;
        mic[i] = (int16_t)(v > 32000 ? 32000 : (v < -32000 ? -32000 : v));
        double o = out[i]; sumSq += o * o; if (fabs(o) > peak) peak = fabs(o); nS++;
      }
      if (earPush(mic, 256, synthOwnRms(), synthTransientAge())) synthTock();
      if (t < 240) { fwrite(out, 2, 256, wav); wavN += 256; }
      audioDone += 256;
    }
    float part[10]; int np = synthPartials(part, 10);
    earAnalyze(in, dt, part, np, S.sound.rootHz);
    if (S.wantSounding) { S.wantSounding = false; earStartSounding(); }
    Sounding snd; if (earTakeSounding(snd)) { mindSounding(snd); printf("  %6.1fs room: valid %d tail %.0f ms verdict %d\n", t, snd.valid, snd.tailMs, snd.verdict); }
    if (S.wantForget) S.wantForget = false;
    if (S.wantPortal) S.wantPortal = false;
    S.wantSave = false;

    if (in.onset) printf("  %6.1fs knock heard\n", t);
    mindTick();
    if ((tick % 3) == 0) { drawFrame(); frames++; }

    if (strcmp(lastSay, S.say) && S.sayAge < 0.1f) {
      strncpy(lastSay, S.say, 83);
      if (S.sayCipher) printf("  %6.1fs says <sealed, %zu chars>\n", t, strlen(S.say));
      else printf("  %6.1fs says: %s\n", t, S.say);
    }
    if (S.presence != lastPresence) { printf("  %6.1fs presence %d -> %d (kind %d)\n", t, lastPresence, S.presence, S.placeKind); lastPresence = S.presence; }
    if (S.view != lastView) { lastView = S.view; }
    int sl = S.sleep > 0.5f; if (sl != lastSleep) { printf("  %6.1fs %s\n", t, sl ? "asleep" : "awake"); lastSleep = sl; }
    if (in.pitchHz > 0 && (tick % 30) == 0) printf("  %6.1fs hears %.1f Hz, answers %.1f\n", t, in.pitchHz, S.sound.answerHz);

    if (tick == 30 * 3) snap("boot_twilight");
    if (tick == 30 * 6) snap("typed_reply");
    if (tick == 30 * 34) snap("humming");
    if (tick == 30 * 80) snap("sky_arrives");
    if (tick == 30 * 135) snap("storm_near");
    if (tick == 30 * 171 + 8) snap("here_unresolved");
    if (tick == 30 * 215) snap("snow");
    if (tick == 30 * 290) snap("clearing");
    if (tick == 30 * 345) snap("kin");
    if (tick == 30 * 383) snap("shaken");
    if (tick == 30 * 475) snap("line_crossed");
    if (tick == 30 * 512) snap("view_spiral");
    if (tick == 30 * 516) snap("view_sounding");
    if (tick == 30 * 520) snap("view_chart");
    if (tick == 30 * 526) snap("typing");
    if (tick == 30 * 790) snap("asleep");
    if (tick == 30 * 870) snap("low_battery");
  }
  wavClose();
  double rms = sqrt(sumSq / (double)nS);
  printf("  audio: peak %.0f, rms %.0f (%.1f dBFS), %llu samples; frames drawn %d\n", peak, rms, 20 * log10(rms / 32768.0), (unsigned long long)nS, frames);
  printf("  spiral %u samples, cells %u, lines %u, found %u, root %.1f Hz, scraps kept %u\n", S.spiralLen, S.cellsKnown, S.linesCrossed, S.placesFound, S.sound.rootHz, life().scrapCount);
  CHECK(peak < 32001 && rms > 200, "audio level sane");
  if (target) CHECK(S.placesFound == 1, "the place it walked to was opened");
  CHECK(S.linesCrossed >= 1, "a line was crossed");
  mindLifeOut(); size_t n; const uint8_t* b = lifeBytes(n);
  std::vector<uint8_t> copy(b, b + n); CHECK(lifeLoad(copy.data(), n), "life survives a save");
}

int main(int argc, char** argv) {
  fb = buf;
  loadSpoilers();
  printf("%zu places, %zu days in the spoiler file\n", places.size(), days.size());
  testSeal(); testAstro(); testNmea(); testDrift(); testEar(); testSounding(); testFormat(); testHorizon(); testPro();
  if (argc < 2 || strcmp(argv[1], "quick")) longRun();
  printf(fails ? "\n%d CHECKS FAILED\n" : "\nall checks passed\n", fails);
  return fails ? 1 : 0;
}
