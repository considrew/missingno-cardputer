// missingno. -- the theremin. Typed `theremin`, it gives its voice to your hand.
//
// The accelerometer is the instrument: rolling it left and right is the pitch
// antenna (three octaves across about 110 degrees, centred near a4). The volume
// loop is tipping it forward or back: full as it was held when the word was
// typed, fading to nothing about 55 degrees away either way, or face down. The gyroscope is the expression: twisting it about the
// vertical makes the note tremble, and any quick movement gives it an edge.
//
//   ; and .   octave up and down        , and /   glide, chromatic, its mood, five dark scales
//   enter     hold the note             escape    back to where it was
//
// The creature sings it: its mouth opens with the volume and shakes with the
// vibrato, and the melody falls down the screen behind it.
#pragma once
#include <stdint.h>

namespace mn {

void thereminStart();
void thereminStop();
void thereminTick(float dt);    // every frame: read the hand, set S.sound.there*
bool thereminKey(int c);        // its keys; false to let the mind have it
void thereminButton();          // the G0 button: hold
void drawTheremin();
const char* thereminScaleName(int scale);
float thereminNoteHz();         // what it is playing now (0 when silent)

}  // namespace mn
