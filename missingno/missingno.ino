/*
  missingno. -- a world-response device for the M5Stack Cardputer ADV.

  A fork of SOUND TERRARIUM by kariagepompadour (MIT licence, see LICENSE).
  What is kept from upstream, because it was hard-won on real hardware:
    - the ES8311 register sequence and the full-duplex I2S0 setup (mic + speaker at once)
    - the direct TCA8418 keyboard polling and the BMI270 bring-up + Bosch step counter
    - the saved Wi-Fi slots, the browser setup page, and the small JSON helpers
    - the rule that M5Cardputer.begin() is never called, so nothing else claims I2S0
  Everything else is new. This file is only the *body*: it turns pins into
  `mn::Senses` and turns `mn::State` back into light and sound. The mind, the
  renderer and the synth live in src/core/ and know nothing about hardware,
  which is why the same code also runs in the host simulator (sim/).

  Board: M5Cardputer (M5Stack board manager 3.2.2 or newer). Libraries: M5Cardputer, M5Unified, M5GFX.
*/
#include <Arduino.h>
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <utility/imu/BMI270_Class.hpp>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "driver/i2s_std.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <sd_diskio.h>
#include <LittleFS.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <ArduinoOTA.h>
#if __has_include("ota_secret.h")
#include "ota_secret.h"          // MN_OTA_HASH: lets it be changed over Wi-Fi. Kept out of git.
#endif

#include "src/core/mn_gfx.h"
#include "src/core/mn_state.h"
#include "src/core/mn_draw.h"
#include "src/core/mn_mind.h"
#include "src/core/mn_synth.h"
#include "src/core/mn_ear.h"
#include "src/core/mn_nmea.h"
#include "src/core/mn_drift.h"
#include "src/core/mn_horizon.h"
#if __has_include("horizon_local.h")
#include "horizon_local.h"        // MN_HORIZON_BLOB: the land around home, from tools/horizon.py. Kept out of git.
#define MN_HAVE_HORIZON 1
#endif

// ------------------------------------------------------------------ the pins
static constexpr int SAMPLE_RATE = 48000;
static constexpr uint8_t ES8311_ADDR = 0x18;
static constexpr int ADV_I2C_SDA = 8;
static constexpr int ADV_I2C_SCL = 9;
static constexpr gpio_num_t ADV_MIC_BCLK = GPIO_NUM_41;
static constexpr gpio_num_t ADV_MIC_WS   = GPIO_NUM_43;
static constexpr gpio_num_t ADV_MIC_DIN  = GPIO_NUM_46;
static constexpr gpio_num_t ADV_SPK_DOUT = GPIO_NUM_42;

static constexpr uint8_t TCA8418_ADDR = 0x34;
static constexpr uint8_t TCA_REG_CFG = 0x01;
static constexpr uint8_t TCA_REG_KEY_LCK_EC = 0x03;
static constexpr uint8_t TCA_REG_KEY_EVENT_A = 0x04;
static constexpr uint8_t TCA_REG_KP_GPIO_1 = 0x1D;
static constexpr uint8_t TCA_REG_KP_GPIO_2 = 0x1E;
static constexpr uint8_t TCA_REG_KP_GPIO_3 = 0x1F;

static constexpr uint8_t BMI270_ADDR = 0x69;
static constexpr uint8_t BMI270_ACC_X_LSB = 0x0C;
static constexpr uint8_t BMI270_ACC_CONF_ADDR = 0x40;
static constexpr uint8_t BMI270_ACC_RANGE_ADDR = 0x41;
static constexpr uint8_t BMI270_STEP_ACC_CONF = 0xA8;   // perf=1, bwp=AVG4, odr=100Hz
static constexpr uint8_t BMI270_STEP_ACC_RANGE = 0x00;  // +/-2 g
static constexpr uint8_t BMI270_FEAT_PAGE_REG = 0x2F;
static constexpr uint8_t BMI270_FEATURES_REG  = 0x30;
static constexpr uint8_t BMI270_STEP_CFG_PAGE = 0x06;
static constexpr uint8_t BMI270_STEP_CFG_START = 0x02;
static constexpr uint8_t BMI270_STEP_ENABLE_OFFSET = 0x01;
static constexpr uint8_t BMI270_STEP_ENABLE_MASK = 0x10;

static constexpr int PIN_BUTTON = 0;        // G0, the one button on the edge
static constexpr int PIN_BATTERY = 10;      // half the cell voltage
static constexpr int PIN_RGB = 21;          // the Stamp's own pixel
static constexpr int PIN_IR = 44;           // infrared: a heartbeat only a camera can see
static constexpr int PIN_SD_SCK = 40, PIN_SD_MISO = 39, PIN_SD_MOSI = 14, PIN_SD_CS = 12;
static constexpr int PIN_GROVE_SDA = 2, PIN_GROVE_SCL = 1;
static constexpr uint8_t EXTIO_ADDR = 0x45; // the STM32F0 Extend I/O unit, if present

static constexpr int WIFI_SLOT_COUNT = 5;
static const char* SETUP_AP_SSID = "missingno";

// ------------------------------------------------------------ shared structures
struct NetJob {
  volatile bool wantWx = false, wantGeo = false;
  double lat = 0, lon = 0;
  char city[84] = {0};
};
struct NetResult {
  volatile bool wxFresh = false, geoDone = false, geoOk = false;
  int wmo = -1; float cloud = 0.3f, tempC = 12, humid = 60, press = 1013, trend = 0, windKmh = 0, windDir = 0, visKm = 20;
  double geoLat = 0, geoLon = 0;
};
struct KinSeen { uint32_t id; uint32_t ms; float rootHz; char scrap[40]; bool scrapNew; };
struct KinPacket { uint32_t magic; uint32_t id; float rootHz; uint8_t unease, arousal, seq, pad; char scrap[40]; };
static constexpr uint32_t KIN_MAGIC = 0x304E4D2EUL;   // ".MN0"

static i2s_chan_handle_t micChan = nullptr, spkChan = nullptr;
static bool micReady = false, imuReady = false, keysReady = false, stepReady = false;
static uint32_t stepOrigin = 0;
static Preferences prefs, wifiPrefs;
static WebServer portalServer(80);
static HardwareSerial GpsSerial(1);
static mn::Nmea nmea;
static NetJob netJob;
static NetResult netRes;
static KinSeen kin[4];
static volatile bool kinInbox = false;
static KinPacket kinIn; static volatile int kinInRssi = -100;

static volatile uint32_t lastKeyMs = 0;          // when someone was last looking at the screen
static fs::FS* storeFs = nullptr;
static const char* storeName = "none";
static uint8_t cardState = 0;                 // 0 none, 1 in use, 2 present but unreadable
static float cardGB = 0;

// Wi-Fi
enum WifiStage : uint8_t { WS_IDLE = 0, WS_CONNECTING, WS_ONLINE };
static WifiStage wifiStage = WS_IDLE;
static uint32_t wifiDeadline = 0, lastCensusMs = 0, lastWxMs = 0;
static bool censusRunning = false, sntpStarted = false, espNowOk = false;
static double wxLat = 1000, wxLon = 1000;
// Over-the-air updates: started once it is online.
static bool otaReady = false;
// Silence while it is being rewritten or a card is being erased.
static volatile bool hushed = false;
static bool portalOpen = false; static String portalStatus = "waiting";
static String pendSsid, pendPass, pendCity; static uint8_t pendStage = 0; static uint32_t pendMs = 0;
static bool haveNamed = false; static double namedLat = 0, namedLon = 0;

// GPS
static const int GPS_RX_ALL[4] = {13, 15, PIN_GROVE_SCL, PIN_GROVE_SDA};
static const uint32_t GPS_BAUDS[2] = {115200, 9600};
static int gpsCombo = 0, gpsCombos = 8; static bool gpsLocked = false, groveIsI2C = false, extioReady = false;
static uint32_t gpsTryMs = 0, gpsGoodAtTry = 0, gpsLastSentenceMs = 0, gpsLastFixMs = 0, gpsClockSetMs = 0;

// ------------------------------------------------------------- upstream: codec
static bool es8311Write(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(ES8311_ADDR);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}

static bool setupCodec() {
  // Bruce's ADV mic sequence, with upstream's corrections:
  // REG01 = 0xBF turns on both the ADC and the DAC clocks (mic and speaker together).
  bool ok = true;
  ok &= es8311Write(0x00, 0x80);
  ok &= es8311Write(0x01, 0xBF);
  ok &= es8311Write(0x02, 0x18);
  ok &= es8311Write(0x0D, 0x01);
  ok &= es8311Write(0x0E, 0x02);
  ok &= es8311Write(0x14, 0x16);   // mic1 differential, +18 dB analog gain
  ok &= es8311Write(0x16, 0x02);   // +12 dB ADC scale
  ok &= es8311Write(0x17, 0xBF);   // ADC digital volume 0 dB
  ok &= es8311Write(0x1C, 0x6A);
  ok &= es8311Write(0x12, 0x00);   // DAC powered
  ok &= es8311Write(0x13, 0x10);
  ok &= es8311Write(0x32, 0xBF);   // DAC volume 0 dB: the synth sets its own level, and it is a quiet one
  ok &= es8311Write(0x37, 0x08);
  delay(20);
  return ok;
}

