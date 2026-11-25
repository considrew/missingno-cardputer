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
`chart`, `ear`, `shore`, `save`, `pro` (the instruments, below), `theremin`
(its voice, played by tilting it, below), `format`
(typed twice, it erases a card it cannot read and makes it its own), and
`forget` (typed twice, it erases everything it remembers).

### The views

- **shore** — the mark on its strip of land and water. The sky is computed
  from the clock and the position: the real sun and moon, the moon's phase,
  an idealised tide. Weather arrives as cloud, fog, rain, snow and lightning.
  The skyline on the horizon is drawn by sound. Behind it, if you give it
  one, stand the real hills and mountains of where it is (see below).
- **having-been** — a conical spiral of how much the world has been
  *differing*. Novelty widens it, repetition tightens it. Event marks leave the
  ring as lines of flight. When it fills, the past is folded in half.
- **sounding** — the instrument panel: what it hears, the last echo it
  measured, the radio count, pressure, moon, tide, sky, and the five slow dials
  it lives by.
- **drift** — the hundredth-of-a-degree squares of the earth it has stood in.

### pro: the instruments

Type `pro` and the shore gives way to a dashboard of everything it can
sense, laid out by **reach**: how far away the thing each instrument
measures is, from its own body out to the sun.

| reach | | instruments |
|---|---|---|
| SELF | 0 m | power, memory, link (enter opens Wi-Fi setup), system |
| HAND | 1 m | level (a bubble lying flat; roll and pitch upright), motion (g, spin, falls), steps, wires, theremin |
| ROOM | 10 m | meter (level, spectrum, history), tuner, echo (enter strikes the room), knock (tempo) |
| RF | 100 m | ether (the Wi-Fi networks it hears), kin |
| AIR | 10 km | temperature and dew point, pressure and its trend, wind, weather |
| LAND | 100 km | position, horizon (the real skyline and its highest point), drift |
| SAT | 20,000 km | GPS, clock (local, UTC, and by the sun) |
| SKY | 1 AU | sun (rise and set, and when it meets your ridge), moon, tide |

`,` and `/` (the left and right arrows) walk outward and back, and `;` and
`.` (up and down) pick an instrument. Enter opens it. In an open
instrument, enter uses it where there is something to do (strike the room,
reset a peak), backspace closes it, and the arrows go on through the rest.
Escape returns to the shore. Once you have started typing, those keys type.
`pro tuner`, or any instrument's name, goes straight to that one. The
creature keeps living underneath, and what it says runs along the bottom
line.

The meter reads decibels below the microphone's full scale, not calibrated
sound pressure. Local time comes from the weather service's time zone for
where it is; without that, the clock shows UTC.

### theremin

Type `theremin` and it gives its voice to your hand. The accelerometer is
the instrument and the gyroscope is the expression:

| do this | and it |
|---|---|
| roll it left or right | plays lower or higher: three octaves across about 110 degrees |
| tip it forward or back from how you held it when you typed the word | fades; about 55 degrees away (or face down) it is silent |
| twist it about the vertical, back and forth | trembles: the faster the twist, the deeper the vibrato |
| move it quickly | the tone gets an edge |
| `;` and `.` (the up and down arrows) | octave up and down |
| `,` and `/` | glide (no notes, a true theremin), chromatic, its own mood's scale, or one of five dark scales |
| enter, or **G0** | holds the note, so you can put your hand down |
| escape | stops (back to the dashboard, if that is where it came from) |

The middle note is its own root, folded up to sit near a4, so it is always
in tune with the drone (which steps back while you play) and high enough
for the little speaker; `;` and `.` move it from three octaves down to two up. The creature sings it: its
mouth opens with the volume, and the melody falls down the screen behind
it. It is also on the dashboard, under HAND. Which way is "higher" depends
on how the IMU sits in the case; if it feels backwards, it is one sign to
flip in `mn_theremin.cpp`.

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

### The real horizon

The shore can show the land that actually stands around you: the near
ground, the hills across the valley, the far ranges, each paler with
distance. It is pre-rendered on your computer from public elevation data and
then carried by the device:

```bash
python3 tools/horizon.py --town "Innsbruck"
python3 tools/horizon.py --lat 47.61 --lon -122.33 --radius-km 8 --step-km 1
```

For every point of a grid around the place (17 x 17 points a kilometre apart
by default, about a minute's work) it casts 720 rays out to 200 km and keeps,
for each degree of the compass, how high the land stands above the level
line, allowing for the curve of the earth and the bending of light near the
ground. That skyline is flattened onto the shore's own vertical scale, the
square-root curve the sun and moon are placed on, so the sun goes behind
your ridge at the minute it really does, the moon comes up from behind it,
and the high crests catch the light at dusk after the valley has lost it.
Haze and fog take the far ranges first, by the visibility in the weather.
The device notices the sun going behind the land and says so.

Elevation comes from [USGS 3DEP](https://www.usgs.gov/3d-elevation-program)
(1 arc-second, bare earth) inside the US and
[Copernicus GLO-30](https://spacedata.copernicus.eu/collections/copernicus-digital-elevation-model)
everywhere else. Only the tiles, and the resolution each distance needs, are
fetched; they are cached in `~/.cache/missingno-dem`, so the first run
downloads a few tens of megabytes and later runs nothing. Needs `numpy`, and
`Pillow` for the preview.

It writes `tools/horizon_out/horizon.bin`, a preview `horizon.png`, and
`missingno/horizon_local.h`. Build and upload (over Wi-Fi is fine) and the
horizon is part of the firmware; or copy `horizon.bin` to the card as
`/missingno/horizon.bin`, which wins over the built-in one. Away from the
grid it falls back to the plain horizon. Both files say where you live, so
both are kept out of git.

Running it asks AWS (Copernicus), USGS, and, for `--town`, Open-Meteo's
place search for data about that area. Nothing is sent from the device.

Copernicus DEM: produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014
and © Airbus Defence and Space GmbH 2014-2018, provided under COPERNICUS by
the European Union and ESA; all rights reserved. USGS 3DEP is in the public
domain.

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
tools/horizon.py           pre-renders the real horizon around a place
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
