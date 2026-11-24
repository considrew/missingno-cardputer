// missingno. -- "pro": everything it can sense, as instruments you can use.
//
// Typed `pro`, the shore gives way to a dashboard. The tools are set out by
// reach -- how far away the thing each one measures is -- from the device
// itself, through the hand, the room, the radio air, the weather, the land and
// the satellites, out to the sun. Left and right walk outward and back; up and
// down pick a tool; enter opens it (and, opened, uses it: strike the room,
// open the Wi-Fi door, reset a peak). Escape or backspace steps back; `pro`
// again returns to the shore. The creature keeps living underneath.
#pragma once
#include <stdint.h>

namespace mn {

void proTick(float dt);   // keep the histories; once per frame, before the mind consumes S.in
bool proKey(int c);       // the dashboard's keys; false if it is a key for the mind
void drawPro();           // the whole screen
int  proToolCount();      // all the tools there are, for the help line
bool proGoto(const char* name);   // open a tool by name ("pro tuner"); false if there is none

}  // namespace mn
