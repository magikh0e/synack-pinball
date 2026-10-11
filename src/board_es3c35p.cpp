// LCDwiki ES3C35P (ESP32-S3, 3.5" 320x480): ST77922 panel over quad SPI with
// its own capacitive multi-touch at I2C 0x55, ES8311 codec + speaker amp,
// WS2812 LED, BOOT button. Pins are from the vendor pack's pin table and the
// pueo bring-up (see s3/St77922Qspi.h).
//
// The board brings out no free GPIO for flipper buttons (IO38/39 are the I2C
// bus, IO43/44 are UART0), so the flippers are touch-only: with real
// multi-touch, a thumb on each half of the screen holds each flipper.

#include <Arduino.h>
#if defined(BOARD_ES3C35P)

#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include "board.hpp"
#include "driver/i2s.h"
#include "s3/St77922Qspi.h"
#include "s3/es8311.h"

namespace {

// ---- touch: inside the ST77922, 16-bit registers, polled (INT never moves)
constexpr uint8_t TOUCH_ADDR = 0x55;
constexpr int TOUCH_SDA = 38, TOUCH_SCL = 39, TOUCH_RST = 48;
constexpr uint16_t R_STATUS = 0x0001, R_MAX_TOUCHES = 0x0009, R_TOUCH_INFO = 0x0010,
                   R_POINT0 = 0x0014;
constexpr uint8_t TOUCH_INFO_READY = 0x08, POINT_VALID = 0x80;
constexpr uint32_t TOUCH_POLL_MS = 8, TOUCH_HOLD_MS = 60;

// ---- audio: ES8311 on the same I2C bus, I2S out, amplifier enable active LOW
constexpr int I2S_MCK = 17, I2S_BCK = 18, I2S_WS = 21, I2S_DOUT = 15, AMP_EN = 1;
constexpr int SAMPLE_RATE = 16000;
constexpr int16_t TONE_AMP = 2200;  // square wave amplitude (of 32767)
constexpr int CODEC_VOLUME = 70;    // 0..100

constexpr int PIN_LED = 40;
constexpr int PIN_BTN_LAUNCH = 0;  // BOOT

uint8_t maxPoints = 0;
board::Touch held[board::MAX_TOUCH];
int heldN = 0;
uint32_t lastPoll = 0, lastFresh = 0;

volatile uint16_t toneHz = 0;
bool audioOk = false;

uint16_t* shadow = nullptr;  // PSRAM copy of the screen, for screenshots
bool lit = false;

Adafruit_NeoPixel pixel(1, PIN_LED, NEO_GRBW + NEO_KHZ800);  // GRBW as the vendor drives it

int rd(uint16_t reg, uint8_t* out, int n) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return 0;
  Wire.requestFrom(TOUCH_ADDR, (uint8_t)n);
  int d = 0;
  while (Wire.available() && d < n) out[d++] = Wire.read();
  return d;
}

bool touchBegin() {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(100);
  digitalWrite(TOUCH_RST, HIGH);
  delay(100);
  uint8_t st = 0xFF;
  for (int i = 0; i < 50; i++) {
    if (rd(R_STATUS, &st, 1) && !(st & 0x0F)) break;
    delay(20);
  }
  if (st & 0x0F) return false;
  if (!rd(R_MAX_TOUCHES, &maxPoints, 1) || maxPoints == 0 || maxPoints > 10) {
    maxPoints = 0;
    return false;
  }
  return true;
}

// every point the controller tracks; reading the whole report is what
// clears its ready flag (a partial read re-offers the same points forever)
int touchPoll(board::Touch* pts) {
  uint8_t info = 0;
  if (!rd(R_TOUCH_INFO, &info, 1) || !(info & TOUCH_INFO_READY)) return -1;
  uint8_t rep[7 * 10];
  int want = 7 * maxPoints;
  if (rd(R_POINT0, rep, want) != want) return -1;
  int n = 0;
  for (int i = 0; i < maxPoints && n < board::MAX_TOUCH; i++) {
    const uint8_t* p = rep + 7 * i;
    if (!(p[0] & POINT_VALID)) continue;
    pts[n++] = {(int16_t)(((p[0] & 0x3F) << 8) | p[1]), (int16_t)(((p[2] & 0x3F) << 8) | p[3])};
  }
  return n;
}

