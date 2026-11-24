# missingno.

*a world-response device for the M5Stack Cardputer ADV*

It is what the map left out: a few scraps of code arranged into a face,
living on the shoreline of a 240 × 135 screen. It listens to the room it is
in, feels how it is carried, keeps solar time, watches the weather, counts the
radio voices in the air, and remembers where it has been. There are also
places in the world it can only read by standing in them.

You don't operate it. You poke it and see what it does.

This is a fork of [SOUND TERRARIUM](https://github.com/kariagepompadour/SOUND-TERRARIUM)
by kariagepompadour (MIT). The hard-won hardware bring-up comes from there: the
ES8311 codec sequence that lets the microphone and speaker run at once, the
direct keyboard and IMU access, the saved Wi-Fi slots and the setup page.
Everything above the hardware is new.

---

## Living with it

| do this | and it |
|---|---|
| type, then **enter** | takes your words, keeps some of them, and answers |
| **enter** on an empty line | strikes the room: a short tock, then it listens to the echo and tells you what kind of space you are in |
| hum a steady note and hold it | answers a fifth above. Hold it for three seconds and it retunes itself to you |
| knock a rhythm | knocks it back |
| tilt it, shake it, spin it, drop it (gently) | leans, comes apart, turns its spiral, tears |
| lay it face down | goes under |
| leave it still and quiet for a few minutes | falls asleep |
| carry it | draws the ground it has walked, notices lines on the map, and feels for hidden places |
| **tab** | turns the view: shore, having-been spiral, sounding, drift chart |
| **G0** (the button on the edge) | strikes the room; hold 1.5 s to mute |

Fn with the arrow keys: **fn ;** / **fn .** volume up/down, **fn ,** / **fn /**
previous/next view, **fn `** back.

Words it obeys: `help`, `wifi`, `mute`, `unmute`, `vol 0`–`vol 9`, `spiral`,
`chart`, `ear`, `shore`, `save`, and `forget` (typed twice, it erases
everything it remembers).

### The views

- **shore** — the mark on its strip of land and water. The sky is computed
  from the clock and the position: the real sun and moon, the moon's phase,
  an idealised tide. Weather arrives as cloud, fog, rain, snow and lightning.
  The skyline on the horizon is drawn by sound.
- **having-been** — a conical spiral of how much the world has been
  *differing*. Novelty widens it, repetition tightens it. Event marks leave the
  ring as lines of flight. When it fills, the past is folded in half.
- **sounding** — the instrument panel: what it hears, the last echo it
  measured, the radio count, pressure, moon, tide, sky, and the five slow dials
  it lives by.
- **drift** — the hundredth-of-a-degree squares of the earth it has stood in.

### What it can sense

| sense | from |
|---|---|
| sound, pitch, knocks, room size | the built-in microphone, with its own voice subtracted |
| lean, handling, spin, freefall, face-down, steps | the built-in BMI270 IMU |
| time | network time, or the GPS clock |
| position | a GPS module if one is attached; otherwise a town you name on the setup page |
| weather, pressure trend, wind, visibility | [Open-Meteo](https://open-meteo.com) over Wi-Fi |
| the radio environment | a passive Wi-Fi census every few minutes (it keeps only hashes of the strongest access points, enough to recognise a place again) |
| others of its kind | ESP-NOW, within a few metres |
| loose wires | the Extend I/O unit on the Grove port, if attached (experimental) |
| its own power | the battery voltage |

With none of the optional parts and no Wi-Fi it still lives: it keeps a
permanent deep twilight, listens, and remembers.

---

## Flashing it

1. Arduino IDE 2, with the **M5Stack** board package 3.2.2 or newer
   (tested with 3.3.7), and the **M5Cardputer**, **M5Unified** and **M5GFX**
   libraries.
2. Open `missingno/missingno.ino`.
3. Tools → Board → M5Stack → **M5Cardputer**, then set both of these:
   - Tools → Flash Size → **8MB (64Mb)**
   - Tools → Partition Scheme → **8M with spiffs (3MB APP/1.5MB SPIFFS)**

   It is about 1.4 MB, too big for the default 1.2 MB scheme; the IDE will
   say "Sketch too big" if you skip the partition scheme. If you change the
   partition scheme but leave Flash Size at its 4MB default, the upload
   looks fine but the device never starts: its bootloader believes the chip
   is 4 MB and refuses a partition table that runs past that. Saved Wi-Fi
   networks are kept across the change.
4. Upload. If the port won't take it, hold **G0** while plugging in USB to enter
   download mode.

Put a microSD card in. It keeps its memory in `/missingno/life.bin` and a
plain-text diary in `/missingno/havingbeen.log`, which you may read. Without a
card it uses the spare flash, and only if that partition is empty.

### Giving it Wi-Fi

Type `wifi` and press enter. It opens a network called **missingno**; join it
from a phone and open `http://192.168.4.1`. Pick your network, and, if you have
no GPS, type the nearest town. Up to five networks are kept, and it rejoins
them by itself when it hears them. Any key closes the page. Close it once
you're done: while it's open, anyone on your home network can reach it too.
On your network it calls itself **missingno**, so that's the name your
router's device list shows.

### Changing it over Wi-Fi

Once it has flashed by USB with a key, later changes can arrive over Wi-Fi.
The key is `missingno/ota_secret.h`, made on your machine and kept out of
git; the firmware holds only its SHA-256. Without that file it builds
without over-the-air updates. While new firmware arrives it goes quiet,
saves its memory, shows *being changed.*, then wakes as the new version.

- From the Arduino IDE: Tools → Port → **missingno** (a network port), then
  Upload. It asks for the password from the `// password:` line of the key
  file.
- From a terminal: `tools/ota_upload.sh path/to/missingno.ino.bin`. The
  address defaults to `missingno.local`; give its IP as a second argument
  if that name doesn't resolve.

The device fetches the firmware by connecting back to your computer, so a
firewall has to let that Python accept incoming connections. If the upload
says "No response from device", that is the usual cause. On a Mac the
script uses the system `/usr/bin/python3`.

Anyone without the key who tries gets refused, and it says so.

### What leaves the device

- Weather requests to Open-Meteo, with the position rounded to two decimals
  (about a kilometre), and the town name if you give one.
- Network time requests.
- An ESP-NOW broadcast every few seconds with its mood, its root note and
  **one short fragment of something typed to it**. It is unencrypted and
  carries only a few metres, but anyone nearby with a radio could read it.
  Don't type secrets to it.

- On your home network only, it announces itself as `missingno.local`
  (mDNS) and listens for firmware updates, which need the key described
  above.

Nothing else is sent anywhere.

---

## Parts worth adding

None of these are needed. Each one opens a sense it doesn't otherwise have.

- **A GPS.** The biggest single addition: true position, the hidden places
  at full strength, the drift chart, line crossings, and a clock that works
  without Wi-Fi. Either the **Cap LoRa-1262 / 868 with GPS** on the back
  connector (it also brings a long-range radio for later, and leaves the Grove
  port free), or a **Unit GPS** on the Grove port. The firmware looks for
  NMEA on both, at 9600 or 115200 baud.
- **A second Cardputer.** Two of them held close notice each other, settle on
  one root note, and trade fragments of what was said to them.
- **IMU Pro Mini** (magnetometer + barometer): a compass and a real pressure
  trend without the network. Not yet read by the firmware.
- **A Geiger counter pulse module**: true randomness, and a reason for the
  clicks. Not yet read by the firmware.
- **ENV / BME688**: air quality and gas, for rooms that feel wrong. Not yet
  read by the firmware.
- **A Grove hub**, if you want the Extend I/O and a Grove GPS at once.

The **Extend I/O unit** (STM32F0, I²C 0x45) on the Grove port is used as four
floating analogue "antennae": loose wires on its first four pins pick up hands,
mains hum and static, and the device feels that as agitation. This is
experimental; leave the pins unconnected or attach short bare wires.

---

## For whoever changes it

```
missingno/missingno.ino    the body: pins, audio task, Wi-Fi, GPS, storage, kin
missingno/src/core/        the mind, renderer, synth and ear (no hardware in here)
sim/                       host simulator and tests (clang++ on a Mac or Linux)
tools/seal_places.py       seals the hidden places into mn_places.h
tools/SPOILERS_places.txt  the hidden places in plain text: don't open it
```

The core never touches a pin. The body fills `mn::Senses`; the mind writes
`mn::State`; the renderer and synth read it. So the same code runs on the Mac:

```bash
cd sim && mkdir -p out
clang++ -std=c++17 -O2 -o out/sim sim.cpp ../missingno/src/core/*.cpp
./out/sim            # unit checks, then fifteen scripted minutes of life
python3 sheet.py     # contact sheet of the frames it drew
```

The hidden places are sealed: each entry is encrypted with a key derived from
the cell of the earth it belongs to, so the firmware can only read it by being
there. After editing the spoiler file, run `python3 tools/seal_places.py`.
The spoiler file is kept out of git by default, so a public fork stays a
surprise; the sealed header is all a build needs.

Things that still need tuning on real hardware: the echo-length thresholds,
the microphone's coupling to its own speaker, which way is face-down on the
IMU, and the Extend I/O sensitivity.

## Licence

MIT, as upstream. See `LICENSE`.
