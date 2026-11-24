// missingno. -- the ear. Turns the microphone into a few felt things:
// how loud the world is (with its own voice taken out), where the energy sits,
// whether something knocked, whether someone is holding a tone, and -- when
// asked -- how long the room keeps a sound.
#pragma once
#include <stdint.h>
#include "mn_state.h"

namespace mn {

void earInit();

// ---- audio thread, once per block of 48 kHz mono samples.
// Returns true on the one block where the sounding rod should strike.
bool  earPush(const int16_t* mic, int n, float ownRms, uint32_t ownTransientAge);
float earDuck();                       // what the synth's gain should be right now

// ---- main thread
void earAnalyze(Senses& in, float dt, const float* ownPartials, int nPartials, float ownRootHz);
void earStartSounding();
bool earSoundingBusy();
bool earTakeSounding(Sounding& out);   // true once, when a sounding has finished

}  // namespace mn
