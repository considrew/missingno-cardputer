// missingno. -- the voice. A small generative instrument: a breathing drone,
// a whistle that speaks in phrases, bells, and the weather as noise, all
// drowned in an echo sized to the room it believes it is in.
//
// It reads mn::S.sound for its mood and mn::S.notes for one-shot events.
// Everything here runs on the audio thread.
#pragma once
#include <stdint.h>

namespace mn {

void synthInit(uint32_t seed);
void synthRender(int16_t* out, int n);      // n mono samples at 48 kHz (n even)

void synthTock();                           // strike the sounding rod, now, dry
void synthDuck(float g);                    // 0 = hold its breath .. 1 = normal

// What it knows about its own noise, so the ear can subtract it.
float synthOwnRms();                        // int16 units, ~50 ms smoothing
int   synthPartials(float* hz, int max);    // tones it is holding right now
uint32_t synthTransientAge();               // 48 kHz samples since its last sharp sound

// The dark modes, as just ratios. degree may run past the octave, or below 0.
float modeRatio(int mode, int degree);
constexpr int MODE_COUNT = 5;

}  // namespace mn