static bool initAudio() {
  Wire1.end();
  Wire1.begin(ADV_I2C_SDA, ADV_I2C_SCL);
  delay(20);
  if (!setupCodec()) return false;

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num = 8;
  chanCfg.dma_frame_num = 124;
  if (i2s_new_channel(&chanCfg, &spkChan, &micChan) != ESP_OK) return false;

  i2s_std_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.clk_cfg.clk_src = I2S_CLK_SRC_PLL_160M;
  cfg.clk_cfg.sample_rate_hz = SAMPLE_RATE;
  cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
  cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
  cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
  cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  cfg.slot_cfg.ws_width = 16;
  cfg.slot_cfg.bit_shift = true;
  cfg.slot_cfg.left_align = true;
  cfg.slot_cfg.big_endian = false;
  cfg.slot_cfg.bit_order_lsb = false;
  cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  cfg.gpio_cfg.bclk = ADV_MIC_BCLK;
  cfg.gpio_cfg.ws   = ADV_MIC_WS;
  cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
  cfg.gpio_cfg.din  = ADV_MIC_DIN;
  if (i2s_channel_init_std_mode(micChan, &cfg) != ESP_OK) return false;

  // Mono TX has to feed both wire slots for the ES8311; RX stays on the left.
  cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
  cfg.gpio_cfg.dout = ADV_SPK_DOUT;
  cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(spkChan, &cfg) != ESP_OK) return false;
  if (i2s_channel_enable(micChan) != ESP_OK) return false;
  if (i2s_channel_enable(spkChan) != ESP_OK) return false;
  return true;
}

// One task owns all sound: the synth out, the microphone in, in lockstep.
static void audioTask(void*) {
  static int16_t out[256], mic[256];
  for (;;) {
    if (hushed) memset(out, 0, sizeof(out));              // hush while it is being rewritten
    else { mn::synthDuck(mn::earDuck()); mn::synthRender(out, 256); }
    size_t wrote = 0;
    i2s_channel_write(spkChan, out, sizeof(out), &wrote, portMAX_DELAY);
    size_t got = 0;
    if (i2s_channel_read(micChan, mic, sizeof(mic), &got, pdMS_TO_TICKS(40)) == ESP_OK && got >= 2 && !hushed) {
      if (mn::earPush(mic, (int)(got / 2), mn::synthOwnRms(), mn::synthTransientAge())) mn::synthTock();
    }
  }
}

// ---------------------------------------------------------- upstream: keyboard
static bool tcaWrite(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(TCA8418_ADDR);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}
static int tcaRead(uint8_t reg) {
  Wire1.beginTransmission(TCA8418_ADDR);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return -1;
  if (Wire1.requestFrom((int)TCA8418_ADDR, 1) != 1) return -1;
  return Wire1.read();
}
static bool initKeys() {
  Wire1.beginTransmission(TCA8418_ADDR);
  if (Wire1.endTransmission() != 0) return false;
  bool ok = true;
  ok &= tcaWrite(TCA_REG_KP_GPIO_1, 0x7F);   // 7 rows
  ok &= tcaWrite(TCA_REG_KP_GPIO_2, 0xFF);   // 8 columns
  ok &= tcaWrite(TCA_REG_KP_GPIO_3, 0x00);
  for (int i = 0; i < 16; i++) {             // drain stale events
    int n = tcaRead(TCA_REG_KEY_LCK_EC);
    if (n < 0 || (n & 0x0F) == 0) break;
    (void)tcaRead(TCA_REG_KEY_EVENT_A);
  }
  int cfg = tcaRead(TCA_REG_CFG);
  if (cfg >= 0) ok &= tcaWrite(TCA_REG_CFG, (uint8_t)(cfg | 0x01));
  return ok;
}
static void mapTcaRawToPhysical(uint8_t keyvalue, uint8_t& row, uint8_t& col) {
  const uint8_t u = keyvalue % 10;   // 1..8
  const uint8_t t = keyvalue / 10;   // 0..6
  if (u >= 1 && u <= 8 && t <= 6) {
    const uint8_t u0 = u - 1;
    row = u0 & 0x03;
    col = (t << 1) | (u0 >> 2);
  } else { row = 0xFF; col = 0xFF; }
}

// The printed legends, plain and shifted. 0 = a modifier.
static const char KEYMAP[4][14][2] = {
  {{'`', '~'}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'}, {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'}, {'=', '+'}, {8, 8}},
  {{'\t', '\t'}, {'q', 'Q'}, {'w', 'W'}, {'e', 'E'}, {'r', 'R'}, {'t', 'T'}, {'y', 'Y'}, {'u', 'U'}, {'i', 'I'}, {'o', 'O'}, {'p', 'P'}, {'[', '{'}, {']', '}'}, {'\\', '|'}},
  {{0, 0}, {0, 0}, {'a', 'A'}, {'s', 'S'}, {'d', 'D'}, {'f', 'F'}, {'g', 'G'}, {'h', 'H'}, {'j', 'J'}, {'k', 'K'}, {'l', 'L'}, {';', ':'}, {'\'', '"'}, {'\n', '\n'}},
  {{0, 0}, {0, 0}, {0, 0}, {'z', 'Z'}, {'x', 'X'}, {'c', 'C'}, {'v', 'V'}, {'b', 'B'}, {'n', 'N'}, {'m', 'M'}, {',', '<'}, {'.', '>'}, {'/', '?'}, {' ', ' '}},
};

static void portalClose();

static void pollKeys() {
  if (!keysReady) return;
  static bool fnHeld = false, shiftHeld = false;
  int available = tcaRead(TCA_REG_KEY_LCK_EC);
  if (available < 0) return;
  int count = available & 0x0F;
  while (count-- > 0) {
    int ev = tcaRead(TCA_REG_KEY_EVENT_A);
    if (ev <= 0) break;
    bool pressed = (ev & 0x80) != 0;
    uint8_t row, col;
    mapTcaRawToPhysical((uint8_t)(ev & 0x7F), row, col);
    if (row > 3 || col > 13) continue;
    if (row == 2 && col == 0) { fnHeld = pressed; continue; }
    if (row == 2 && col == 1) { shiftHeld = pressed; continue; }
    if (row == 3 && col <= 2) continue;                       // ctrl, opt, alt: unused
    if (!pressed) continue;
    lastKeyMs = millis();
    if (portalOpen) { portalClose(); continue; }              // any key shuts the door
    char plain = KEYMAP[row][col][0];
    if (fnHeld) {                                             // the arrows printed on ; . , /
      if (plain == ';') mn::mindKey(mn::K_VOL_UP);
      else if (plain == '.') mn::mindKey(mn::K_VOL_DOWN);
      else if (plain == ',') mn::mindKey(mn::K_VIEW_PREV);
      else if (plain == '/') mn::mindKey(mn::K_VIEW_NEXT);
      else if (plain == '`' || plain == 8) mn::mindKey(mn::K_ESC);
      continue;
    }
    char c = KEYMAP[row][col][shiftHeld ? 1 : 0];
    if (c == '\n') mn::mindKey(mn::K_ENTER);
    else if (c == 8) mn::mindKey(mn::K_BACK);
    else if (c == '\t') mn::mindKey(mn::K_TAB);
    else if (c) mn::mindKey(c);
  }
}

static void pollButton() {
  static bool was = false, fired = false; static uint32_t downMs = 0;
  bool down = digitalRead(PIN_BUTTON) == LOW;
  uint32_t now = millis();
  if (down && !was) { downMs = now; fired = false; }
  if (down && !fired && now - downMs > 1500) { fired = true; mn::mindButton(true); }
  if (!down && was && !fired && now - downMs > 30) {
    if (portalOpen) portalClose(); else mn::mindButton(false);
  }
  was = down;
}

