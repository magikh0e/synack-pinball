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

void tone(uint16_t hz);  // square-ish beep, 0 = silence
void led(bool r, bool g, bool b);

// screenshot support: one screen row as RGB888, and the serial speed the
// row stream uses (a no-op over native USB, where baud means nothing)
void readRowRGB(int y, uint8_t* rgb);
void serialFast(bool on);

}  // namespace board
