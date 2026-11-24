// missingno. -- everything that becomes pixels.
#pragma once
#include "mn_gfx.h"
#include "mn_state.h"

namespace mn {

constexpr int HORIZON_Y = 90;   // where the land stops and the sky starts
constexpr int FEET_Y    = 107;  // where it stands
constexpr float MARK_S  = 0.38f;// mark units (the site's 256 viewBox) -> px

void drawFrame();               // renders S.view into fb
void drawMark(int cx, int baseY, float s, uint16_t ink, const FaceP& f,
              const float* bands, uint32_t frame, float t);
void drawMarkIcon(int x, int y, uint16_t c);   // 9x9 glyph of the mark
void drawBoot(float t);         // the first seconds
void drawPortal(const char* ssid, const char* ip, const char* status);

// The chart view asks the drift memory what it knows.
typedef bool (*CellKnownFn)(int32_t ilat, int32_t ilon);
void setChartOracle(CellKnownFn fn);

}  // namespace mn