// --------------------------------------------------------------- upstream: IMU
static bool bmiWriteReg(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(BMI270_ADDR);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}
static bool bmiReadRegs(uint8_t reg, uint8_t* dst, size_t len) {
  Wire1.beginTransmission(BMI270_ADDR);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((int)BMI270_ADDR, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) dst[i] = Wire1.read();
  return true;
}
static bool bmiWriteRegs(uint8_t reg, const uint8_t* src, size_t len) {
  Wire1.beginTransmission(BMI270_ADDR);
  Wire1.write(reg);
  for (size_t i = 0; i < len; i++) Wire1.write(src[i]);
  return Wire1.endTransmission() == 0;
}
static bool bmiConfigureAccel() {
  bool ok = bmiWriteReg(BMI270_ACC_CONF_ADDR, BMI270_STEP_ACC_CONF);
  if (ok) ok = bmiWriteReg(BMI270_ACC_RANGE_ADDR, BMI270_STEP_ACC_RANGE);
  delay(5);
  uint8_t conf = 0, range = 0;
  if (ok) ok = bmiReadRegs(BMI270_ACC_CONF_ADDR, &conf, 1);
  if (ok) ok = bmiReadRegs(BMI270_ACC_RANGE_ADDR, &range, 1);
  return ok && conf == BMI270_STEP_ACC_CONF && (range & 0x03) == BMI270_STEP_ACC_RANGE;
}
static bool bmiEnableStepCounter() {
  uint8_t page[16];
  bool ok = bmiWriteReg(BMI270_FEAT_PAGE_REG, BMI270_STEP_CFG_PAGE);
  if (ok) ok = bmiReadRegs(BMI270_FEATURES_REG, page, sizeof(page));
  if (ok) {
    const uint8_t idx = BMI270_STEP_CFG_START + BMI270_STEP_ENABLE_OFFSET;
    page[idx] |= BMI270_STEP_ENABLE_MASK;
    ok = bmiWriteRegs(BMI270_FEATURES_REG, page, sizeof(page));
  }
  bool restore = bmiWriteReg(BMI270_FEAT_PAGE_REG, 0x00);
  delay(2);
  return ok && restore;
}
static bool bmiReadStepCount(uint32_t& count) {
  uint8_t b[4];
  if (!bmiWriteReg(BMI270_FEAT_PAGE_REG, 0x00)) return false;
  if (!bmiReadRegs(BMI270_FEATURES_REG, b, sizeof(b))) return false;
  count = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  return true;
}
static bool initImu() {
  // M5Unified uploads the Bosch config blob; we borrow only that, never M5.begin().
  M5Cardputer.In_I2C.begin((i2c_port_t)I2C_NUM_1, ADV_I2C_SDA, ADV_I2C_SCL);
  m5::BMI270_Class bmi(BMI270_ADDR, 400000, &M5Cardputer.In_I2C);
  bool ok = (bool)bmi.begin(&M5Cardputer.In_I2C);
  Wire1.end();
  Wire1.begin(ADV_I2C_SDA, ADV_I2C_SCL);
  delay(5);
  if (ok) {
    stepReady = bmiConfigureAccel() && bmiEnableStepCounter();
    uint32_t raw = 0;
    if (stepReady && bmiReadStepCount(raw)) stepOrigin = raw; else stepReady = false;
  }
  return ok;
}

// What the body feels: lean, handling, spin, falling, lying on its face.
static void readImu(mn::Senses& in) {
  static uint32_t lastMs = 0, lastStepMs = 0, faceDownSince = 0;
  static float gx = 0, gy = 0, gz = 1, motionE = 0; static int fallRun = 0;
  if (!imuReady) return;
  uint32_t now = millis();
  if (now - lastMs < 20) return;
  lastMs = now;
  uint8_t b[12];
  if (!bmiReadRegs(BMI270_ACC_X_LSB, b, 12)) return;       // accel xyz, then gyro xyz
  float a[3], w[3];
  for (int i = 0; i < 3; i++) {
    a[i] = (int16_t)(((uint16_t)b[i * 2 + 1] << 8) | b[i * 2]) / 16384.0f;          // +/-2 g
    w[i] = (int16_t)(((uint16_t)b[7 + i * 2] << 8) | b[6 + i * 2]) / 16.384f;        // +/-2000 deg/s
  }
  gx += (a[0] - gx) * 0.12f; gy += (a[1] - gy) * 0.12f; gz += (a[2] - gz) * 0.12f;   // where down is
  float dx = a[0] - gx, dy = a[1] - gy, dz = a[2] - gz;
  float jolt = sqrtf(dx * dx + dy * dy + dz * dz);
  motionE += (jolt - motionE) * (jolt > motionE ? 0.5f : 0.06f);
  float m = motionE / 0.55f; in.motion = m > 1 ? 1 : m;
  float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  fallRun = mag < 0.3f ? fallRun + 1 : 0;
  in.freefall = fallRun >= 3;
  float gl = sqrtf(gx * gx + gy * gy + gz * gz); if (gl < 0.2f) gl = 0.2f;
  // Axes as upstream found them: held in front of you, gravity runs along z
  // (with x for left/right lean), and y is the axis through the screen.
  float tilt = atan2f(gx, gz) * 57.29578f;
  if (tilt > 90) tilt = 180 - tilt; else if (tilt < -90) tilt = -180 - tilt;
  in.tilt = tilt;
  in.spin = (w[0] * gx + w[1] * gy + w[2] * gz) / gl;      // turning about the vertical, in any pose
  float through = gy / gl;
  in.flat = fabsf(through) > 0.93f;
  // Which sign of y means "screen up" is learned: whenever a key is pressed,
  // someone is looking at the screen, so the screen faces them.
  static int upSign = 0;
  if (!upSign) upSign = prefs.getInt("upsign", 0);
  if (now - lastKeyMs < 600 && fabsf(through) > 0.4f) {
    int s = through > 0 ? 1 : -1;
    if (s != upSign) { upSign = s; prefs.putInt("upsign", s); }
  }
  bool down = upSign != 0 && through * upSign < -0.8f;
  if (down) { if (!faceDownSince) faceDownSince = now; } else faceDownSince = 0;
  in.faceDown = faceDownSince && now - faceDownSince > 1200;
  if (stepReady && now - lastStepMs > 1000) {
    lastStepMs = now;
    uint32_t raw;
    if (bmiReadStepCount(raw) && raw >= stepOrigin) in.steps = raw - stepOrigin;
  }
}

// ------------------------------------------------------------------- storage
static bool internalIsBlank() {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!p) return false;
  uint8_t b[32];
  for (uint32_t off : {0u, 4096u, 8192u}) {
    if (esp_partition_read(p, off, b, sizeof(b)) != ESP_OK) return false;
    for (uint8_t v : b) if (v != 0xFF) return false;
  }
  return true;
}

// A card that answers but holds no filesystem it can read (exFAT, usually: any card over 32 GB).
// Wake it far enough to learn its type and size, then let it go again.
static void cardProbe() {
  uint8_t pdrv = sdcard_init(PIN_SD_CS, &SPI, 20000000);
  if (pdrv == 0xFF) return;
  sdcard_mount(pdrv, "/sdprobe", 1, false);                  // fails, but initialises the card first
  sdcard_type_t t = sdcard_type(pdrv);
  if (t == CARD_SD || t == CARD_SDHC || t == CARD_MMC) {
    cardState = 2; cardGB = (float)((double)sdcard_num_sectors(pdrv) * 512.0 / 1.0e9);
  }
  sdcard_unmount(pdrv);
  sdcard_uninit(pdrv);
}

static void storeBegin() {
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (SD.begin(PIN_SD_CS, SPI, 20000000)) {
    storeFs = &SD; storeName = "card"; cardState = 1; cardGB = SD.cardSize() / 1.0e9f;
  } else {
    SD.end();
    cardProbe();
    // No card: use the flash's data partition, but never format over someone else's files.
    bool blank = internalIsBlank();
    if (LittleFS.begin(blank, "/littlefs", 5, "spiffs")) { storeFs = &LittleFS; storeName = "flash"; }
  }
  if (storeFs && !storeFs->exists("/missingno")) storeFs->mkdir("/missingno");
}

static bool storeLoadLife() {
  if (!storeFs) return false;
  for (const char* path : {"/missingno/life.bin", "/missingno/life.tmp"}) {
    File f = storeFs->open(path, FILE_READ);
    if (!f) continue;
    size_t n = f.size();
    bool ok = false;
    if (n == sizeof(mn::Life)) {
      uint8_t* buf = (uint8_t*)malloc(n);
      if (buf) { ok = f.read(buf, n) == n && mn::lifeLoad(buf, n); free(buf); }
    }
    f.close();
    if (ok) return true;
  }
  return false;
}

static bool storeSaveLife() {
  if (!storeFs) return false;
  mn::mindLifeOut();
  size_t n; const uint8_t* b = mn::lifeBytes(n);
  File f = storeFs->open("/missingno/life.tmp", FILE_WRITE);
  if (!f) return false;
  bool ok = f.write(b, n) == n;
  f.close();
  if (!ok) return false;
  storeFs->remove("/missingno/life.bin");
  return storeFs->rename("/missingno/life.tmp", "/missingno/life.bin");
}

