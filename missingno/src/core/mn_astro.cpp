// Low-precision almanac formulae: the sun to about a hundredth of a degree,
// the moon to about a third of one. More than a 135-pixel sky can show.
#include "mn_astro.h"
#include <math.h>

namespace mn {

static const double D2R = 0.017453292519943295;
static const double R2D = 57.29577951308232;

static double wrap360(double a) { a = fmod(a, 360.0); return a < 0 ? a + 360.0 : a; }
static double sind(double a) { return sin(a * D2R); }
static double cosd(double a) { return cos(a * D2R); }

struct Body { double ra, dec, lambda; };   // degrees

static Body sunAt(double n) {
  double L = wrap360(280.460 + 0.9856474 * n);
  double g = wrap360(357.528 + 0.9856003 * n);
  double lam = L + 1.915 * sind(g) + 0.020 * sind(2 * g);
  double eps = 23.439 - 0.0000004 * n;
  Body b;
  b.lambda = wrap360(lam);
  b.ra = wrap360(atan2(cosd(eps) * sind(lam), cosd(lam)) * R2D);
  b.dec = asin(sind(eps) * sind(lam)) * R2D;
  return b;
}

static Body moonAt(double n) {
  double T = n / 36525.0;
  double lam = 218.32 + 481267.881 * T
             + 6.29 * sind(135.0 + 477198.87 * T) - 1.27 * sind(259.3 - 413335.36 * T)
             + 0.66 * sind(235.7 + 890534.22 * T) + 0.21 * sind(269.9 + 954397.74 * T)
             - 0.19 * sind(357.5 + 35999.05 * T) - 0.11 * sind(186.5 + 966404.03 * T);
  double beta = 5.13 * sind(93.3 + 483202.02 * T) + 0.28 * sind(228.2 + 960400.89 * T)
              - 0.28 * sind(318.3 + 6003.15 * T) - 0.17 * sind(217.6 - 407332.21 * T);
  double l = cosd(beta) * cosd(lam);
  double m = 0.9175 * cosd(beta) * sind(lam) - 0.3978 * sind(beta);
  double nn = 0.3978 * cosd(beta) * sind(lam) + 0.9175 * sind(beta);
  Body b;
  b.lambda = wrap360(lam);
  b.ra = wrap360(atan2(m, l) * R2D);
  b.dec = asin(nn) * R2D;
  return b;
}

static void horizon(const Body& b, double lst, double lat, float& el, float& az, double& ha) {
  ha = wrap360(lst - b.ra);
  double sl = sind(lat), cl = cosd(lat);
  double sinEl = sl * sind(b.dec) + cl * cosd(b.dec) * cosd(ha);
  el = (float)(asin(sinEl) * R2D);
  double a = atan2(-sind(ha), tan(b.dec * D2R) * cl - sl * cosd(ha)) * R2D;
  az = (float)wrap360(a);
}

float moonPhaseAt(double utc) {
  double n = utc / 86400.0 + 2440587.5 - 2451545.0;
  return (float)(wrap360(moonAt(n).lambda - sunAt(n).lambda) / 360.0);
}

void astro(double utc, double lat, double lon, Sky& o) {
  double n = utc / 86400.0 + 2440587.5 - 2451545.0;
  Body s = sunAt(n), m = moonAt(n);
  double gmst = fmod(18.697374558 + 24.06570982441908 * n, 24.0);
  if (gmst < 0) gmst += 24.0;
  double lst = wrap360(gmst * 15.0 + lon);
  double haS, haM;
  horizon(s, lst, lat, o.sunEl, o.sunAz, haS);
  horizon(m, lst, lat, o.moonEl, o.moonAz, haM);
  double elong = wrap360(m.lambda - s.lambda);
  o.moonPhase = (float)(elong / 360.0);
  // Two bulges follow the moon; the sun deepens them at new and full.
  o.tide = (float)(cosd(2.0 * haM) * (0.7 + 0.3 * cosd(2.0 * elong)));
  o.solarHour = (float)fmod(haS / 15.0 + 12.0, 24.0);

  // The date as the sun keeps it here: shift the clock by the longitude.
  double local = utc + lon * 240.0;                    // 4 minutes per degree
  long long z = (long long)floor(local / 86400.0) + 719468;
  long long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  o.day = (int)(doy - (153 * mp + 2) / 5 + 1);
  o.month = (int)(mp < 10 ? mp + 3 : mp - 9);
  o.year = (int)(yoe + era * 400 + (o.month <= 2 ? 1 : 0));
}

}  // namespace mn
