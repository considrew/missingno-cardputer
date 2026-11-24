// missingno. -- the land that is really there.
//
// tools/horizon.py casts rays over a digital elevation model (USGS 3DEP or
// Copernicus GLO-30) for a grid of points around a place, and flattens each
// point's skyline into the shore's own vertical scale: one byte per degree of
// azimuth, for the near ground, the middle distance and the far ranges. The
// body hands over a way to read that file (from the card, or built into the
// firmware); this keeps only the one point nearest to where it stands.
//
// File layout (little-endian), "MNHZ" version 1:
//   0  char[4] magic   4 u8 version   5 u8 bands   6 u16 nAz (360)
//   8  i32 lat0, lon0, dlat, dlon (micro-degrees; row 0 is the south edge)
//   24 u16 ny, nx, recSize   30 u16 bandKm[4]   38 u8 sources   39 u8 eye (dm)
//   40 u32 made (unix)       ... 64: records, row-major
//   record: i16 groundM, u8 flags, u8 0, u16 peakAz (0.1 deg), i16 peakAng (0.01 deg),
//           u16 peakKm (0.1 km), u16 0, then bands x nAz bytes (band 0 nearest)
#pragma once
#include <stdint.h>

namespace mn {

constexpr int HZ_NAZ = 360;
constexpr int HZ_BANDS = 4;

typedef bool (*HorizonRead)(uint32_t off, void* dst, uint32_t n);

struct Horizon {
  bool open = false;              // a horizon file was found and made sense
  bool here = false;              // and where it stands is on it
  uint8_t bands = 0;
  uint8_t sources = 0;            // 1 USGS 3DEP, 2 Copernicus GLO-30
  uint16_t bandKm[HZ_BANDS] = {0};
  uint32_t points = 0;
  float stepKm = 0;
  int32_t index = -1;             // which point is loaded
  float offKm = 0;                // how far that point is from here
  int16_t groundM = 0;
  float peakAz = 0, peakAng = 0, peakKm = 0;   // the highest point beyond the near ground
  uint8_t prof[HZ_BANDS][HZ_NAZ]; // 0 nothing above the level line .. 255 straight up (sqrt scale)
};
extern Horizon HZ;

bool horizonOpen(HorizonRead rd, uint32_t size);   // false: not a horizon file
void horizonClose();
// Loads the point nearest (lat, lon) if it is a different one. Cheap when it is not.
void horizonAt(bool havePos, double lat, double lon);
// Height of a band's silhouette at an azimuth, as a fraction of the full 90-degree
// scale (0..1). Multiply by the scale's pixel height; elevation = h*h*90 degrees.
float horizonHeight(int band, float azDeg);
// Which band of land stands highest at this azimuth, and its elevation in degrees.
float horizonAngle(float azDeg, int* band = nullptr);

}  // namespace mn
