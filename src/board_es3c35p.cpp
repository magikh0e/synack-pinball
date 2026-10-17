// LCDwiki ES3C35P (ESP32-S3, 3.5" 320x480): ST77922 panel over quad SPI with
// its own capacitive multi-touch at I2C 0x55, ES8311 codec + speaker amp,
// WS2812 LED, BOOT button.
//
// Sources, all from LCDwiki's ES3C35P documentation pack
// (lcdwiki.com/res/ES3C35P/):
//   ESP32-S3_IO资源分配表.xlsx   the IO allocation table - every pin used here
//   ESP32-S3原理图.pdf           the schematic
//   ST77922_SPEC_V1.2.pdf        panel controller (window rules: see main.cpp)
//   Example_16_music             ES8311 setup and the I2S pins
//   ST77922_TOUCH library        touch protocol (0x55, 16-bit registers)
//
// One conflict inside the pack: the IO table's description column swaps the
// touch reset and interrupt. The schematic wires CTP_RST to module pin 36 and
// CTP_INT to pin 37, which the same table's pin column maps to IO48 and IO47,
// and the vendor's touch library agrees: reset 48, interrupt 47.
//
// Flipper buttons go on the serial-port header (the vendor spec's "1.25mm
// 4P socket", silkscreened UART, P2 on the schematic). Per the silkscreen in
// the vendor's outline drawing: pin 1 (triangle) 5V, pin 2 GND, pin 3 TXD =
// IO43 (left), pin 4 RXD = IO44 (right), each through a 100R series resistor
// on the board. Never wire a button to pin 1: that's 5 V. This build's
// console is native USB, so UART0 is idle and the IO table allows those two
// as plain IO. Each button is a momentary switch from its pin to GND; internal pull-ups, so unwired
// pins read "not pressed". Touch keeps working alongside the buttons.
//   - The ROM prints its boot log on IO43, so don't hold the left button
//     while the board resets (the 100R limits the current if you do).
//   - IO45/46 on the expansion header are free too, but they're strapping
//     pins (IO45 picks the flash voltage at reset), so a pull-up there could
//     stop the board booting. They drive the flipper buttons' lamps instead:
//     the expansion socket (P3, silkscreened IO45 IO46 GND 3.3V) is pin 1
//     3.3V, pin 2 GND, pin 3 IO46 (right lamp), pin 4 IO45 (left lamp). Each pin switches its 5 V lamp through an NPN
//     transistor (1k into the base), so the pin only ever sees a base, which
//     leaves it low at reset as the straps need.

#include <Arduino.h>
#if defined(BOARD_ES3C35P)

#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include "board.hpp"
#include "driver/i2s.h"
#include "s3/St77922Qspi.h"
#include "s3/es8311.h"

// SCREEN_FLIP=1 (env es3c35p-cabinet): the board is mounted upside down, so
// its USB-C, RESET and BOOT face the back of a cabinet and the speaker and
// flipper-button sockets sit nearer their parts. The image and touch are
// turned 180 degrees here; nothing above the board layer knows.
#ifndef SCREEN_FLIP
#define SCREEN_FLIP 0
#endif