// A plain-text diary on the card. It is allowed to be read.
// ------------------------------------------------------------- the real horizon
// tools/horizon.py pre-renders the skyline around a place. Read from the card (or the
// flash store) if a horizon.bin is there, else from a copy built into the firmware.
static File hzFile;
static const char* hzFrom = "none";
static bool hzReadFile(uint32_t off, void* dst, uint32_t n) {
  return hzFile && hzFile.seek(off) && hzFile.read((uint8_t*)dst, n) == n;
}
#ifdef MN_HAVE_HORIZON
static bool hzReadBlob(uint32_t off, void* dst, uint32_t n) {
  if ((uint64_t)off + n > sizeof(MN_HORIZON_BLOB)) return false;
  memcpy(dst, MN_HORIZON_BLOB + off, n);
  return true;
}
#endif
static void horizonBegin() {
  if (storeFs && storeFs->exists("/missingno/horizon.bin")) {
    hzFile = storeFs->open("/missingno/horizon.bin", FILE_READ);
    if (hzFile && mn::horizonOpen(hzReadFile, hzFile.size())) { hzFrom = storeName; return; }
    if (hzFile) hzFile.close();
  }
#ifdef MN_HAVE_HORIZON
  if (mn::horizonOpen(hzReadBlob, sizeof(MN_HORIZON_BLOB))) hzFrom = "built in";
#endif
}

static void journal(const char* what) {
  if (!storeFs) return;
  const char* path = "/missingno/havingbeen.log";
  File f = storeFs->open(path, FILE_APPEND);
  if (!f) return;
  if (f.size() > 512000) {                                   // porous: the old half is let go
    f.close();
    storeFs->remove("/missingno/havingbeen.old");
    storeFs->rename(path, "/missingno/havingbeen.old");
    f = storeFs->open(path, FILE_APPEND);
    if (!f) return;
  }
  time_t t = time(nullptr);
  if (t > 1735689600) { struct tm tmv; gmtime_r(&t, &tmv); f.printf("%04d-%02d-%02d %02d:%02dZ ", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min); }
  else f.printf("t+%lus ", (unsigned long)(millis() / 1000));
  if (mn::S.in.haveFix) f.printf("%.3f,%.3f ", mn::S.in.lat, mn::S.in.lon);
  f.println(what);
  f.close();
}

// ----------------------------------------------------- upstream: JSON helpers
static bool jsonIsWs(char c){
  return c==' ' || c=='\t' || c=='\r' || c=='\n';
}

// Find a named value while tolerating JSON whitespace around the colon.
// Optional 'to' bounds the search to one already-identified JSON container.
static int jsonValuePos(const String &s,const char* key,int from=0,int to=-1){
  const int len=(int)s.length();
  if(to<0 || to>len) to=len;
  if(from<0) from=0;
  if(from>=to) return -1;
  String token=String("\"")+key+"\"";
  int p=from;
  while((p=s.indexOf(token,p))>=0 && p<to){
    int q=p+token.length();
    while(q<to && jsonIsWs(s[q])) q++;
    if(q<to && s[q]==':'){
      q++;
      while(q<to && jsonIsWs(s[q])) q++;
      return q<to ? q : -1;
    }
    p+=token.length();
  }
  return -1;
}

static int jsonMatchingClose(const String &s,int openPos,char openCh,char closeCh,int to=-1){
  const int len=(int)s.length();
  if(to<0 || to>len) to=len;
  if(openPos<0 || openPos>=to || s[openPos]!=openCh) return -1;
  int depth=0;
  bool inString=false,esc=false;
  for(int i=openPos;i<to;i++){
    char c=s[i];
    if(inString){
      if(esc){ esc=false; continue; }
      if(c=='\\'){ esc=true; continue; }
      if(c=='\"') inString=false;
      continue;
    }
    if(c=='\"'){ inString=true; continue; }
    if(c==openCh) depth++;
    else if(c==closeCh && --depth==0) return i;
  }
  return -1;
}

static bool jsonObjectScope(const String &s,const char* key,int &from,int &to,int searchFrom=0,int searchTo=-1){
  int p=jsonValuePos(s,key,searchFrom,searchTo);
  if(p<0 || p>=(int)s.length() || s[p]!='{') return false;
  int e=jsonMatchingClose(s,p,'{','}',searchTo);
  if(e<0) return false;
  from=p+1;
  to=e;
  return true;
}

static bool jsonFirstObjectInArrayScope(const String &s,const char* key,int &from,int &to,int searchFrom=0,int searchTo=-1){
  int p=jsonValuePos(s,key,searchFrom,searchTo);
  const int limit=(searchTo<0 || searchTo>(int)s.length()) ? (int)s.length() : searchTo;
  if(p<0 || p>=limit || s[p]!='[') return false;
  p++;
  while(p<limit && jsonIsWs(s[p])) p++;
  if(p>=limit || s[p]!='{') return false;
  int e=jsonMatchingClose(s,p,'{','}',limit);
  if(e<0) return false;
  from=p+1;
  to=e;
  return true;
}

static bool jsonNumber(const String &s,const char* key,double &out,int from=0,int to=-1){
  int p=jsonValuePos(s,key,from,to);
  const int limit=(to<0 || to>(int)s.length()) ? (int)s.length() : to;
  if(p<0 || p>=limit || s.startsWith("null",p)) return false;
  const char* base=s.c_str();
  char* e=nullptr;
  out=strtod(base+p,&e);
  return e && e>base+p && (int)(e-base)<=limit;
}

static bool jsonString(const String &s,const char* key,String &out,int from=0,int to=-1){
  int p=jsonValuePos(s,key,from,to);
  const int limit=(to<0 || to>(int)s.length()) ? (int)s.length() : to;
  if(p<0 || p>=limit || s[p]!='\"') return false;
  p++;
  out="";
  while(p<limit){
    char c=s[p++];
    if(c=='\"') return true;
    if(c!='\\'){ out+=c; continue; }
    if(p>=limit) return false;
    c=s[p++];
    if(c=='u'){
      if(p+4>limit) return false;
      uint32_t cp=0;
      for(int i=0;i<4;i++){
        char h=s[p++];
        uint8_t v=(h>='0'&&h<='9')?h-'0':(h>='a'&&h<='f')?h-'a'+10:(h>='A'&&h<='F')?h-'A'+10:255;
        if(v>15) return false;
        cp=(cp<<4)|v;
      }
      if(cp>=0xD800 && cp<=0xDBFF && p+6<=limit && s[p]=='\\' && s[p+1]=='u'){
        int q=p+2; uint32_t lo=0; bool ok=true;
        for(int i=0;i<4;i++){
          char h=s[q++];
          uint8_t v=(h>='0'&&h<='9')?h-'0':(h>='a'&&h<='f')?h-'a'+10:(h>='A'&&h<='F')?h-'A'+10:255;
          if(v>15){ ok=false; break; }
          lo=(lo<<4)|v;
        }
        if(ok && lo>=0xDC00 && lo<=0xDFFF){ cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00); p=q; }
      }
      if(cp<=0x7F) out+=(char)cp;
      else if(cp<=0x7FF){ out+=(char)(0xC0|(cp>>6)); out+=(char)(0x80|(cp&0x3F)); }
      else if(cp<=0xFFFF){ out+=(char)(0xE0|(cp>>12)); out+=(char)(0x80|((cp>>6)&0x3F)); out+=(char)(0x80|(cp&0x3F)); }
      else { out+=(char)(0xF0|(cp>>18)); out+=(char)(0x80|((cp>>12)&0x3F)); out+=(char)(0x80|((cp>>6)&0x3F)); out+=(char)(0x80|(cp&0x3F)); }
    }else{
      switch(c){
        case '\"': out+='\"'; break; case '\\': out+='\\'; break; case '/': out+='/'; break;
        case 'b': out+='\b'; break; case 'f': out+='\f'; break; case 'n': out+='\n'; break;
        case 'r': out+='\r'; break; case 't': out+='\t'; break; default: return false;
      }
    }
  }
  return false;
}

static bool jsonFirstStringInArray(const String &s,const char* key,String &out,int from=0,int to=-1){
  int p=jsonValuePos(s,key,from,to);
  const int limit=(to<0 || to>(int)s.length()) ? (int)s.length() : to;
  if(p<0 || p>=limit || s[p]!='[') return false;
  p++;
  while(p<limit && jsonIsWs(s[p])) p++;
  if(p>=limit || s.startsWith("null",p) || s[p]!='\"') return false;
  p++;
  int e=p;
  bool esc=false;
  for(;e<limit;e++){
    char c=s[e];
    if(esc){ esc=false; continue; }
    if(c=='\\'){ esc=true; continue; }
    if(c=='\"') break;
  }
  if(e>=limit) return false;
  out=s.substring(p,e);
  return true;
}

