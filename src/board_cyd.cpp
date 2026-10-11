// Sunton ESP32-3248S035R (CYD 3.5"): ST7796 + XPT2046 on HSPI, piezo-style
// speaker on GPIO 26, common-anode RGB LED, buttons on the P3 header.

#include <Arduino.h>
#if defined(CONFIG_IDF_TARGET_ESP32) && !defined(BOARD_ES3C35P)

#include "LGFX_CYD35.hpp"
#include "board.hpp"

namespace {

constexpr int PIN_RGB_R = 4;
constexpr int PIN_RGB_G = 17;  // G/B are swapped on this board (measured)
constexpr int PIN_RGB_B = 16;
constexpr int PIN_SPK = 26;
constexpr int SPK_CH = 0;

// physical buttons, each a momentary switch from the pin to GND (internal
// pull-ups, so unwired pins just read "not pressed"). -1 = none.
//   flippers: IO22 / IO21 on the "Extended IO" header, silkscreened P3:
//             GND IO35 IO22 IO21. Sunton's document gives its connectors as
//             4-pin 1.25 mm; the plug that fits is JST GH (PicoBlade doesn't).
//   launch:   the board's BOOT button, GPIO 0. (Don't hold it while the board
//             resets: it enters flash mode.)
// IO35 is the other free pin, but input-only with no pull-up (needs 10k to 3.3V).
constexpr int PIN_BTN_LEFT = 22;
constexpr int PIN_BTN_RIGHT = 21;
constexpr int PIN_BTN_LAUNCH = 0;

constexpr uint16_t TOUCH_CAL[8] = {3628, 3681, 3566, 308, 588, 3560, 504, 381};

LGFX_CYD35 tft;

bool pressed(int pin) { return pin >= 0 && digitalRead(pin) == LOW; }

}  // namespace

namespace board {

const char* const NAME = "CYD 3.5 (ESP32-3248S035R)";
const bool MULTITOUCH = false;
const int ALIGN_X = 1;
const uint8_t DEFAULT_VOLUME = 3;  // the buzzer's full 50% duty, as before
const uint8_t BRIGHT_FULL = 220;

bool init() {
  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_RGB_G, OUTPUT);
  pinMode(PIN_RGB_B, OUTPUT);
  led(false, false, false);
  ledcSetup(SPK_CH, 2000, 8);
  ledcAttachPin(PIN_SPK, SPK_CH);
  ledcWriteTone(SPK_CH, 0);
  for (int pin : {PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_LAUNCH})
    if (pin >= 0) pinMode(pin, INPUT_PULLUP);

  tft.init();
  tft.setRotation(0);
  uint16_t cal[8];
  memcpy(cal, TOUCH_CAL, sizeof cal);
  tft.setTouchCalibrate(cal);
  tft.setBrightness(BRIGHT_FULL);
  return true;
}

void beginFrame() { tft.startWrite(); }
void pushRect(int x, int y, int w, int h, uint16_t* px) {
  // waits for the previous DMA transfer, then starts this one and returns
  tft.pushImageDMA(x, y, w, h, (lgfx::swap565_t*)px);
}
void endFrame() { tft.endWrite(); }  // waits for the last DMA transfer

int readTouch(Touch* pts) {
  int32_t x, y;
  if (!tft.getTouch(&x, &y)) return 0;
  pts[0] = {(int16_t)x, (int16_t)y};
  return 1;
}

bool btnLeft() { return pressed(PIN_BTN_LEFT); }
bool btnRight() { return pressed(PIN_BTN_RIGHT); }
bool btnLaunch() { return pressed(PIN_BTN_LAUNCH); }

// volume is the PWM duty (of 1023): a buzzer has no other volume control
static const uint16_t DUTY[VOLUME_LEVELS] = {0, 40, 160, 511};
static uint8_t volume = 3;

void tone(uint16_t hz, uint8_t, uint16_t, uint16_t) {
  ledcWriteTone(SPK_CH, hz);  // sets 10-bit resolution at 50% duty
  if (hz) ledcWrite(SPK_CH, DUTY[volume]);
}
void setVolume(uint8_t level) { volume = level < VOLUME_LEVELS ? level : VOLUME_LEVELS - 1; }

void setBrightness(uint8_t v) { tft.setBrightness(v); }

void led(bool r, bool g, bool b) {
  digitalWrite(PIN_RGB_R, r ? LOW : HIGH);  // common anode: LOW = on
  digitalWrite(PIN_RGB_G, g ? LOW : HIGH);
  digitalWrite(PIN_RGB_B, b ? LOW : HIGH);
}

void readRowRGB(int y, uint8_t* rgb) { tft.readRectRGB(0, y, 320, 1, rgb); }

void serialFast(bool on) {
  Serial.flush();
  Serial.updateBaudRate(on ? 921600 : 115200);
}

}  // namespace board

#endif
