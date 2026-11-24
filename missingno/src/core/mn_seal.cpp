#include "mn_seal.h"
#include "mn_places.h"      // generated: MN_PLACES[], MN_PLACES_LEN
#include <math.h>
#include <string.h>

namespace mn {

// ------------------------------------------------------------- SipHash-2-4
static inline uint64_t rotl(uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }
static inline uint64_t rd64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
  return v;
}
#define MN_SIPROUND do { \
  v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32); \
  v2 += v3; v3 = rotl(v3, 16); v3 ^= v2; \
  v0 += v3; v3 = rotl(v3, 21); v3 ^= v0; \
  v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32); } while (0)

uint64_t siphash24(const uint8_t key[16], const uint8_t* msg, size_t len) {
  uint64_t k0 = rd64(key), k1 = rd64(key + 8);
  uint64_t v0 = 0x736f6d6570736575ULL ^ k0, v1 = 0x646f72616e646f6dULL ^ k1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ k0, v3 = 0x7465646279746573ULL ^ k1;
  const uint8_t* end = msg + (len & ~(size_t)7);
  for (; msg != end; msg += 8) {
    uint64_t m = rd64(msg);
    v3 ^= m; MN_SIPROUND; MN_SIPROUND; v0 ^= m;
  }
  uint64_t b = (uint64_t)len << 56;
  for (int i = (int)(len & 7) - 1; i >= 0; i--) b |= (uint64_t)msg[i] << (8 * i);
  v3 ^= b; MN_SIPROUND; MN_SIPROUND; v0 ^= b;
  v2 ^= 0xff; MN_SIPROUND; MN_SIPROUND; MN_SIPROUND; MN_SIPROUND;
  return v0 ^ v1 ^ v2 ^ v3;
}

// Three public salts. They are not the secret: the place is.
static const uint8_t K_TAG[16] = {'m','i','s','s','i','n','g','n','o','.','t','a','g','.','0','1'};
static const uint8_t K_KA[16]  = {'t','h','e','.','o','n','l','y','.','w','a','y','.','o','u','t'};
static const uint8_t K_KB[16]  = {'i','s','.','o','u','t','.','/','/','.','s','c','r','a','p','s'};

static void cellMsg(int tier, int32_t cx, int32_t cy, uint8_t m[9]) {
  m[0] = (uint8_t)tier;
  for (int i = 0; i < 4; i++) { m[1 + i] = (uint8_t)((uint32_t)cx >> (8 * i)); m[5 + i] = (uint8_t)((uint32_t)cy >> (8 * i)); }
}

static const double SCALE[4] = {1.0, 10.0, 100.0, 1000.0};

void sealCell(int tier, double lat, double lon, int32_t& cx, int32_t& cy) {
  cx = (int32_t)floor((lat + 90.0) * SCALE[tier]);
  cy = (int32_t)floor((lon + 180.0) * SCALE[tier]);
}

uint32_t sealTag(int tier, int32_t cx, int32_t cy) {
  uint8_t m[9]; cellMsg(tier, cx, cy, m);
  return (uint32_t)siphash24(K_TAG, m, 9);
}

void sealCrypt(int tier, int32_t cx, int32_t cy, uint8_t* buf, size_t len) {
  uint8_t m[9]; cellMsg(tier, cx, cy, m);
  uint64_t a = siphash24(K_KA, m, 9), b = siphash24(K_KB, m, 9);
  uint8_t key[16];
  for (int i = 0; i < 8; i++) { key[i] = (uint8_t)(a >> (8 * i)); key[8 + i] = (uint8_t)(b >> (8 * i)); }
  uint8_t ctr[5] = {'k', 0, 0, 0, 0};
  for (size_t off = 0; off < len; off += 8) {
    uint32_t blk = (uint32_t)(off / 8);
    for (int i = 0; i < 4; i++) ctr[1 + i] = (uint8_t)(blk >> (8 * i));
    uint64_t ks = siphash24(key, ctr, 5);
    for (size_t i = 0; i < 8 && off + i < len; i++) buf[off + i] ^= (uint8_t)(ks >> (8 * i));
  }
}

// Entry: tag[4 LE] meta[1 = level<<4 | tier] len[1] cipher[len]
// Plain: kind[1] sum8[1] text[len-2]
static bool tryEntry(const uint8_t* e, int tier, int32_t cx, int32_t cy, Sealed& out) {
  int len = e[5];
  if (len < 2 || len > 2 + 78) return false;
  uint8_t plain[84];
  memcpy(plain, e + 6, len);
  sealCrypt(tier, cx, cy, plain, len);
  uint8_t sum = 0x5A;
  for (int i = 2; i < len; i++) { sum = (uint8_t)(sum * 31 + plain[i]); if (plain[i] < 0x20 || plain[i] > 0x7E) return false; }
  sum = (uint8_t)(sum + plain[0]);
  if (sum != plain[1] || plain[0] > 12) return false;
  out.kind = plain[0];
  memcpy(out.text, plain + 2, len - 2);
  out.text[len - 2] = 0;
  return true;
}

static bool scan(int tier, int32_t cx, int32_t cy, int levelFloor, Sealed& best) {
  uint32_t tag = sealTag(tier, cx, cy);
  bool hit = false;
  const uint8_t* p = MN_PLACES;
  const uint8_t* end = MN_PLACES + MN_PLACES_LEN;
  while (p + 6 <= end) {
    int len = p[5];
    uint32_t t = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    int etier = p[4] & 0x0F, level = p[4] >> 4;
    if (t == tag && etier == tier && level > levelFloor && level > best.level) {
      Sealed s;
      if (tryEntry(p, tier, cx, cy, s)) { s.level = level; s.tag = tag; best = s; hit = true; }
    }
    p += 6 + len;
  }
  return hit;
}

bool sealLookup(double lat, double lon, Sealed& out) {
  out = Sealed();
  if (lat < -90 || lat > 90) return false;
  for (int tier = 3; tier >= 0; tier--) {            // finest first: at equal strength the nearer thing wins
    int32_t cx, cy; sealCell(tier, lat, lon, cx, cy);
    int32_t wrap = (int32_t)(360.0 * SCALE[tier]);
    int span = tier == 0 ? 0 : 1;                      // the coarsest scale counts only its own cell
    for (int dx = -span; dx <= span; dx++)
      for (int dy = -span; dy <= span; dy++) {
        int32_t y = cy + dy;
        if (y < 0) y += wrap; else if (y >= wrap) y -= wrap;
        scan(tier, cx + dx, y, 0, out);
      }
  }
  return out.level > 0;
}

bool sealDate(int month, int day, Sealed& out) {
  out = Sealed();
  scan(0x0F, month, day, 0, out);
  return out.level > 0;
}

}  // namespace mn