namespace {

// ---- touch: inside the ST77922, 16-bit registers, polled (INT never moves)
constexpr uint8_t TOUCH_ADDR = 0x55;
constexpr int TOUCH_SDA = 38, TOUCH_SCL = 39, TOUCH_RST = 48;  // INT is IO47, never read
constexpr uint16_t R_STATUS = 0x0001, R_MAX_TOUCHES = 0x0009, R_TOUCH_INFO = 0x0010,
                   R_POINT0 = 0x0014;
constexpr uint8_t TOUCH_INFO_READY = 0x08, POINT_VALID = 0x80;
constexpr uint32_t TOUCH_POLL_MS = 8, TOUCH_HOLD_MS = 60;

// ---- audio: ES8311 on the same I2C bus, I2S out, amplifier enable active LOW
constexpr int I2S_MCK = 17, I2S_BCK = 18, I2S_WS = 21, I2S_DOUT = 15, AMP_EN = 1;
constexpr int SAMPLE_RATE = 16000;
constexpr float AMP[4] = {0, 1100, 2200, 4400};  // per volume level (of 32767)
constexpr int BL_CH = 1;  // LEDC channel for the backlight PWM
constexpr int CODEC_VOLUME = 70;    // 0..100

constexpr int PIN_LED = 40;
constexpr int PIN_BTN_LEFT = 43;   // serial header pin 3 (TXD0)
constexpr int PIN_BTN_RIGHT = 44;  // serial header pin 4 (RXD0)
constexpr int PIN_BTN_LAUNCH = 0;  // BOOT
constexpr int PIN_LAMP_LEFT = 45, PIN_LAMP_RIGHT = 46;  // expansion socket pins 4 / 3
constexpr int LAMP_CH_LEFT = 2, LAMP_CH_RIGHT = 3;     // LEDC channels (backlight is 1)

uint8_t maxPoints = 0;
board::Touch held[board::MAX_TOUCH];
int heldN = 0;
uint32_t lastPoll = 0, lastFresh = 0;

// the note each synth voice is playing; gen bumps on every new note
struct NoteCmd {
  uint16_t hz, toHz, ms;
  uint8_t wave;
};
NoteCmd note[board::VOICES] = {};
volatile uint32_t noteGen[board::VOICES] = {};
portMUX_TYPE noteMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t volume = 2;
uint8_t brightness = 255;
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

// A small synth: VOICES voices, each square / 25% pulse / triangle / saw /
// noise with a linear pitch sweep, a 3 ms attack and an exponential decay (a
// struck sound, not an organ), mixed together so sounds overlap.
struct Voice {
  NoteCmd n = {0, 0, 0, 0};
  uint32_t gen = 0, elapsed = 0, len = 1;
  float phase = 0, env = 0, decay = 1, sweep = 0, freq = 0, noise = 0;
  uint16_t lfsr = 0xACE1;

  float sample() {
    constexpr uint32_t ATTACK = SAMPLE_RATE * 3 / 1000;
    if (!n.hz || elapsed >= len) return 0;
    env = elapsed < ATTACK ? (float)elapsed / ATTACK : env * decay;
    phase += freq / SAMPLE_RATE;
    if (phase >= 1) {
      phase -= 1;
      lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);  // new noise value per cycle
      noise = (lfsr & 1) ? 1.0f : -1.0f;
    }
    float s;
    switch (n.wave) {
      case board::W_PULSE: s = phase < 0.25f ? 1 : -1; break;
      case board::W_TRI:   s = 4 * fabsf(phase - 0.5f) - 1; break;
      case board::W_SAW:   s = 2 * phase - 1; break;
      case board::W_NOISE: s = noise; break;
      default:             s = phase < 0.5f ? 1 : -1; break;
    }
    freq += sweep;
    elapsed++;
    return s * env;
  }
};

