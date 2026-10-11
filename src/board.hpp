// SYN/ACK pinball: the hardware a table needs, per board.
//
// main.cpp is board-agnostic; each board_*.cpp implements this for one
// target and compiles to nothing on the others:
//   board_cyd.cpp      Sunton ESP32-3248S035R (CYD 3.5"), env esp32-3248s035r
//   board_es3c35p.cpp  LCDwiki ES3C35P (ESP32-S3 3.5"),   env es3c35p
// Both are 320x480 portrait, so the table itself needs no changes.

#pragma once
#include <stdint.h>

namespace board {

constexpr int MAX_TOUCH = 5;
struct Touch {
  int16_t x, y;
};

extern const char* const NAME;
// true: the panel reports each finger separately (left/right halves are the
// flippers). false: one averaged point (thirds, middle = both flippers).
extern const bool MULTITOUCH;
// pushRect windows must start and span multiples of this many columns
// (the ST77922 rejects anything else and smears the pixels diagonally)
extern const int ALIGN_X;

bool init();  // display, touch, sound, LED, buttons; false if the display failed

// Pixels are RGB565, byte-swapped (LovyanGFX sprite layout). A push may
// return before the transfer finishes (DMA); the caller alternates buffers
// and endFrame() waits for the last one.
void beginFrame();
void pushRect(int x, int y, int w, int h, uint16_t* px);
void endFrame();

int readTouch(Touch* pts);  // raw points down right now, in screen pixels

bool btnLeft();
bool btnRight();
bool btnLaunch();

// Sound: one voice. A note starts at hz, optionally sweeps to toHz over ms,
// and fades like a struck sound. Boards play what they can: the ES3C35P
// synthesizes every waveform; the CYD's buzzer plays a square at hz.
enum Wave : uint8_t { W_SQUARE, W_PULSE, W_TRI, W_SAW, W_NOISE };
void tone(uint16_t hz, uint8_t wave = W_SQUARE, uint16_t toHz = 0, uint16_t ms = 0);
constexpr int VOLUME_LEVELS = 4;  // 0 off, 1 low, 2 medium, 3 high
extern const uint8_t DEFAULT_VOLUME;
void setVolume(uint8_t level);

extern const uint8_t BRIGHT_FULL;  // the board's normal backlight level
void setBrightness(uint8_t v);     // 0 = off
void led(bool r, bool g, bool b);

// screenshot support: one screen row as RGB888, and the serial speed the
// row stream uses (a no-op over native USB, where baud means nothing)
void readRowRGB(int y, uint8_t* rgb);
void serialFast(bool on);

}  // namespace board
