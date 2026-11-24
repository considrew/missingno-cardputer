// missingno. -- the shared vocabulary between body, mind, and expression.
//
// The body (missingno.ino, or the host simulator) fills `Senses` from whatever
// hardware it has. The mind (mn_mind.cpp) never sees a pin, a register, or a
// byte: only what it is like to be somewhere. The mind then writes `State`,
// which the renderer and the synth read to make it visible and audible.
#pragma once
#include <stdint.h>

namespace mn {

// ---------------------------------------------------------------- body -> mind
struct Senses {
  float dt = 0.033f;            // seconds since the last tick

  // hearing
  float loud = 0;               // ambient loudness 0..1 (its own voice removed)
  float bands[8] = {0};         // 0..1, low -> high
  float pitchHz = 0;            // a held outside tone right now, else 0
  bool  onset = false;          // a knock, a clap, a door

  // the body in space
  float tilt = 0;               // left/right lean, degrees
  float motion = 0;             // 0..1 handling / shake energy
  float spin = 0;               // deg/s about the vertical
  bool  flat = false;           // lying on its back or its face
  bool  faceDown = false;
  bool  freefall = false;
  uint32_t steps = 0;           // lifetime-of-boot step count

  // time and place
  bool   haveTime = false;
  double utc = 0;               // unix seconds
  bool   haveFix = false;       // any position at all (GPS, or the named city)
  bool   gpsFix = false;        // a live sky fix
  double lat = 0, lon = 0;
  float  altM = 0, speedKmh = 0;
  int    sats = 0;

  // weather, when a network reaches it
  bool  haveWx = false;
  int   wmo = -1;               // WMO weather code
  float cloud = 0.3f;           // 0..1
  float tempC = 12, humid = 60;
  float pressHpa = 1013, pressTrend = 0;   // trend in hPa per 3 h
  float windKmh = 0, windDir = 0;
  float visKm = 20;

  // the ether (radio environment)
  int   apCount = -1;           // -1 = never listened
  int   apRssi = -100;          // strongest
  bool  etherFresh = false;     // a new census arrived this tick
  float etherNovelty = 0;       // 0 = known ground .. 1 = never heard this air
  int   etherVisits = 0;        // times this radio-place has been met before

  // kin (other missingno within earshot of the link)
  int   peers = 0;
  float peerRootHz = 0;

  // power
  float battV = 0;              // 0 = unknown
  uint8_t card = 0;            // its memory card: 0 none, 1 in use, 2 present but unreadable
  float cardGB = 0;             // the card's size, when one answers

