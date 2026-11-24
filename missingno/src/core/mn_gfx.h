// missingno. -- framebuffer + primitives.
// Hardware-free: draws into a plain RGB565 array so the exact same code runs
// on the Cardputer and in the host simulator (sim/).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace mn {

constexpr int W = 240;
constexpr int H = 135;

extern uint16_t* fb;                 // W*H, native-endian RGB565

inline constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// The whole palette. Restraint is the look.
constexpr uint16_t C_VOID   = rgb(0x05, 0x04, 0x0A);  // indigo-black
constexpr uint16_t C_BONE   = rgb(0xE5, 0xE5, 0xE5);  // the site's ink, dark mode
constexpr uint16_t C_ASH    = rgb(0x88, 0x88, 0x88);
constexpr uint16_t C_SOOT   = rgb(0x2A, 0x2A, 0x2E);
constexpr uint16_t C_PHOS   = rgb(0x6C, 0xF0, 0xA0);  // phosphor green
constexpr uint16_t C_PHOSD  = rgb(0x1E, 0x5A, 0x3C);
constexpr uint16_t C_EMBER  = rgb(0xD8, 0x5A, 0x28);
constexpr uint16_t C_BLOOD  = rgb(0x7A, 0x12, 0x18);

uint16_t mix(uint16_t a, uint16_t b, int t);          // t: 0..256 toward b
uint16_t scale(uint16_t c, int t);                    // t: 0..256 brightness

void clear(uint16_t c);
inline void px(int x, int y, uint16_t c) {
  if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) fb[y * W + x] = c;
}
inline uint16_t get(int x, int y) {
  if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) return fb[y * W + x];
  return 0;
}
void blend(int x, int y, uint16_t c, int a);          // a: 0..256
void hline(int x, int y, int w, uint16_t c);
void vline(int x, int y, int h, uint16_t c);
void rect(int x, int y, int w, int h, uint16_t c);    // filled
void frame(int x, int y, int w, int h, uint16_t c);   // 1px outline
void line(int x0, int y0, int x1, int y1, uint16_t c);
void disc(int cx, int cy, int r, uint16_t c);
void ring(int cx, int cy, int r, uint16_t c);
// Ordered-dither fill: level 0..16 of a 4x4 Bayer matrix.
void dither(int x, int y, int w, int h, uint16_t c, int level);
bool bayer(int x, int y, int level);

// 5x7 text in a 6x8 cell. Returns the x after the last glyph.
int text(int x, int y, const char* s, uint16_t c);
int textN(int x, int y, const char* s, int n, uint16_t c);
int textWidth(const char* s);
void textCenter(int cx, int y, const char* s, uint16_t c);
void textRight(int xr, int y, const char* s, uint16_t c);
void glyph(int x, int y, char ch, uint16_t c);
// The same font, each dot k x k pixels (a 6k-wide cell). Returns the x after the last glyph.
int textBig(int x, int y, const char* s, uint16_t c, int k);

// Cheap deterministic hashes (visual noise, star fields, glitches).
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352DU; x ^= x >> 15; x *= 0x846CA68BU; x ^= x >> 16;
  return x;
}
inline uint32_t hash2(int x, int y, uint32_t s = 0) {
  return hash32((uint32_t)x * 0x9E3779B1U ^ (uint32_t)y * 0x85EBCA77U ^ s);
}
float vnoise(float x, float y, uint32_t seed);        // value noise 0..1

// Tiny PRNG for everything that is allowed to be random.
struct Rng {
  uint32_t s = 0x1234ABCDu;
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float f() { return (next() >> 8) * (1.0f / 16777216.0f); }      // 0..1
  float range(float a, float b) { return a + (b - a) * f(); }
  int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
  bool chance(float p) { return f() < p; }
};

}  // namespace mn