void audioTask(void*) {
  int16_t buf[128 * 2];
  uint32_t phase = 0;
  for (;;) {
    uint16_t hz = toneHz;
    uint32_t inc = (uint32_t)((uint64_t)hz * 4294967296ULL / SAMPLE_RATE);
    for (int i = 0; i < 128; i++) {
      int16_t s = 0;
      if (hz) {
        phase += inc;
        s = (phase & 0x80000000u) ? TONE_AMP : -TONE_AMP;
      }
      buf[2 * i] = buf[2 * i + 1] = s;
    }
    size_t wrote;
    i2s_write(I2S_NUM_0, buf, sizeof buf, &wrote, portMAX_DELAY);
  }
}

bool audioBegin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 128;
  cfg.tx_desc_auto_clear = true;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_MCK;
  pins.bck_io_num = I2S_BCK;
  pins.ws_io_num = I2S_WS;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;

  // the codec rides the I2C driver Wire already installed on port 0
  es8311_handle_t es = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  if (!es) return false;
  es8311_clock_config_t clk = {};
  clk.mclk_from_mclk_pin = true;
  clk.mclk_frequency = SAMPLE_RATE * 384;
  clk.sample_frequency = SAMPLE_RATE;
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_voice_volume_set(es, CODEC_VOLUME, nullptr);

  pinMode(AMP_EN, OUTPUT);
  digitalWrite(AMP_EN, LOW);  // amplifier on
  xTaskCreatePinnedToCore(audioTask, "tone", 3072, nullptr, 3, nullptr, 0);
  return true;
}

}  // namespace

namespace board {

const char* const NAME = "ES3C35P (ESP32-S3)";
const bool MULTITOUCH = true;
const int ALIGN_X = 4;  // ST77922 CASET: XS and width must be multiples of 4

bool init() {
  pinMode(PIN_BTN_LAUNCH, INPUT_PULLUP);
  pixel.begin();
  pixel.setBrightness(60);
  led(false, false, false);

  shadow = (uint16_t*)ps_malloc(320 * 480 * 2);
  if (!St77922::begin()) return false;

  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.setTimeOut(30);
  bool touchOk = touchBegin();
  audioOk = audioBegin();
  Serial.printf("[board] %s: touch %s (%u points), audio %s, shadow %s\n", NAME,
                touchOk ? "ok" : "FAILED", maxPoints, audioOk ? "ok" : "FAILED",
                shadow ? "ok" : "none");
  return true;
}

void beginFrame() {}

void pushRect(int x, int y, int w, int h, uint16_t* px) {
  St77922::pushRect(x, y, w, h, px);  // synchronous
  if (shadow)
    for (int r = 0; r < h; r++) memcpy(shadow + (y + r) * 320 + x, px + r * w, w * 2);
}

void endFrame() {
  if (!lit) {  // first full frame is on the panel: now light it
    St77922::backlight(true);
    lit = true;
  }
}

int readTouch(Touch* pts) {
  uint32_t now = millis();
  if (maxPoints && now - lastPoll >= TOUCH_POLL_MS) {
    lastPoll = now;
    Touch fresh[MAX_TOUCH];
    int n = touchPoll(fresh);
    if (n >= 0) {
      memcpy(held, fresh, sizeof(Touch) * n);
      heldN = n;
      lastFresh = now;
    } else if (heldN && now - lastFresh >= TOUCH_HOLD_MS) {
      heldN = 0;  // the controller stops reporting once every finger is up
    }
  }
  memcpy(pts, held, sizeof(Touch) * heldN);
  return heldN;
}

bool btnLeft() { return false; }
bool btnRight() { return false; }
bool btnLaunch() { return digitalRead(PIN_BTN_LAUNCH) == LOW; }

void tone(uint16_t hz) { toneHz = hz; }

void led(bool r, bool g, bool b) {
  pixel.setPixelColor(0, pixel.Color(r ? 255 : 0, g ? 255 : 0, b ? 255 : 0, 0));
  pixel.show();
}

void readRowRGB(int y, uint8_t* rgb) {
  for (int x = 0; x < 320; x++) {
    uint16_t v = shadow ? shadow[y * 320 + x] : 0;
    v = (uint16_t)((v >> 8) | (v << 8));  // stored byte-swapped
    rgb[3 * x] = ((v >> 11) & 0x1F) * 255 / 31;
    rgb[3 * x + 1] = ((v >> 5) & 0x3F) * 255 / 63;
    rgb[3 * x + 2] = (v & 0x1F) * 255 / 31;
  }
}

void serialFast(bool) {}  // native USB: no baud rate to change

}  // namespace board

#endif