// Preserve array indexes: JSON null becomes NAN instead of being compacted out.
static int jsonNumberArray(const String &s,const char* key,double *out,int maxN,int from=0,int to=-1){
  int p=jsonValuePos(s,key,from,to);
  const int limit=(to<0 || to>(int)s.length()) ? (int)s.length() : to;
  if(p<0 || p>=limit || s[p]!='[') return 0;
  p++;
  int n=0;
  const char* base=s.c_str();
  while(p<limit && n<maxN){
    while(p<limit && (jsonIsWs(s[p]) || s[p]==',')) p++;
    if(p>=limit || s[p]==']') break;
    if(s.startsWith("null",p)){
      out[n++]=NAN;
      p+=4;
      continue;
    }
    char* e=nullptr;
    double v=strtod(base+p,&e);
    if(!e || e<=base+p || (int)(e-base)>limit) return 0;
    out[n++]=v;
    p=(int)(e-base);
  }
  return n;
}

// ------------------------------------------------- upstream: saved Wi-Fi slots
static bool readWiFiSlot(int idx, String& ssidOut, String& passOut) {
  if (idx < 0 || idx >= WIFI_SLOT_COUNT) return false;
  char ks[8], kp[8];
  snprintf(ks, sizeof(ks), "ssid%d", idx);
  snprintf(kp, sizeof(kp), "pass%d", idx);
  wifiPrefs.begin("srwifi", true);
  ssidOut = wifiPrefs.getString(ks, "");
  passOut = wifiPrefs.getString(kp, "");
  wifiPrefs.end();
  return ssidOut.length() > 0;
}
static void writeWiFiSlot(int idx, const String& ssid, const String& pass) {
  if (idx < 0 || idx >= WIFI_SLOT_COUNT) return;
  char ks[8], kp[8];
  snprintf(ks, sizeof(ks), "ssid%d", idx);
  snprintf(kp, sizeof(kp), "pass%d", idx);
  wifiPrefs.begin("srwifi", false);
  wifiPrefs.putString(ks, ssid);
  wifiPrefs.putString(kp, pass);
  wifiPrefs.putInt("lastwifi", idx);
  wifiPrefs.end();
}
static bool findSavedWiFi(const String& ssid, String& passOut, int& slotOut) {
  for (int i = 0; i < WIFI_SLOT_COUNT; i++) {
    String ss, pp;
    if (readWiFiSlot(i, ss, pp) && ss == ssid) { passOut = pp; slotOut = i; return true; }
  }
  return false;
}
static void saveWiFi(const String& ssid, const String& pass) {
  String pp; int slot = -1;
  if (!findSavedWiFi(ssid, pp, slot)) {
    for (int i = 0; i < WIFI_SLOT_COUNT && slot < 0; i++) { String ss, p2; if (!readWiFiSlot(i, ss, p2)) slot = i; }
    if (slot < 0) { wifiPrefs.begin("srwifi", true); slot = (wifiPrefs.getInt("lastwifi", 0) + 1) % WIFI_SLOT_COUNT; wifiPrefs.end(); }
  }
  writeWiFiSlot(slot, ssid, pass);
}
static String urlEncode(const String& src) {
  const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < src.length(); i++) {
    uint8_t c = (uint8_t)src[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}
static String htmlEscape(const String& src) {
  String out;
  for (size_t i = 0; i < src.length(); i++) {
    char c = src[i];
    if (c == '&') out += "&amp;"; else if (c == '<') out += "&lt;"; else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;"; else if (c == '\'') out += "&#39;"; else out += c;
  }
  return out;
}

// --------------------------------------------- the net task: slow things, apart
// Weather comes from Open-Meteo over plain HTTP. The position sent is rounded
// to two decimals (about a kilometre); nothing else leaves the device.
static bool fetchWeather(double lat, double lon) {
  HTTPClient http;
  String url = String("http://api.open-meteo.com/v1/forecast?latitude=") + String(lat, 2) + "&longitude=" + String(lon, 2)
    + "&current=weather_code,cloud_cover,temperature_2m,relative_humidity_2m,pressure_msl,wind_speed_10m,wind_direction_10m,visibility"
    + "&hourly=pressure_msl&past_hours=3&forecast_hours=1&timezone=UTC";
  http.setConnectTimeout(8000); http.setTimeout(8000);
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  String body = http.getString();
  http.end();
  int cp = 0, ce = 0;
  if (!jsonObjectScope(body, "current", cp, ce)) return false;
  double v = 0;
  if (!jsonNumber(body, "weather_code", v, cp, ce)) return false;
  netRes.wmo = (int)v;
  if (jsonNumber(body, "cloud_cover", v, cp, ce)) netRes.cloud = constrain((float)v / 100.0f, 0.0f, 1.0f);
  if (jsonNumber(body, "temperature_2m", v, cp, ce)) netRes.tempC = (float)v;
  if (jsonNumber(body, "relative_humidity_2m", v, cp, ce)) netRes.humid = (float)v;
  if (jsonNumber(body, "pressure_msl", v, cp, ce)) netRes.press = (float)v;
  if (jsonNumber(body, "wind_speed_10m", v, cp, ce)) netRes.windKmh = (float)v;
  if (jsonNumber(body, "wind_direction_10m", v, cp, ce)) netRes.windDir = (float)v;
  if (jsonNumber(body, "visibility", v, cp, ce)) netRes.visKm = (float)v / 1000.0f;
  double pr[6]; int hp = 0, he = 0;
  netRes.trend = 0;
  if (jsonObjectScope(body, "hourly", hp, he)) {
    int n = jsonNumberArray(body, "pressure_msl", pr, 6, hp, he);
    if (n >= 2 && isfinite(pr[0]) && isfinite(pr[n - 1])) netRes.trend = (float)(pr[n - 1] - pr[0]);
  }
  return true;
}

static bool fetchCity(const char* city, double& lat, double& lon) {
  HTTPClient http;
  String url = "http://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(String(city)) + "&count=1&language=en&format=json";
  http.setConnectTimeout(8000); http.setTimeout(8000);
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  String body = http.getString();
  http.end();
  int rp = 0, re = 0;
  if (!jsonFirstObjectInArrayScope(body, "results", rp, re)) return false;
  return jsonNumber(body, "latitude", lat, rp, re) && jsonNumber(body, "longitude", lon, rp, re);
}

static void netTask(void*) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (netJob.wantGeo) {
        double la = 0, lo = 0;
        netRes.geoOk = fetchCity(netJob.city, la, lo);
        netRes.geoLat = la; netRes.geoLon = lo;
        netJob.wantGeo = false; netRes.geoDone = true;
      }
      if (netJob.wantWx) {
        if (fetchWeather(netJob.lat, netJob.lon)) netRes.wxFresh = true;
        netJob.wantWx = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

// ------------------------------------------------------------- the wifi door
static void portalClose() {
  if (!portalOpen) return;
  portalOpen = false;
  portalServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

static void portalStart() {
  static bool handlers = false;
  portalOpen = true; portalStatus = "join it from a phone";
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  WiFi.softAP(SETUP_AP_SSID);
  if (!handlers) {
    portalServer.on("/", HTTP_GET, []() {
      int n = WiFi.scanNetworks(false, true, false, 300);
      String h = "<meta name='viewport' content='width=device-width'><title>missingno.</title>"
                 "<body style='background:#0d0d0d;color:#e5e5e5;font-family:monospace'>"
                 "<h2>missingno.</h2><p>give it a way out to the weather and the clock.</p>"
                 "<form method='POST' action='/save'>network<br><select name='s'>";
      for (int i = 0; i < n; i++) {
        String ss = WiFi.SSID(i);
        if (!ss.length()) continue;
        String pp; int slot = -1;
        bool saved = findSavedWiFi(ss, pp, slot);
        h += "<option value=\"" + htmlEscape(ss) + "\">" + htmlEscape(ss) + (saved ? " (kept)" : "") + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
      }
      h += "</select><br>password<br><input name='p' type='password' maxlength='63' placeholder='blank keeps a saved one'><br>"
           "nearest town (only needed without gps)<br><input name='city' maxlength='80'><br><br>"
           "<button type='submit'>open</button></form>"
           "<form method='POST' action='/forget'><button>forget every network</button></form></body>";
      WiFi.scanDelete();
      portalServer.sendHeader("Cache-Control", "no-store");
      portalServer.send(200, "text/html", h);
    });
    portalServer.on("/save", HTTP_POST, []() {
      String s = portalServer.arg("s"), p = portalServer.arg("p"), c = portalServer.arg("city");
      s.trim(); c.trim();
      String kept; int slot = -1;
      if (!p.length() && findSavedWiFi(s, kept, slot)) p = kept;
      if (!s.length()) { portalServer.send(400, "text/plain", "a network is required"); return; }
      portalServer.send(200, "text/html", "<meta name='viewport' content='width=device-width'><body style='background:#0d0d0d;color:#e5e5e5;font-family:monospace'><h3>trying.</h3><p>watch the device, not this page.</p></body>");
      pendSsid = s; pendPass = p; pendCity = c; pendStage = 1; pendMs = millis();
    });
    portalServer.on("/forget", HTTP_POST, []() {
      wifiPrefs.begin("srwifi", false); wifiPrefs.clear(); wifiPrefs.end();
      WiFi.disconnect(false, true);
      portalServer.send(200, "text/html", "<meta http-equiv='refresh' content='1;url=/'><body style='background:#0d0d0d;color:#e5e5e5;font-family:monospace'>forgotten.</body>");
    });
    handlers = true;
  }
  portalServer.begin();
}

// ------------------------------------------------- over-the-air: being changed
static void pushFrame();
static void busyScreen(unsigned pct, const char* what) {
  mn::clear(mn::C_VOID);
  mn::textCenter(mn::W / 2, 52, what, mn::C_BONE);
  mn::rect(30, 70, mn::W - 60, 2, mn::C_SOOT);
  mn::rect(30, 70, (int)((mn::W - 60) * (pct > 100 ? 100 : pct) / 100), 2, mn::C_PHOS);
  pushFrame();
}

// The second "format": erase the card it could not read, make it FAT32, and move in.
static void cardFormat() {
  if (cardState != 2) return;
  hushed = true;
  M5Cardputer.Display.setBrightness(118);
  busyScreen(0, "making the card mine.");
  journal("erasing the card.");
  bool ok = SD.begin(PIN_SD_CS, SPI, 20000000, "/sd", 5, true);  // formats only a card with no filesystem
  hushed = false;
  if (!ok) { SD.end(); mn::mindSay("the card would not take it.", 6.0f, 3, false); return; }
  if (storeFs == &LittleFS) LittleFS.end();
  storeFs = &SD; storeName = "card"; cardState = 1; cardGB = SD.cardSize() / 1.0e9f;
  if (!SD.exists("/missingno")) SD.mkdir("/missingno");
  storeSaveLife();                                           // what it kept in flash carries on here
  journal("made the card mine.");
  Serial.printf("card: formatted, %.1f GB\n", cardGB);
  mn::mindSay("the card is mine now. i will keep what happens here.", 7.0f, 3, false);
}

static void otaBegin() {
#ifdef MN_OTA_HASH
  if (otaReady) return;
  ArduinoOTA.setHostname("missingno");                       // also missingno.local, for the IDE
  ArduinoOTA.setPasswordHash(MN_OTA_HASH);
  ArduinoOTA.onStart([]() {
    hushed = true;
    storeSaveLife();                                         // keep everything before the reboot
    journal("being changed.");
    M5Cardputer.Display.setBrightness(118);
    busyScreen(0, "being changed.");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static unsigned last = 999;
    unsigned pct = total ? (unsigned)((uint64_t)done * 100 / total) : 0;
    if (pct / 4 != last / 4 || last == 999) { last = pct; busyScreen(pct, "being changed."); }
  });
  ArduinoOTA.onEnd([]() { busyScreen(100, "again."); });
  ArduinoOTA.onError([](ota_error_t e) {
    hushed = false;
    mn::mindSay(e == OTA_AUTH_ERROR ? "someone tried to change me without the word." : "the change did not take.",
                6.0f, 3, false);
  });
  ArduinoOTA.begin();
  otaReady = true;
  Serial.println("ota: listening as missingno.local");
#endif
}

static void wifiOnline() {
  wifiStage = WS_ONLINE;
  if (!sntpStarted) { configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com"); sntpStarted = true; }
  lastWxMs = 0;
  otaBegin();
}

// The setup page asked for a network: try it without stalling the frame.
static void servicePending() {
  if (!pendStage) return;
  uint32_t now = millis();
  if (pendStage == 1 && now - pendMs > 600) {
    portalStatus = "trying " + pendSsid;
    WiFi.disconnect(false, false);
    WiFi.begin(pendSsid.c_str(), pendPass.c_str());
    pendStage = 2; pendMs = now;
  } else if (pendStage == 2) {
    if (WiFi.status() == WL_CONNECTED) {
      saveWiFi(pendSsid, pendPass);
      wifiOnline();
      if (pendCity.length()) {
        strncpy(netJob.city, pendCity.c_str(), sizeof(netJob.city) - 1); netJob.city[sizeof(netJob.city) - 1] = 0;
        netRes.geoDone = false; netJob.wantGeo = true;
        portalStatus = "finding " + pendCity;
        pendStage = 3; pendMs = now;
      } else { pendStage = 0; portalClose(); mn::mindSay("a way out is open.", 5.0f, 2, false); }
    } else if (now - pendMs > 20000) {
      portalStatus = "could not join. try again";
      WiFi.disconnect(false, false);
      wifiStage = WS_IDLE; pendStage = 0;
    }
  } else if (pendStage == 3) {
    if (netRes.geoDone || now - pendMs > 20000) {
      if (netRes.geoDone && netRes.geoOk) {
        namedLat = netRes.geoLat; namedLon = netRes.geoLon; haveNamed = true;
        prefs.putDouble("loclat", namedLat); prefs.putDouble("loclon", namedLon); prefs.putBool("haveloc", true);
        mn::mindSay("a way out is open. i know the town.", 6.0f, 2, false);
      } else mn::mindSay("a way out is open. the town is unknown to me.", 6.0f, 2, false);
      pendStage = 0; portalClose();
    }
  }
}

// --------------------------------------------------- the ether: a radio census
static void serviceEther(mn::Senses& in) {
  uint32_t now = millis();
  if (portalOpen || pendStage) return;
  if (wifiStage == WS_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) wifiOnline();
    else if ((int32_t)(now - wifiDeadline) > 0) { WiFi.disconnect(false, false); wifiStage = WS_IDLE; }
    return;
  }
  if (wifiStage == WS_ONLINE && WiFi.status() != WL_CONNECTED) wifiStage = WS_IDLE;

  if (!censusRunning) {
    bool due = lastCensusMs == 0 ? now > 8000 : now - lastCensusMs > 150000;
    if (due && !mn::earSoundingBusy()) {
      if (WiFi.scanNetworks(true, true, false, 110) == WIFI_SCAN_RUNNING) censusRunning = true;
      lastCensusMs = now;
    }
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  censusRunning = false;
  if (n < 0) return;
  // Keep only hashes of the strongest voices: enough to know a place again, not enough to name it.
  uint32_t hashes[8]; int rssi[8]; int cnt = 0, best = -100;
  String joinSsid, joinPass;
  for (int i = 0; i < n; i++) {
    int r = WiFi.RSSI(i);
    if (r > best) best = r;
    const uint8_t* bs = WiFi.BSSID(i);
    if (bs && r > -88) {
      uint32_t h = mn::hash32(((uint32_t)bs[0] << 24 | (uint32_t)bs[1] << 16 | (uint32_t)bs[2] << 8 | bs[3]) ^ mn::hash32((uint32_t)bs[4] << 8 | bs[5]));
      int at = cnt < 8 ? cnt++ : -1;
      if (at < 0) { int weakest = 0; for (int k = 1; k < 8; k++) if (rssi[k] < rssi[weakest]) weakest = k; if (r > rssi[weakest]) at = weakest; }
      if (at >= 0) { hashes[at] = h; rssi[at] = r; }
    }
    if (wifiStage == WS_IDLE && !joinSsid.length()) {
      String pp; int slot = -1;
      String ss = WiFi.SSID(i);
      if (ss.length() && findSavedWiFi(ss, pp, slot)) { joinSsid = ss; joinPass = pp; }
    }
  }
  WiFi.scanDelete();
  // Kin find each other on channel 1 unless a network has claimed the radio.
  if (WiFi.status() != WL_CONNECTED && !portalOpen) esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  float nov = 0; int vis = 0;
  if (cnt > 0) mn::etherMeet(hashes, cnt, nov, vis);
  in.apCount = n; in.apRssi = best; in.etherNovelty = cnt > 0 ? nov : 0; in.etherVisits = vis; in.etherFresh = true;
  if (joinSsid.length()) {                                   // a door it was given before
    WiFi.begin(joinSsid.c_str(), joinPass.c_str());
    wifiStage = WS_CONNECTING; wifiDeadline = now + 15000;
  }
}

// ---------------------------------------------------------- kin: the link cable
static void onKin(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != (int)sizeof(KinPacket) || kinInbox) return;
  KinPacket p; memcpy(&p, data, sizeof(p));
  if (p.magic != KIN_MAGIC) return;
  p.scrap[sizeof(p.scrap) - 1] = 0;
  kinIn = p; kinInRssi = info && info->rx_ctrl ? info->rx_ctrl->rssi : -100;
  kinInbox = true;
}

static void serviceKin(mn::Senses& in) {
  static uint32_t lastSend = 0, lastScrapPick = 0, lastSaid = 0; static KinPacket me; static uint32_t myId = 0;
  static const uint8_t ALL[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (!espNowOk) return;
  uint32_t now = millis();
  if (!myId) { uint8_t mac[6]; WiFi.macAddress(mac); myId = mn::hash32(((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5]) | 1; }
  if (kinInbox) {
    KinPacket p = kinIn; int r = kinInRssi; kinInbox = false;
    if (p.id != myId && r > -72) {                           // only when held close
      int slot = -1;
      for (int i = 0; i < 4; i++) if (kin[i].id == p.id) slot = i;
      if (slot < 0) for (int i = 0; i < 4 && slot < 0; i++) if (!kin[i].id || now - kin[i].ms > 60000) { slot = i; kin[i].id = p.id; kin[i].scrap[0] = 0; }
      if (slot >= 0) {
        kin[slot].ms = now; kin[slot].rootHz = p.rootHz;
        if (p.scrap[0] && strncmp(kin[slot].scrap, p.scrap, sizeof(p.scrap))) { strncpy(kin[slot].scrap, p.scrap, sizeof(kin[slot].scrap) - 1); kin[slot].scrapNew = true; }
      }
    }
  }
  int peers = 0; float root = 0;
  for (int i = 0; i < 4; i++) if (kin[i].id && now - kin[i].ms < 9000) {
    peers++; if (!root || kin[i].rootHz < root) root = kin[i].rootHz;
    if (kin[i].scrapNew && (lastSaid == 0 || now - lastSaid > 45000)) { kin[i].scrapNew = false; lastSaid = now; mn::mindKinScrap(kin[i].scrap); }
  }
  in.peers = peers; in.peerRootHz = root;
  if (now - lastSend > 2500 && !portalOpen) {
    lastSend = now;
    if (lastScrapPick == 0 || now - lastScrapPick > 60000) {
      lastScrapPick = now;
      const char* s = mn::mindScrapToGive();
      memset(me.scrap, 0, sizeof(me.scrap)); strncpy(me.scrap, s, sizeof(me.scrap) - 1);
      me.seq++;
    }
    me.magic = KIN_MAGIC; me.id = myId; me.rootHz = mn::S.sound.rootHz;
    me.unease = (uint8_t)(mn::S.unease * 255); me.arousal = (uint8_t)(mn::S.arousal * 255);
    esp_now_send(ALL, (const uint8_t*)&me, sizeof(me));
  }
}

static void initKin() {
  static const uint8_t ALL[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(onKin);
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, ALL, 6);
  peer.channel = 0; peer.encrypt = false; peer.ifidx = WIFI_IF_STA;
  espNowOk = esp_now_add_peer(&peer) == ESP_OK;
}

// ------------------------------------------------------------------ the sky
static void gpsOpen(int combo) {
  int rx = GPS_RX_ALL[(combo / 2) % 4]; uint32_t baud = GPS_BAUDS[combo % 2];
  GpsSerial.end();
  GpsSerial.setRxBufferSize(1024);
  GpsSerial.begin(baud, SERIAL_8N1, rx, -1);
  gpsTryMs = millis(); gpsGoodAtTry = nmea.g.good;
}

// A GPS may be on the cap connector or on the Grove port, at either common
// speed. Listen to each in turn until something speaks NMEA.
static void serviceGps(mn::Senses& in) {
  uint32_t now = millis();
  int budget = 256;
  while (GpsSerial.available() && budget-- > 0) {
    if (nmea.feed((char)GpsSerial.read())) {
      gpsLastSentenceMs = now;
      if (nmea.g.fix) gpsLastFixMs = now;
    }
  }
  if (!gpsLocked) {
    if (nmea.g.good - gpsGoodAtTry >= 3) {
      gpsLocked = true;
      prefs.putInt("gpscombo", gpsCombo);
      Serial.printf("gps: found on G%d at %lu\n", GPS_RX_ALL[(gpsCombo / 2) % 4], (unsigned long)GPS_BAUDS[gpsCombo % 2]);
    } else if (now - gpsTryMs > 2200) {
      gpsCombo = (gpsCombo + 1) % gpsCombos;
      gpsOpen(gpsCombo);
    }
  } else if (now - gpsLastSentenceMs > 30000) { gpsLocked = false; gpsOpen(gpsCombo); }

  bool fix = gpsLocked && nmea.g.fix && gpsLastFixMs && now - gpsLastFixMs < 10000;
  in.gpsFix = fix;
  if (fix) {
    in.haveFix = true; in.lat = nmea.g.lat; in.lon = nmea.g.lon;
    in.altM = nmea.g.altM; in.speedKmh = nmea.g.speedKmh; in.sats = nmea.g.sats;
  } else if (haveNamed) {
    in.haveFix = true; in.lat = namedLat; in.lon = namedLon; in.speedKmh = 0; in.sats = 0;
  } else in.haveFix = false;

  // With no network clock, the satellites keep the time.
  if (gpsLocked && nmea.g.haveTime && (gpsClockSetMs == 0 || now - gpsClockSetMs > 3600000UL)) {
    time_t sys = time(nullptr);
    if (sys < 1735689600 || fabs((double)sys - nmea.g.utc) > 5.0) {
      timeval tv; tv.tv_sec = (time_t)nmea.g.utc; tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
    }
    gpsClockSetMs = now;
  }
}

// ----------------------------------------------- the loose wires (experimental)
static bool extioRead(uint8_t reg, uint8_t* dst, int len) {
  Wire.beginTransmission(EXTIO_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)EXTIO_ADDR, len) != len) return false;
  for (int i = 0; i < len; i++) dst[i] = Wire.read();
  return true;
}

static void initGrove() {
  // If anything answers on the Grove port as I2C, leave those two pins alone for GPS.
  Wire.begin(PIN_GROVE_SDA, PIN_GROVE_SCL, 100000);
  Wire.setTimeOut(20);
  for (uint8_t addr = 0x08; addr < 0x78 && !groveIsI2C; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) groveIsI2C = true;
  }
  if (!groveIsI2C) { Wire.end(); return; }
  uint8_t ver = 0;
  if (extioRead(0xFE, &ver, 1) && ver >= 1 && ver <= 32) {
    // Extend I/O: put the first four pins in analog-input mode and use them as antennae.
    bool ok = true;
    for (uint8_t ch = 0; ch < 4 && ok; ch++) {
      Wire.beginTransmission(EXTIO_ADDR); Wire.write(ch); Wire.write(2);
      ok = Wire.endTransmission() == 0;
    }
    extioReady = ok;
  }
}

static void serviceAntenna(mn::Senses& in) {
  static uint32_t lastMs = 0; static uint8_t prev[4]; static bool primed = false; static float agit = 0;
  if (!extioReady) return;
  uint32_t now = millis();
  if (now - lastMs < 100) return;
  lastMs = now;
  uint8_t v[4];
  if (!extioRead(0x30, v, 4)) return;                        // 8-bit analog readings
  if (primed) {
    float d = 0;
    for (int i = 0; i < 4; i++) d += abs((int)v[i] - (int)prev[i]);
    agit += (d / 4.0f - agit) * 0.2f;                        // floating pins wander; a hand or a field stirs them
    float a = agit / 30.0f;
    in.antenna = a > 1 ? 1 : a;
  }
  memcpy(prev, v, 4); primed = true;
}

// ------------------------------------------------------------- light and power
static void serviceLight() {
  static uint8_t lr = 1, lg = 1, lb = 1; static bool lir = false; static int lbright = -1; static uint32_t lastMs = 0;
  uint32_t now = millis();
  // A 38 kHz carrier at a quarter duty: the LED never sits fully on, and a phone camera still sees it.
  if (mn::S.ir != lir) { lir = mn::S.ir; ledcWrite(PIN_IR, lir ? 64 : 0); }
  if (now - lastMs < 50) return;
  lastMs = now;
  if (mn::S.ledR != lr || mn::S.ledG != lg || mn::S.ledB != lb) {
    lr = mn::S.ledR; lg = mn::S.ledG; lb = mn::S.ledB;
    rgbLedWrite(PIN_RGB, lr, lg, lb);
  }
  int want = mn::S.in.faceDown ? 0 : (int)(118.0f - 100.0f * mn::S.sleep);
  if (portalOpen) want = 118;
  if (lbright < 0) lbright = want;
  if (want != lbright) {
    lbright += want > lbright ? (want - lbright > 12 ? 12 : want - lbright) : (lbright - want > 4 ? -4 : want - lbright);
    M5Cardputer.Display.setBrightness((uint8_t)lbright);
  }
}

static void serviceBattery(mn::Senses& in) {
  static uint32_t lastMs = 0; static float v = 0;
  uint32_t now = millis();
  if (lastMs && now - lastMs < 5000) return;
  lastMs = now;
  float r = analogReadMilliVolts(PIN_BATTERY) * 2.0f / 1000.0f;
  v = v == 0 ? r : v + (r - v) * 0.3f;
  in.battV = v;
}

static void serviceWeather(mn::Senses& in) {
  uint32_t now = millis();
  if (netRes.wxFresh) {
    netRes.wxFresh = false;
    in.haveWx = true; in.wmo = netRes.wmo; in.cloud = netRes.cloud; in.tempC = netRes.tempC; in.humid = netRes.humid;
    in.pressHpa = netRes.press; in.pressTrend = netRes.trend; in.windKmh = netRes.windKmh; in.windDir = netRes.windDir; in.visKm = netRes.visKm;
  }
  if (wifiStage != WS_ONLINE || !in.haveFix || netJob.wantWx) return;
  double dLat = in.lat - wxLat, dLon = (in.lon - wxLon) * cos(in.lat * 0.0174533);
  bool moved = sqrt(dLat * dLat + dLon * dLon) > 0.09;        // about ten kilometres
  if (lastWxMs == 0 || now - lastWxMs > 30UL * 60UL * 1000UL || (moved && now - lastWxMs > 60000UL)) {
    lastWxMs = now; wxLat = in.lat; wxLon = in.lon;
    netJob.lat = in.lat; netJob.lon = in.lon; netJob.wantWx = true;
  }
}

static void pushFrame() {
  M5Cardputer.Display.pushImage(0, 0, mn::W, mn::H, mn::fb);
}

// ---------------------------------------------------------------------- setup
void setup() {
  Serial.begin(115200);
  WiFi.persistent(false);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  ledcAttach(PIN_IR, 38000, 8); ledcWrite(PIN_IR, 0);

  // Only the LCD is initialised through M5GFX; M5Unified never owns I2S0.
  M5Cardputer.Display.begin();
  // Landscape with the keyboard below is the panel's internal rotation 1. Older M5GFX gave the
  // Cardputer no rotation offset, so setRotation(1) meant that; M5GFX 0.2.23 gives it an offset
  // of 1, where setRotation(1) turns the face sideways. Ask the panel and land on 1 either way.
  {
    auto* panel = M5Cardputer.Display.panel();
    uint8_t off = panel ? (panel->config().offset_rotation & 3) : 0;
    M5Cardputer.Display.setRotation((uint8_t)((1 - off) & 3));
  }
  M5Cardputer.Display.setBrightness(118);
  M5Cardputer.Display.setSwapBytes(true);
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  mn::fb = (uint16_t*)heap_caps_malloc(mn::W * mn::H * sizeof(uint16_t), MALLOC_CAP_8BIT);
  if (!mn::fb) {
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5Cardputer.Display.drawString("no room for a face", 4, 4);
    while (true) delay(1000);
  }
  mn::clear(mn::C_VOID); pushFrame();

  imuReady = initImu();
  micReady = initAudio();
  keysReady = initKeys();

  prefs.begin("missingno", false);
  haveNamed = prefs.getBool("haveloc", false);
  namedLat = prefs.getDouble("loclat", 0); namedLon = prefs.getDouble("loclon", 0);

  mn::lifeReset();
  storeBegin();
  bool remembered = storeLoadLife();
  horizonBegin();

  uint32_t seed = esp_random();
  mn::earInit();
  mn::mindInit(seed);
  mn::synthInit(seed ^ 0x9E3779B9u);
  if (micReady && spkChan) xTaskCreatePinnedToCore(audioTask, "voice", 4096, nullptr, 5, nullptr, 0);

  WiFi.setHostname("missingno");                              // how the router lists it; must precede mode()
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  initKin();
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);

  initGrove();
  gpsCombos = groveIsI2C ? 4 : 8;                            // Grove pins only if nothing I2C lives there
  gpsCombo = prefs.getInt("gpscombo", 0) % gpsCombos;
  gpsOpen(gpsCombo);

  Serial.printf("missingno. imu %d mic %d keys %d store %s remembered %d grove-i2c %d ext.io %d wakes %lu\n",
                imuReady, micReady, keysReady, storeName, remembered, groveIsI2C, extioReady, (unsigned long)mn::life().wakes);
  Serial.printf("horizon: %s, %lu points\n", hzFrom, (unsigned long)mn::HZ.points);
  Serial.printf("card: %s, %.1f GB\n", cardState == 1 ? "in use" : cardState == 2 ? "present, unreadable" : "none", cardGB);
  Serial.printf("screen: board %d, %d x %d, rotation %d\n", (int)M5Cardputer.Display.getBoard(),
                (int)M5Cardputer.Display.width(), (int)M5Cardputer.Display.height(),
                (int)M5Cardputer.Display.getRotation());
  journal(remembered ? "woke." : "woke, remembering nothing.");

  // The first seconds: it assembles itself.
  uint32_t t0 = millis();
  while (millis() - t0 < 3400) {
    mn::drawBoot((millis() - t0) / 1000.0f);
    pushFrame();
    delay(16);
  }
  // Say where the memory lives: the card is only looked for at wake.
  if (!storeFs) mn::mindSay("no card, no memory. i will not keep any of this.", 7.0f, 3, false);
  else if (strcmp(storeName, "card") == 0) mn::mindSay("a card. i will keep what happens here.", 6.0f, 3, false);
  else if (cardState == 2) mn::mindSay("a card i cannot read. format, twice, makes it mine.", 7.0f, 3, false);
  else mn::mindSay("no card. i keep what i can inside myself.", 6.0f, 3, false);
}

// ----------------------------------------------------------------------- loop
void loop() {
  static uint32_t lastUs = 0, lastSaveMs = 0, lastPeriodicMs = 0;
  static uint32_t seenFound = 0xFFFFFFFFu, seenLines = 0, seenReson = 0, seenSound = 0;
  uint32_t nowUs = micros();
  float dt = lastUs ? (nowUs - lastUs) / 1.0e6f : 0.033f;
  lastUs = nowUs;
  mn::Senses& in = mn::S.in;
  in.dt = dt;

  pollKeys();
  pollButton();
  readImu(in);
  serviceGps(in);
  serviceEther(in);
  servicePending();
  serviceKin(in);
  serviceWeather(in);
  serviceAntenna(in);
  serviceBattery(in);
  if (portalOpen) portalServer.handleClient();
  in.card = cardState; in.cardGB = cardGB;
  if (mn::S.wantFormat) { mn::S.wantFormat = false; cardFormat(); }
  if (otaReady) ArduinoOTA.handle();                         // blocks here while new firmware arrives

  timeval tv; gettimeofday(&tv, nullptr);
  in.haveTime = tv.tv_sec > 1735689600;
  in.utc = (double)tv.tv_sec + tv.tv_usec / 1.0e6;

  float partials[10];
  int np = mn::synthPartials(partials, 10);
  if (micReady) mn::earAnalyze(in, dt, partials, np, mn::S.sound.rootHz);
  if (mn::S.wantSounding) {
    mn::S.wantSounding = false;
    if (micReady) mn::earStartSounding(); else mn::mindSay("i have no ear here.", 4.0f, 2, false);
  }
  mn::Sounding snd;
  if (mn::earTakeSounding(snd)) mn::mindSounding(snd);

  mn::mindTick();

  if (mn::S.wantPortal) { mn::S.wantPortal = false; if (!portalOpen) portalStart(); }
  uint32_t nowMs = millis();
  if (mn::S.wantForget) {
    mn::S.wantForget = false;
    if (storeFs) { storeFs->remove("/missingno/life.bin"); storeFs->remove("/missingno/life.tmp"); }
    journal("let go of everything.");
    seenFound = 0xFFFFFFFFu; lastSaveMs = 0; mn::S.wantSave = true;
  }
  // the diary
  mn::Life& L = mn::life();
  if (seenFound == 0xFFFFFFFFu) { seenFound = L.foundCount; seenLines = L.linesCrossed; seenReson = L.resonances; seenSound = L.soundings; }
  if (L.foundCount > seenFound) { seenFound = L.foundCount; char b[110]; snprintf(b, sizeof(b), "opened: %s", mn::S.say); journal(b); }
  if (L.linesCrossed > seenLines) { seenLines = L.linesCrossed; journal("crossed a line."); }
  if (L.resonances > seenReson) { seenReson = L.resonances; char b[48]; snprintf(b, sizeof(b), "resonance at %.1f hz.", mn::S.sound.rootHz); journal(b); }
  if (L.soundings > seenSound) { seenSound = L.soundings; char b[64]; snprintf(b, sizeof(b), "struck the room: %d ms, verdict %d.", (int)mn::S.sounding.tailMs, mn::S.sounding.verdict); journal(b); }

  bool periodic = nowMs - lastPeriodicMs > 600000UL;
  if ((mn::S.wantSave && (lastSaveMs == 0 || nowMs - lastSaveMs > 20000UL)) || periodic) {
    mn::S.wantSave = false; lastSaveMs = nowMs; lastPeriodicMs = nowMs;
    storeSaveLife();
  }

  if (portalOpen) {
    String ip = WiFi.softAPIP().toString();
    mn::drawPortal(SETUP_AP_SSID, ip.c_str(), portalStatus.c_str());
  } else mn::drawFrame();
  pushFrame();
  serviceLight();

  uint32_t spent = micros() - nowUs;
  if (spent < 30000) delay((30000 - spent) / 1000);
}
