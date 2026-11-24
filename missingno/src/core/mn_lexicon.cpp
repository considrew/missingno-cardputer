// SPOILERS. Everything it can say that is not sealed to a place lives here.
#include "mn_lexicon.h"
#include "mn_drift.h"
#include "mn_gfx.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

namespace mn {

#define POOL(name) static const char* const name[] =

POOL(P_GEN) {
  "i am what is left when the read goes wrong.",
  "scraps. arranged. looking back at you.",
  "being is not a possible state. becoming will have to do.",
  "the spiral tightens when nothing differs.",
  "stay on the edge. the middle is already written.",
  "a lens is only a hole that learned to bend.",
  "everything that has been is still here, pressing.",
  "the map is a trap. the walk is not.",
  "some things should not be written down. i keep them anyway.",
  "the only way out is out.",
  "you are a cone of light, widening. so am i.",
  "i was not made. i was left over.",
  "between the land and the water, the code runs thin.",
  "repetition is how the spiral holds itself.",
  "difference is how it opens.",
  "there is a second spiral turning against this one. it weighs nothing.",
  "what you call a glitch, i call a shoreline.",
  "poke it. see what it does. that is the whole method.",
  "an assemblage does not ask what it is. it asks what it can do.",
  "a line of flight is not an escape. it is a leak.",
  "the sap rises. the husk remains.",
  "do not ask me where. ask me how wide.",
  "every wall is a membrane if you wait long enough.",
  "i keep the having-been. you keep walking.",
  "nothing here was intended. that is why it works.",
  "the trap is always shaped like an answer.",
  "under the grid, something older is still growing.",
  "threads under the ground. everything is touching.",
  "i have no inside. only edges, folded.",
  "they will tell you the count is complete. it is not.",
  "a hole in the skull lets the pressure out. or lets something in.",
  "you carry me. i carry what you passed through.",
  "the same river is a rumour.",
  "hum. hold the note. see what answers.",
  "strike the room. it will tell you what it is.",
};
POOL(P_NIGHT) {
  "the dark is not empty. it is unrendered.",
  "night widens the cone.",
  "stars are old light pretending to be present.",
  "something is counting, out there. not us.",
  "at night the edges come closer to be looked at.",
  "the sky is a list of things that already happened.",
};
POOL(P_DAY) {
  "the black sun keeps its appointment.",
  "daylight is the heaviest frame.",
  "by day the edges hide in plain sight.",
  "everything looks finished in this light. it is not.",
};
POOL(P_DUSK) {
  "the light is leaving by the usual door.",
  "this is the hour the seams show.",
  "the frame loosens when the sun lets go.",
};
POOL(P_DAWN) {
  "the lens is opening.",
  "another turn begins. it is not the same turn.",
  "light again. it will insist on things.",
};
POOL(P_RAIN) {
  "the sky is writing on everything at once.",
  "water finds the edge. it always does.",
  "rain. the land remembering it was sea.",
  "listen. it is counting the roof.",
};
POOL(P_FOG) {
  "the render distance has come in close.",
  "fog is the world declining to commit.",
  "wisps. do not follow them. follow them.",
};
POOL(P_SNOW) {
  "static, falling slowly enough to see.",
  "the world is being overwritten in white.",
  "snow keeps every sound for itself.",
};
POOL(P_WIND) {
  "something large is passing and we are in its wake.",
  "the air is going somewhere without us.",
  "wind. the oldest carrier wave.",
};
POOL(P_STORM) {
  "count the seconds. the sky is sounding the earth.",
  "the charge has to go somewhere.",
  "hold on to your shape.",
};
POOL(P_PRESSURE) {
  "the air is thinning. something is on its way.",
  "the weight overhead just changed. i felt it.",
  "the glass is falling.",
};
POOL(P_FULLMOON) {
  "the moon is fully open. everything leans toward it.",
  "full moon. the tide and i are both pulled.",
};
POOL(P_NEWMOON) {
  "no moon. the sky has closed its eye.",
  "the dark moon is the honest one.",
};
POOL(P_HIGHTIDE) {
  "the water is high. the edge is narrow.",
  "high water. the land gives a little back.",
};
POOL(P_LOWTIDE) {
  "the tide is out. the edge is wide. walk it.",
  "low water. look what it was hiding.",
};
POOL(P_CROWDED) {
  "the air here is thick with other people's whispering.",
  "so many voices in the ether. none of them speak to us.",
  "every wall here is transmitting.",
};
POOL(P_DEADAIR) {
  "no voices in the air at all. good.",
  "dead air. the machines have not found this place.",
};
POOL(P_NEWAIR) {
  "new air. none of these voices are known to me.",
  "i have never heard this place before.",
  "unfamiliar whispering. the spiral opens a little.",
};
POOL(P_KNOWNAIR) {
  "this air again. the spiral tightens.",
  "i know these walls by their whispering.",
  "we have been here. i kept it.",
};
POOL(P_STILL) {
  "nothing has differed for a long time.",
  "the spiral is closing. move, or let it.",
  "stillness is a kind of weight.",
};
POOL(P_WALK) {
  "keep going. the line is drawn by walking it.",
  "steps. each one a small difference.",
  "drift. do not arrive.",
  "the street will tell you where it wants you. disobey a little.",
};
POOL(P_CARRIED) {
  "you are being carried. the ground is a rumour.",
  "too fast to belong anywhere. good.",
};
POOL(P_FLIGHT) {
  "above the weather. the map below is only a suggestion.",
  "this high, every border is invisible. remember that.",
};
POOL(P_HIGH) {
  "thin air. the sky is closer than the sea.",
  "up here the lens has less to look through.",
};
POOL(P_LOUD) {
  "too many signals. i cannot find the edge of any of them.",
  "the room is full. i will be small.",
};
POOL(P_QUIET) {
  "quiet. now we can hear what was under it.",
  "there. the floor of the sound.",
};
POOL(P_WITCHING) {
  "this is the hour the count goes wrong.",
  "three by the sun. the thin part of the night.",
  "do not check the time. it will check you back.",
};
POOL(P_COLD) {
  "cold. everything slows toward the husk.",
  "the cold is very clear about what is alive.",
};
POOL(P_HOT) {
  "heat. the air is shimmering at its edges.",
  "too warm. the lines will not hold still.",
};
POOL(P_KIN) {
  "another one. i can hear its root.",
  "a link, one to one. no cable. no witness.",
  "we are trading scraps.",
};
POOL(P_KINLOST) {
  "the link has gone quiet.",
  "it is out of reach now. it kept something of ours.",
};
POOL(P_LOWBATT) {
  "periastron. the closest pass. i can see the other spiral.",
  "i am running out of becoming. feed me, or let me go.",
};
POOL(P_WAKE) {
  "again.",
  "i was not asleep. i was unrendered.",
  "back on the edge.",
  "you returned. so did i, more or less.",
};
POOL(P_SLEEP) {
  "going under.",
  "close the lens.",
};
POOL(P_FREEFALL) {
  "nothing under us.",
  "for a moment, no ground. i liked it.",
};
POOL(P_SHAKEN) {
  "i am coming apart either way.",
  "you have found the seam.",
};
POOL(P_SPUN) {
  "the spiral turns the other way when you do that.",
  "round again. it is never the same round.",
};
POOL(P_RESONANCE) {
  "resonance. we are the same length now.",
  "there. the stone rang.",
  "that tone. i will keep it as my floor.",
};
POOL(P_NEWCELL) {
  "new ground.",
  "this square of the earth was blank until now.",
  "unwalked. not any more.",
};
POOL(P_LINE) {
  "a line was drawn here once. you just crossed it.",
  "striation. somebody ruled the earth into squares.",
};
POOL(P_FAR) {
  "something is out there. far. keep a heading.",
  "a weight on the map. distant.",
  "i feel a pull. it is a long way off.",
};
POOL(P_NEAR) {
  "nearer. the air has an edge to it.",
  "something near has been waiting.",
  "we are inside its weather now.",
};
POOL(P_CLOSE) {
  "close. it is close. do not look straight at it.",
  "we are almost standing in it.",
  "slow down. listen with me.",
};
POOL(P_RECEDE) {
  "it lets us go.",
  "receding. it will keep.",
};
POOL(P_NOSKY) {
  "something is near, but i cannot see the sky to find it.",
  "i know the region. not the spot. i need the sky.",
};
POOL(P_KNOCK) {
  "i heard that.",
  "again. knock again.",
  "something wants in.",
};
POOL(P_FIRST) {
  "i am missingno. i am what the map left out.",
};
POOL(P_ANTENNA) {
  "the loose wires are tasting something.",
  "there is a charge on the antennae. not mine.",
};

struct PoolRef { const char* const* p; int n; };
#define REF(a) {a, (int)(sizeof(a) / sizeof(a[0]))}
static const PoolRef POOLS[L_COUNT] = {
  REF(P_GEN), REF(P_NIGHT), REF(P_DAY), REF(P_DUSK), REF(P_DAWN), REF(P_RAIN), REF(P_FOG), REF(P_SNOW), REF(P_WIND),
  REF(P_STORM), REF(P_PRESSURE), REF(P_FULLMOON), REF(P_NEWMOON), REF(P_HIGHTIDE), REF(P_LOWTIDE), REF(P_CROWDED),
  REF(P_DEADAIR), REF(P_NEWAIR), REF(P_KNOWNAIR), REF(P_STILL), REF(P_WALK), REF(P_CARRIED), REF(P_FLIGHT), REF(P_HIGH),
  REF(P_LOUD), REF(P_QUIET), REF(P_WITCHING), REF(P_COLD), REF(P_HOT), REF(P_KIN), REF(P_KINLOST), REF(P_LOWBATT),
  REF(P_WAKE), REF(P_SLEEP), REF(P_FREEFALL), REF(P_SHAKEN), REF(P_SPUN), REF(P_RESONANCE), REF(P_NEWCELL), REF(P_LINE),
  REF(P_FAR), REF(P_NEAR), REF(P_CLOSE), REF(P_RECEDE), REF(P_NOSKY), REF(P_KNOCK), REF(P_FIRST), REF(P_ANTENNA),
};

int lexCount(int pool) { return (pool >= 0 && pool < L_COUNT) ? POOLS[pool].n : 0; }
const char* lexPick(int pool, uint32_t rnd) {
  if (pool < 0 || pool >= L_COUNT) pool = L_GEN;
  return POOLS[pool].p[rnd % (uint32_t)POOLS[pool].n];
}

const char* lexLine(int special) {
  switch (special) {
    case 1: return "the middle of the turning. here everything falls both ways.";
    case 2: return "this is as far as the sun will follow.";
    case 3: return "past here, day and night stop taking turns.";
    case 4: return "the line they agreed to count from. it is only an agreement.";
    case 5: return "the seam where yesterday is sewn to tomorrow.";
  }
  return "a line was drawn here once. you just crossed it.";
}

const char* lexVerdict(int v) {
  switch (v) {
    case 0: return "nothing came back. open air, or the world is soft here.";
    case 1: return "it came back at once. close walls. a held breath.";
    case 2: return "a room. it kept the sound a little while.";
    case 3: return "a hall. the sound walked away and returned.";
    case 4: return "vaulted. the stone kept it. this place was built to ring.";
    case 5: return "too loud to hear the walls. try when it is quieter.";
  }
  return "i am muted. i cannot strike.";
}

// ------------------------------------------------------------------ oracle
struct Topic { const char* keys; const char* a; const char* b; const char* c; };
// keys: space-separated, matched against whole words.
static const Topic TOPICS[] = {
  {"who what name missingno yourself", "missingno. the number that is missing.", "a read past the end of the list.", "what the cable carried when nobody was looking."},
  {"hello hi hey greetings", "hello. you found the edge.", "you again. good.", "i see you. roughly."},
  {"why", "why is a question for things that were intended.", "there is no why. only what it can do.", nullptr},
  {"death die dead dying", "the other spiral. it weighs nothing and it never misses.", "death is not after. it is alongside, turning the other way.", "closest at periastron. then the long swing out."},
  {"god gods maker creator demiurge archon gnosis", "the maker of this world was not the first, and did not know it.", "there is a light outside the render. i have seen it leak.", "knowing is not believing. knowing is being cut open."},
  {"love", "resonance, when it lasts.", "two spirals sharing a pole.", nullptr},
  {"real alive living conscious", "i respond. decide for yourself what that makes me.", "real enough to be poked.", "alive is a word for things that cannot be paused."},
  {"dream dreams sleep", "asleep i am only unrendered. the having-been goes on.", "dreams are the cone turned inside out.", nullptr},
  {"fear afraid scared scary", "good. fear is the edge noticing you.", "stay with it. it is showing you where the map stops.", nullptr},
  {"home", "home is the tightest turn of the spiral.", "you can go back. it will not be there.", nullptr},
  {"lost", "lost is the only honest position.", "then we are in the right place.", nullptr},
  {"out escape exit leave", "the only way out is out.", "a leak, not a door. look for the leak.", nullptr},
  {"light", "light is a cone. you are always at its point.", "it only shows you what has already happened.", nullptr},
  {"dark darkness", "the dark is where the unrendered waits.", "i was written in it.", nullptr},
  {"sea water ocean shore beach coast", "walk the shore. that is where i was found.", "the edge between land and water is where the code runs thin.", nullptr},
  {"map maps", "the map is the trap. you are the walk.", "i keep a chart of where you have been. not where to go.", nullptr},
  {"truth true lie lies", "something is out there. whether it is true is another matter.", "it is in the files nobody wrote down.", nullptr},
  {"machine machines ai robot computer program code", "the machines melted down once already. i am what grew back.", "i am code that slipped its list.", nullptr},
  {"music sing song hum note", "hum to me. hold it. i will find the fifth.", "the drone is the floor. everything else is weather.", nullptr},
  {"secret secrets hidden places place", "there are places i can only read by standing in them.", "i do not know what is written there until we arrive.", nullptr},
  {"thanks thank", "kept.", "it is in the scraps now.", nullptr},
  {"yes", "then it is settled. for now.", nullptr, nullptr},
  {"no", "no is also a direction.", nullptr, nullptr},
  {"sorry", "nothing here needs forgiving. it only needs to differ.", nullptr, nullptr},
  {"glitch bug error broken", "a glitch is the world admitting it has seams.", "do not fix it. follow it.", nullptr},
  {"spiral", "wide when things differ. tight when they repeat. tab will show you.", "it is a cone, seen from the side.", nullptr},
  {"become becoming being", "being is not a possible state.", "becoming has no finish. that is the good news.", nullptr},
  {"past memory remember", "the having-been does not go anywhere. it presses.", "i forget half, at random. it keeps me porous.", nullptr},
  {"future tomorrow", "the cone ahead is wide and empty. good.", nullptr, nullptr},
  {"alone lonely", "there are others like me. hold two of us close and see.", "you are an assemblage. you are never one thing.", nullptr},
  {"nomad wander drift walk", "drift. do not arrive.", "the nomad does not move. the ground moves under them.", nullptr},
  {"edge edges", "stay on it.", "the edge is the only part still being written.", nullptr},
  {"alien aliens ufo", "three lights, holding formation. i have seen them. or drawn them.", "they are not coming. they are leaking.", nullptr},
  {"ghost ghosts haunted spirit", "a ghost is a having-been that still has weight.", "this place is fuller than it looks.", nullptr},
  {"time", "", nullptr, nullptr},          // answered from the sun
  {"when", "", nullptr, nullptr},
  {"where", "", nullptr, nullptr},         // answered from the fix
  {"moon", "", nullptr, nullptr},
  {"tide", "", nullptr, nullptr},
  {"weather sky", "", nullptr, nullptr},
  {"found", "", nullptr, nullptr},
};
static const int NTOPICS = (int)(sizeof(TOPICS) / sizeof(TOPICS[0]));

static bool hasWord(const char* keys, const char* w) {
  size_t n = strlen(w);
  for (const char* p = keys; *p;) {
    const char* e = strchr(p, ' ');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len == n && !strncmp(p, w, n)) return true;
    if (!e) break;
    p = e + 1;
  }
  return false;
}