void audioTask(void*) {
  int16_t buf[128 * 2];
  Voice v[board::VOICES];
  for (;;) {
    for (int k = 0; k < board::VOICES; k++)
      if (v[k].gen != noteGen[k]) {
        portENTER_CRITICAL(&noteMux);
        v[k].n = note[k];
        v[k].gen = noteGen[k];
        portEXIT_CRITICAL(&noteMux);
        const NoteCmd& n = v[k].n;
        v[k].elapsed = 0;
        v[k].len = n.ms ? (uint32_t)n.ms * SAMPLE_RATE / 1000 : SAMPLE_RATE;  // untimed: 1 s
        v[k].decay = expf(-1.0f / (v[k].len * 0.55f));
        v[k].freq = n.hz;
        v[k].sweep = n.toHz ? ((float)n.toHz - n.hz) / v[k].len : 0;
      }
    float amp = AMP[volume] * 0.7f;  // headroom for voices summing
    for (int i = 0; i < 128; i++) {
      float s = 0;
      for (int k = 0; k < board::VOICES; k++) s += v[k].sample();
      float o = s * amp;
      int16_t q = (int16_t)(o > 32000 ? 32000 : (o < -32000 ? -32000 : o));
      buf[2 * i] = buf[2 * i + 1] = q;
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
const uint8_t DEFAULT_VOLUME = 2;
const uint8_t BRIGHT_FULL = 255;

bool init() {
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);  // takes IO43/44 back from UART0
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BTN_LAUNCH, INPUT_PULLUP);
  pixel.begin();
  pixel.setBrightness(60);
  led(false, false, false);
  ledcSetup(LAMP_CH_LEFT, 5000, 8);
  ledcSetup(LAMP_CH_RIGHT, 5000, 8);
  ledcAttachPin(PIN_LAMP_LEFT, LAMP_CH_LEFT);
  ledcAttachPin(PIN_LAMP_RIGHT, LAMP_CH_RIGHT);
  buttonLamps(0, 0);

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
  // the shadow keeps the game's orientation, so screenshots stay upright
  if (shadow)
    for (int r = 0; r < h; r++) memcpy(shadow + (y + r) * 320 + x, px + r * w, w * 2);
#if SCREEN_FLIP
  // 180 degrees: a rectangle turned half a turn is its pixels in reverse
  // order, at the mirrored position. x and w stay multiples of ALIGN_X, so
  // 320 - x - w does too (the ST77922's window rule still holds).
  for (int i = 0, j = w * h - 1; i < j; i++, j--) {
    uint16_t t = px[i];
    px[i] = px[j];
    px[j] = t;
  }
  St77922::pushRect(320 - x - w, 480 - y - h, w, h, px);  // synchronous
#else
  St77922::pushRect(x, y, w, h, px);  // synchronous
#endif
}

void endFrame() {
  if (!lit) {  // first full frame is on the panel: now light it
    // backlight on PWM (vendor Example_14), lit once a whole frame is there
    ledcSetup(BL_CH, 5000, 8);
    ledcAttachPin(ST77922_BL, BL_CH);
    ledcWrite(BL_CH, brightness);
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
#if SCREEN_FLIP
      for (int i = 0; i < n; i++) held[i] = {(int16_t)(319 - held[i].x), (int16_t)(479 - held[i].y)};
#endif
      heldN = n;
      lastFresh = now;
    } else if (heldN && now - lastFresh >= TOUCH_HOLD_MS) {
      heldN = 0;  // the controller stops reporting once every finger is up
    }
  }
  memcpy(pts, held, sizeof(Touch) * heldN);
  return heldN;
}

bool btnLeft() { return digitalRead(PIN_BTN_LEFT) == LOW; }
bool btnRight() { return digitalRead(PIN_BTN_RIGHT) == LOW; }
bool btnLaunch() { return digitalRead(PIN_BTN_LAUNCH) == LOW; }

void tone(uint8_t voice, uint16_t hz, uint8_t wave, uint16_t toHz, uint16_t ms) {
  if (voice >= VOICES) return;
  portENTER_CRITICAL(&noteMux);
  note[voice] = {hz, toHz, ms, wave};
  noteGen[voice] = noteGen[voice] + 1;
  portEXIT_CRITICAL(&noteMux);
}
void setVolume(uint8_t level) { volume = level < VOLUME_LEVELS ? level : VOLUME_LEVELS - 1; }

void setBrightness(uint8_t v) {
  brightness = v;
  if (lit) ledcWrite(BL_CH, v);
}

void led(bool r, bool g, bool b) {
  pixel.setPixelColor(0, pixel.Color(r ? 255 : 0, g ? 255 : 0, b ? 255 : 0, 0));
  pixel.show();
}

void buttonLamps(uint8_t left, uint8_t right) {
  ledcWrite(LAMP_CH_LEFT, left);
  ledcWrite(LAMP_CH_RIGHT, right);
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
