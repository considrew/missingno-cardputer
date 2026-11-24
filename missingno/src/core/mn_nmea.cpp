#include "mn_nmea.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace mn {

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// ddmm.mmmm -> degrees
static bool degMin(const char* f, const char* hemi, double& out) {
  if (!f[0] || !hemi[0]) return false;
  double v = atof(f);
  double d = floor(v / 100.0);
  out = d + (v - d * 100.0) / 60.0;
  if (hemi[0] == 'S' || hemi[0] == 'W') out = -out;
  return true;
}

static double unixFrom(int y, int mo, int d, int h, int mi, double s) {
  y -= mo <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + (long)doe - 719468;
  return days * 86400.0 + h * 3600.0 + mi * 60.0 + s;
}

static bool parse(char* s, GpsFix& g) {
  // s: "GPGGA,....", no '$', no checksum
  char* f[20]; int nf = 0;
  f[nf++] = s;
  for (char* p = s; *p && nf < 20; p++) if (*p == ',') { *p = 0; f[nf++] = p + 1; }
  if (strlen(f[0]) < 5) return false;
  const char* type = f[0] + 2;
  if (!strcmp(type, "GGA") && nf >= 10) {
    int q = atoi(f[6]);
    g.sats = atoi(f[7]);
    if (q > 0) {
      double la, lo;
      if (degMin(f[2], f[3], la) && degMin(f[4], f[5], lo)) { g.lat = la; g.lon = lo; g.fix = true; }
      if (f[9][0]) g.altM = (float)atof(f[9]);
    } else g.fix = false;
    return true;
  }
  if (!strcmp(type, "RMC") && nf >= 10) {
    bool ok = f[2][0] == 'A';
    if (ok) {
      double la, lo;
      if (degMin(f[3], f[4], la) && degMin(f[5], f[6], lo)) { g.lat = la; g.lon = lo; g.fix = true; }
      if (f[7][0]) g.speedKmh = (float)(atof(f[7]) * 1.852);
    } else g.fix = false;
    // hhmmss.ss and ddmmyy: the clock is often right long before the position is
    if (strlen(f[1]) >= 6 && strlen(f[9]) >= 6) {
      int hh = (f[1][0] - '0') * 10 + (f[1][1] - '0'), mm = (f[1][2] - '0') * 10 + (f[1][3] - '0');
      double ss = atof(f[1] + 4);
      int dd = (f[9][0] - '0') * 10 + (f[9][1] - '0'), mo = (f[9][2] - '0') * 10 + (f[9][3] - '0');
      int yy = 2000 + (f[9][4] - '0') * 10 + (f[9][5] - '0');
      if (yy >= 2024 && mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31 && hh < 24 && mm < 60) {
        g.utc = unixFrom(yy, mo, dd, hh, mm, ss); g.haveTime = true;
      }
    }
    return true;
  }
  return false;
}

bool Nmea::feed(char c) {
  if (c == '$') { n = 0; line[n++] = c; return false; }
  if (n == 0) return false;                        // waiting for a start
  if (c == '\r' || c == '\n') {
    line[n] = 0;
    int len = n; n = 0;
    if (len < 9) return false;
    char* star = strrchr(line, '*');
    if (!star || star - line + 3 > len) { g.bad++; return false; }
    int a = hexv(star[1]), b = hexv(star[2]);
    if (a < 0 || b < 0) { g.bad++; return false; }
    uint8_t sum = 0;
    for (char* p = line + 1; p < star; p++) sum ^= (uint8_t)*p;
    if (sum != (uint8_t)(a * 16 + b)) { g.bad++; return false; }
    *star = 0;
    g.good++;
    return parse(line + 1, g);
  }
  if (n < (int)sizeof(line) - 1) line[n++] = c; else n = 0;
  return false;
}

}  // namespace mn