  // loose wires (ext.io antennae), 0..1 agitation; <0 = absent
  float antenna = -1;
};

// ------------------------------------------------------------ mind -> the eye
enum View : uint8_t { V_SHORE = 0, V_SPIRAL, V_SOUNDING, V_CHART, V_COUNT };

struct FaceP {
  float eyeOpen = 1;            // 0 shut .. 1 open .. 1.4 wide
  float gazeX = 0, gazeY = 0;   // -1..1
  float mouthW = 1;             // 1 = the mark's own line
  float mouthOpen = 0;          // 0 line .. 1 a held 'o'
  float mouthWave = 0;          // speaking wobble 0..1
  float glitch = 0;             // 0..1 tearing
  float scatter = 0;            // 0..1 coming apart into tiles
  float shear = 0;              // lean in px across the body height
  float bob = 0;                // px
  float fill = 0.2f;            // interior density; the mark is 20%
  float scrapLift = 0;          // 0..1 how far the loose fragments wander
  float scrapSpin = 0;          // radians
  int   scraps = 2;             // the mark has two
  bool  invert = false;
};

struct SceneP {
  float sunEl = -30, sunAz = 180, moonEl = 20, moonAz = 140;   // degrees
  float moonPhase = 0.3f;       // 0 new .. 0.5 full .. 1 new
  float tide = 0;               // -1 low .. +1 high
  float cloud = 0.3f, fog = 0, rain = 0, snow = 0;             // 0..1
  float wind = 0;               // signed px/s drift
  float flash = 0;              // lightning 0..1
  float starShift = 0;          // px, the sky turning
  bool  south = false;          // southern hemisphere: the sky is mirrored
  float green = 0;              // 0..1 phosphor tint: something is near
  float dread = 0;              // 0..1 edge static
  float wisps = 0;              // 0..1 how many lights are out
  uint8_t omen = 0;             // sky apparition id, 0 = none
  float omenT = 0;              // 0..1 progress
};

// ------------------------------------------------------------ mind -> the voice
struct SoundP {
  float master = 0.5f;          // user volume 0..1
  float rootHz = 110.0f;
  uint8_t mode = 0;             // index into the dark modes
  float drone = 0.7f;           // 0..1
  float lead = 0.3f;            // how often the whistle speaks
  float unease = 0;             // detune, tritones, crackle
  float open = 0.4f;            // echo length/feedback: the room it thinks it is in
  float wind = 0, rain = 0, crackle = 0;   // noise beds 0..1
  float answerHz = 0;           // sing back at this pitch (0 = silent)
  float hush = 0;               // 0..1 sleep / face-down: everything recedes
};

enum NoteKind : uint8_t {
  N_PLUCK = 0,   // a key was touched
  N_BELL,        // something happened
  N_TOCK,        // the sounding rod strikes
  N_THUNDER,
  N_RUPTURE,     // a glitch tears
  N_CHIME_UP,    // resonance achieved
  N_GEIGER,      // one click
  N_VOICE,       // a syllable of its speaking
};
struct Note { uint8_t kind; float hz; float amp; };

// A single-producer / single-consumer ring: main loop -> audio task.
struct NoteQueue {
  static constexpr int N = 16;
  volatile uint8_t head = 0, tail = 0;
  Note q[N];
  bool push(const Note& n) {
    uint8_t nx = (uint8_t)((head + 1) % N);
    if (nx == tail) return false;
    q[head] = n; head = nx; return true;
  }
  bool pop(Note& n) {
    if (tail == head) return false;
    n = q[tail]; tail = (uint8_t)((tail + 1) % N); return true;
  }
};

// The result of striking the room.
struct Sounding {
  bool  valid = false;
  float tailMs = 0;             // how long the room kept the sound
  float floorDb = -60;          // noise floor
  uint8_t env[60] = {0};        // decay envelope, 8 ms hops, 0..255 (log)
  uint8_t verdict = 0;          // 0 open air, 1 close, 2 a room, 3 a hall, 4 vaulted
};

// ----------------------------------------------------------- everything else
constexpr int SPIRAL_N = 720;   // having-been samples kept

struct State {
  // affect: slow dials, 0..1
  float arousal = 0.3f;
  float unease = 0.1f;
  float difference = 0.5f;      // how wide the spiral is right now
  float resonance = 0;
  float openness = 0.4f;
  float sleep = 0;              // 0 awake .. 1 under

  // what is near (sealed places)
  int   presence = 0;           // 0 none, 1 far, 2 near, 3 close, 4 here
  uint8_t placeKind = 0;

  Senses in;                    // the last thing the body told it
  FaceP  face;
  SceneP scene;
  SoundP sound;
  NoteQueue notes;
  Sounding sounding;

  View view = V_SHORE;

  // the one line of text it may speak, and the line being typed to it
  char  say[84] = {0};
  float sayAge = 99;            // seconds since it began
  float sayHold = 6;
  bool  sayCipher = false;      // resolving out of ciphertext
  char  typed[44] = {0};
  float hintAge = 0;            // seconds since boot, for the first-run hint

  // the skyline that sound draws (upstream's terrain, at the horizon)
  uint8_t ridge[240] = {0};
  uint8_t ridgeMark[240] = {0}; // 0 none, else a standing thing
  float ridgeAcc = 0;

  // having-been
  uint8_t spiral[SPIRAL_N] = {0};   // difference over time, 0..255
  uint8_t spiralEv[SPIRAL_N] = {0}; // event marks
  uint16_t spiralLen = 0;
  uint32_t spiralTotal = 0;         // samples ever (lifetime turns)
  float spiralRot = 0;              // view rotation, radians
  float spiralAcc = 0;

  // drift
  uint32_t cellsKnown = 0;
  uint32_t placesFound = 0;
  uint32_t stepsBase = 0;
  uint32_t linesCrossed = 0;

  // light out of the top edge
  uint8_t ledR = 0, ledG = 0, ledB = 0;
  bool ir = false;

  uint32_t frame = 0;
  float t = 0;                  // seconds awake this boot
  bool muted = false;
  bool wantSounding = false;    // main -> body: strike the room now
  bool wantPortal = false;      // main -> body: open the wifi door
  bool wantForget = false;      // main -> body: erase having-been
  bool wantSave = false;        // main -> body: something worth keeping happened
  bool wantFormat = false;      // main -> body: erase an unreadable card and make it FAT32
};

extern State S;

}  // namespace mn
