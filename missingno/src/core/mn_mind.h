// missingno. -- the mind. Reads what the body felt (S.in), decides what that
// is like, and writes everything the eye and the voice need (the rest of S).
// It never touches hardware.
#pragma once
#include <stdint.h>
#include "mn_state.h"

namespace mn {

// Key codes beyond ASCII that the body may send.
enum : int { K_ENTER = '\n', K_BACK = 8, K_TAB = '\t', K_ESC = 27,
             K_VOL_UP = 0x80, K_VOL_DOWN, K_VIEW_PREV, K_VIEW_NEXT };

void mindInit(uint32_t seed);        // after the body has loaded (or reset) Life
void mindTick();                     // once per frame, after S.in is filled
void mindKey(int c);
void mindButton(bool longPress);     // the one physical button on the edge
void mindSounding(const Sounding& s);// the room answered
void mindKinScrap(const char* s);    // another one handed over a scrap
const char* mindScrapToGive();       // what to hand over in return (may be "")
void mindSay(const char* s, float hold = 6.0f, int prio = 1, bool cipher = false);

void mindLifeOut();                  // S -> Life, before the body saves
void mindLifeIn();                   // Life -> S, after the body loads

}  // namespace mn