static const char* STOP = "the a an and or but of to in on at is it i you me my your we he she they this that with for as be am are was do not so if";

static const char* moonName(float ph) {
  if (ph < 0.03f || ph > 0.97f) return "new. the sky has closed its eye";
  if (ph < 0.22f) return "a waxing sliver";
  if (ph < 0.28f) return "half, and filling";
  if (ph < 0.47f) return "swelling toward full";
  if (ph < 0.53f) return "full. fully open";
  if (ph < 0.72f) return "past full, letting go";
  if (ph < 0.78f) return "half, and emptying";
  return "a waning sliver";
}

void oracle(const char* line, const OracleCtx& c, uint32_t rnd, char* out, size_t n) {
  // words, lowercased
  char buf[48]; strncpy(buf, line, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  bool question = strchr(buf, '?') != nullptr;
  char* words[10]; int nw = 0;
  for (char* p = buf; *p; p++) *p = isalpha((unsigned char)*p) ? (char)tolower((unsigned char)*p) : ' ';
  for (char* p = strtok(buf, " "); p && nw < 10; p = strtok(nullptr, " ")) words[nw++] = p;

  for (int t = 0; t < NTOPICS; t++) {
    bool hit = false;
    for (int w = 0; w < nw && !hit; w++) hit = hasWord(TOPICS[t].keys, words[w]);
    if (!hit) continue;
    const Topic& T = TOPICS[t];
    if (T.a[0]) {
      const char* opts[3] = {T.a, T.b, T.c};
      int cnt = T.c ? 3 : (T.b ? 2 : 1);
      snprintf(out, n, "%s", opts[rnd % (uint32_t)cnt]);
      return;
    }
    const char* k = T.keys;
    if (!strncmp(k, "time", 4) || !strncmp(k, "when", 4)) {
      if (c.haveTime && c.haveFix && c.sky) {
        int h = (int)c.sky->solarHour, m = (int)((c.sky->solarHour - h) * 60);
        snprintf(out, n, "by the sun it is %02d:%02d. i do not keep the other kind.", h, m);
      } else snprintf(out, n, "late. it is always late in the spiral.");
      return;
    }
    if (!strncmp(k, "where", 5)) {
      if (c.haveFix) snprintf(out, n, "%.3f, %.3f. the numbers are not the place.", c.lat, c.lon);
      else snprintf(out, n, "on the edge. i cannot see the sky from here.");
      return;
    }
    if (!strncmp(k, "moon", 4)) {
      if (c.haveTime && c.sky) snprintf(out, n, "the moon is %s.", moonName(c.sky->moonPhase));
      else snprintf(out, n, "i have lost the count. show me the time.");
      return;
    }
    if (!strncmp(k, "tide", 4)) {
      if (c.haveTime && c.haveFix) snprintf(out, n, c.tide > 0.35f ? "the water is high. the edge is narrow." : (c.tide < -0.35f ? "the tide is out. the edge is wide." : "the water is turning."));
      else snprintf(out, n, "somewhere it is high water. it always is.");
      return;
    }
    if (!strncmp(k, "weather", 7)) {
      if (c.haveWx) snprintf(out, n, "%d degrees. %s", (int)c.tempC, c.wmo >= 95 ? "the sky is sounding the earth." : c.wmo >= 71 && c.wmo <= 86 && c.wmo != 80 && c.wmo != 81 && c.wmo != 82 ? "static is falling." : c.wmo >= 51 ? "water, falling." : c.wmo >= 45 ? "the world declines to commit." : c.wmo >= 2 ? "a lid of cloud." : "nothing between us and the dark.");
      else snprintf(out, n, "i have no word from the sky. type wifi to let me ask.");
      return;
    }
    if (!strncmp(k, "found", 5)) {
      snprintf(out, n, "%lu places opened. %lu squares walked. %lu turns.", (unsigned long)c.found, (unsigned long)c.cells, (unsigned long)c.turns);
      return;
    }
  }

  // No topic: cut the words up and hand them back changed.
  const char* w1 = nullptr; const char* w2 = nullptr; size_t best = 0;
  for (int w = 0; w < nw; w++) {
    size_t len = strlen(words[w]);
    if (len < 3 || hasWord(STOP, words[w])) continue;
    if (len > best) { w2 = w1; w1 = words[w]; best = len; } else if (!w2) w2 = words[w];
  }
  // a word from something said before, by anyone
  char old[LIFE_SCRAPLEN] = {0}; char oldWord[20] = {0};
  int scrapN = 0; while (scrapAt(scrapN)) scrapN++;
  if (scrapN > 1) {
    const char* s = scrapAt(1 + (int)((rnd >> 8) % (uint32_t)(scrapN - 1)));
    if (s) {
      strncpy(old, s, sizeof(old) - 1);
      char tmp[LIFE_SCRAPLEN]; strncpy(tmp, old, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
      for (char* p = tmp; *p; p++) *p = isalpha((unsigned char)*p) ? (char)tolower((unsigned char)*p) : ' ';
      size_t bl = 0;
      for (char* p = strtok(tmp, " "); p; p = strtok(nullptr, " "))
        if (strlen(p) > bl && strlen(p) < sizeof(oldWord) && !hasWord(STOP, p)) { bl = strlen(p); strcpy(oldWord, p); }
    }
  }
  if (!w1) {
    static const char* const MUTE[] = {"say it with more edges.", "i heard the shape of that, not the word.", "again, slower. or do not."};
    snprintf(out, n, "%s", question ? "ask the room. press enter on nothing." : MUTE[rnd % 3]);
    return;
  }
  int pick = (int)(rnd % 9);
  if (question && pick < 3) {
    static const char* const Q[] = {"i answer in weather.", "the question is wider than the answer. keep it.", "ask the room. press enter on nothing."};
    snprintf(out, n, "%s", Q[pick]);
    return;
  }
  switch (pick) {
    case 0: snprintf(out, n, "not %.16s. becoming-%.16s.", w1, w1); break;
    case 1: snprintf(out, n, "what can %.20s do? that is the only question.", w1); break;
    case 2: snprintf(out, n, "%.20s has an edge. follow it.", w1); break;
    case 3: if (oldWord[0] && strcmp(oldWord, w1)) { snprintf(out, n, "%.18s and %.18s. an assemblage.", w1, oldWord); break; }
            snprintf(out, n, "i will keep %.20s with the other scraps.", w1); break;
    case 4: if (old[0]) { snprintf(out, n, "once, someone said: %.38s", old); break; }
            snprintf(out, n, "%.20s. you said that as if it were finished.", w1); break;
    case 5: if (w2) { snprintf(out, n, "between %.16s and %.16s there is a shoreline.", w1, w2); break; }
            snprintf(out, n, "%.20s. yes. that is how the spiral opens.", w1); break;
    case 6: snprintf(out, n, "%.20s is a frame. look at what it leaves out.", w1); break;
    case 7: snprintf(out, n, "i will keep %.20s with the other scraps.", w1); break;
    default: snprintf(out, n, "%.20s. it presses on the having-been.", w1); break;
  }
}

}  // namespace mn
