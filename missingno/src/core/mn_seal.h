// missingno. -- places that can only be read by standing in them.
//
// The firmware carries a table of sealed entries. Each one is locked with a
// key derived from the cell of the earth it belongs to, so the text is not in
// the binary in any readable form: the device has to *be there* to produce the
// key. tools/seal_places.py makes the table and must agree with this file
// byte for byte.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace mn {

uint64_t siphash24(const uint8_t key[16], const uint8_t* msg, size_t len);

struct Sealed {
  int     level = 0;        // 0 nothing, 1 far, 2 near, 3 close, 4 here
  uint8_t kind = 0;         // what sort of place (drives the apparition)
  uint32_t tag = 0;         // identity of the matched entry
  char    text[80] = {0};   // what is written there, if anything is
};

// Scans the cell it stands in and the eight around it, at every scale.
// Returns the strongest thing found.
bool sealLookup(double lat, double lon, Sealed& out);

// Some things are tied to a day rather than a place.
bool sealDate(int month, int day, Sealed& out);

// Lower-level, for the tests and the sealer's cross-check.
void sealCell(int tier, double lat, double lon, int32_t& cx, int32_t& cy);
uint32_t sealTag(int tier, int32_t cx, int32_t cy);
void sealCrypt(int tier, int32_t cx, int32_t cy, uint8_t* buf, size_t len);

}  // namespace mn
