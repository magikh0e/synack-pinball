// SYN/ACK pinball - main loop, rendering, sound sequencing and input mapping.
// Board-agnostic: the hardware lives behind board.hpp (board_cyd.cpp for the
// CYD 3.5", board_es3c35p.cpp for the ESP32-S3 ES3C35P).
//
// Touch controls:
//   single-touch resistive (CYD): left third = left flipper, right third =
//     right flipper, middle = both (a thumb on each side reads as the middle,
//     because a resistive panel averages two touches)
//   multi-touch capacitive (ES3C35P): each finger on the left/right half
//     holds that flipper
//   ball in the plunger lane: hold anywhere (or the launch button) to charge,
//   release to fire
//
// Rendering: the screen is never redrawn whole during play. game.hpp marks
// dirty rectangles (ball, moving flippers, lights, score); each one is painted
// into a small RAM sprite by render.hpp and pushed by the board, double-buffered
// so on DMA-capable boards the next region renders while the last is in flight.

#include <Arduino.h>
#include <Preferences.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board.hpp"
#include "game.hpp"
#include "render.hpp"
#include "bot.hpp"

constexpr bool SOUND_ON = true;

LGFX_Sprite spr;  // no parent: it only ever draws into our buffers
Preferences prefs;
pb::Game game;

// two render buffers, 320x48 px each (30 KB) - DMA capable internal RAM
constexpr int BUF_PIX = 320 * 48;
uint16_t* bufs[2];
int curBuf = 0;

// ---------------------------------------------------------------- canvas
// pb::drawWorld() target: the sprite positioned at (ox, oy) on screen.
struct DevCanvas {
  LGFX_Sprite* s;
  int ox = 0, oy = 0, w = 0, h = 0;

  bool vis(float x0, float y0, float x1, float y1) const {
    return x1 >= ox && x0 < ox + w && y1 >= oy && y0 < oy + h;
  }
  static int I(float v) { return (int)lroundf(v); }
  void clear(uint32_t c) { s->fillScreen(c); }
  void rect(int x, int y, int rw, int rh, uint32_t c) { s->fillRect(x - ox, y - oy, rw, rh, c); }
  void hline(int x, int y, int n, uint32_t c) { s->drawFastHLine(x - ox, y - oy, n, c); }
  void vline(int x, int y, int n, uint32_t c) { s->drawFastVLine(x - ox, y - oy, n, c); }
  void wide(float x0, float y0, float x1, float y1, float r, uint32_t c) {
    s->drawWideLine(I(x0) - ox, I(y0) - oy, I(x1) - ox, I(y1) - oy, r, c);
  }
  void wedge(float x0, float y0, float x1, float y1, float r0, float r1, uint32_t c) {
    s->drawWedgeLine(I(x0) - ox, I(y0) - oy, I(x1) - ox, I(y1) - oy, r0, r1, c);
  }
  void disc(float x, float y, float r, uint32_t c) {
    s->fillSmoothCircle(I(x) - ox, I(y) - oy, I(r), c);
  }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
    s->fillTriangle(I(x0) - ox, I(y0) - oy, I(x1) - ox, I(y1) - oy, I(x2) - ox, I(y2) - oy, c);
  }
  static const lgfx::IFont* font(pb::Font f) {
    switch (f) {
      case pb::F_SMALL: return &fonts::Font2;
      case pb::F_HUD:   return &fonts::Font4;
      case pb::F_TITLE: return &fonts::Orbitron_Light_24;
      default:          return &fonts::Orbitron_Light_32;
    }
  }
  void text(const char* str, int x, int y, pb::Datum d, pb::Font f, uint32_t c) {
    static const textdatum_t map[] = {top_left, top_center, top_right,
                                      middle_left, middle_center, middle_right};
    s->setFont(font(f));
    s->setTextDatum(map[d]);
    s->setTextColor(c);
    s->drawString(str, x - ox, y - oy);
  }
  // centred text that steps down a font size until it fits maxW
  void textFit(const char* str, int x, int y, pb::Font f, int maxW, uint32_t c) {
    s->setFont(font(f));
    if (s->textWidth(str) > maxW) f = pb::F_HUD;
    s->setFont(font(f));
    if (s->textWidth(str) > maxW) f = pb::F_SMALL;
    text(str, x, y, pb::D_MC, f, c);
  }
};
DevCanvas canvas{&spr};

// paint one screen rectangle, in horizontal chunks that fit a buffer
void paint(int x, int y, int w, int h) {
  int rows = BUF_PIX / w;
  for (int yy = y; yy < y + h; yy += rows) {
    int hh = min(rows, y + h - yy);
    uint16_t* b = bufs[curBuf];
    curBuf ^= 1;
    spr.setBuffer(b, w, hh, 16);
    canvas.ox = x; canvas.oy = yy; canvas.w = w; canvas.h = hh;
    pb::drawWorld(canvas, game);
    board::pushRect(x, yy, w, hh, b);
  }
}

bool overlaps(const pb::Rect& a, const pb::Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// A full redraw (new game, game over, waking) takes ~170 ms on the ES3C35P,
// and a frame that long plays in slow motion. So it goes out in bands over a
// few frames, alongside the normal dirty rects; anything that changes in a
// band not yet repainted is dirty anyway, so the screen is right throughout.
constexpr int FULL_BAND = 48;  // rows per frame (one render buffer): 10 frames of ~20 ms
int fullRow = pb::SCR_H;       // next row of a full redraw still to paint

void renderFrame() {
  board::beginFrame();
  if (game.fullRedraw) {
    game.fullRedraw = false;
    fullRow = 0;
  }
  {
    pb::Rect* d = game.dirty;
    int n = game.ndirty;
    // widen to the panel's column alignment before merging
    const int A = board::ALIGN_X;
    if (A > 1)
      for (int i = 0; i < n; i++) {
        int x0 = d[i].x / A * A;
        int x1 = min((d[i].x + d[i].w + A - 1) / A * A, (int)pb::SCR_W);
        d[i].x = x0;
        d[i].w = x1 - x0;
      }
    // merge overlapping rects so no pixel is painted twice per frame
    for (bool merged = true; merged;) {
      merged = false;
      for (int i = 0; i < n && !merged; i++)
        for (int j = i + 1; j < n; j++)
          if (overlaps(d[i], d[j])) {
            int x0 = min(d[i].x, d[j].x), y0 = min(d[i].y, d[j].y);
            int x1 = max(d[i].x + d[i].w, d[j].x + d[j].w);
            int y1 = max(d[i].y + d[i].h, d[j].y + d[j].h);
            d[i] = {(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
            d[j] = d[--n];
            merged = true;
            break;
          }
    }
    for (int i = 0; i < n; i++) paint(d[i].x, d[i].y, d[i].w, d[i].h);
    game.ndirty = 0;
  }
  if (fullRow < pb::SCR_H) {
    int h = min(FULL_BAND, pb::SCR_H - fullRow);
    paint(0, fullRow, pb::SCR_W, h);
    fullRow += h;
  }
  board::endFrame();
}

// ---------------------------------------------------------------- sound
// A note: start pitch, length, waveform, and an optional pitch to sweep to.
using board::W_SQUARE; using board::W_PULSE; using board::W_TRI; using board::W_SAW;
using board::W_NOISE;
struct Note {
  uint16_t hz, ms;
  uint8_t wave = W_SQUARE;
  uint16_t to = 0;
};
uint8_t volume = 2;  // 0..3, kept in NVS

// One channel per synth voice, each with its own note queue. Clicks and ticks
// (priority 0-1: spinner, slings, flipper presses, bonus ticks) share the last
// voice, so they can never cut off a bumper or a fanfare; everything else
// takes an idle voice, or bumps the lowest-priority one if it outranks it.
struct Chan {
  Note q[12];
  int len = 0, pos = 0, prio = -1;
  uint32_t end = 0;
  bool idle() const { return pos >= len && !end; }
};
Chan chans[board::VOICES];

void sfx(int prio, std::initializer_list<Note> notes) {
  if (!SOUND_ON || volume == 0) return;
  constexpr int NV = board::VOICES;
  int pick = -1;
  if (prio <= 1) {
    pick = NV - 1;
  } else {
    for (int i = 0; i < NV - 1 && pick < 0; i++)
      if (chans[i].idle()) pick = i;
    for (int i = 0; i < NV - 1 && pick < 0; i++)
      if (chans[i].prio <= prio && (pick < 0 || chans[i].prio < chans[pick].prio)) pick = i;
    if (pick < 0) return;  // everything playing matters more
  }
  Chan& c = chans[pick];
  c.len = 0;
  for (const Note& n : notes) if (c.len < 12) c.q[c.len++] = n;
  c.pos = 0;
  c.prio = prio;
  c.end = 0;
}

void sfxUpdate() {
  uint32_t now = millis();
  for (int v = 0; v < board::VOICES; v++) {
    Chan& c = chans[v];
    if (c.end && now < c.end) continue;
    if (c.pos < c.len) {
      const Note& n = c.q[c.pos++];
      board::tone(v, n.hz, n.wave, n.to, n.ms);
      c.end = now + n.ms;
    } else if (c.end) {
      board::tone(v, 0);
      c.end = 0;
      c.prio = -1;
    }
  }
}

// ---------------------------------------------------------------- RGB LED
uint32_t ledUntil = 0;
void led(bool r, bool g, bool b, uint32_t ms) {
  board::led(r, g, b);
  ledUntil = millis() + ms;
}
void ledUpdate() {
  if (ledUntil && millis() > ledUntil) { led(false, false, false, 0); ledUntil = 0; }
}

// ---------------------------------------------------------------- button lamps
// The cabinet's LED flipper buttons. On the table they glow, flare while
// pressed and flash on the big moments; off it (title, demo, game over) they
// breathe in turn, asking to be pressed.
float lampLvl[2] = {0, 0};
uint32_t lampFlashUntil = 0;
uint16_t lampFlashMs = 1;  // half of one on/off cycle
void lampFlash(uint32_t ms, uint16_t half) {
  lampFlashUntil = millis() + ms;
  lampFlashMs = half;
}
void lampsUpdate(const pb::Input& in, float dt, bool dimmed) {
  uint32_t t = millis();
  float out[2];
  if (game.state != pb::PLAY || game.demo) {
    float s = sinf(t * (2 * PI / 2400.0f));
    float k = dimmed ? 0.3f : 1.0f;
    out[0] = k * (0.5f + 0.5f * s);
    out[1] = k * (0.5f - 0.5f * s);
    lampFlashUntil = 0;
  } else {
    bool press[2] = {in.left, in.right};
    for (int i = 0; i < 2; i++) {
      lampLvl[i] = press[i] ? 1.0f : fmaxf(0.25f, lampLvl[i] - dt * 3);  // fade back to a glow
      out[i] = lampLvl[i];
    }
    if (t < lampFlashUntil) out[0] = out[1] = (t / lampFlashMs) % 2 ? 0.0f : 1.0f;
  }
  // squared, so the fades look even to the eye
  board::buttonLamps((uint8_t)(out[0] * out[0] * 255), (uint8_t)(out[1] * out[1] * 255));
}

// Sound design. On the ES3C35P every field is heard; the CYD's buzzer plays
// the start pitch of each note as a square wave.
void handleEvents(uint32_t ev) {
  using namespace pb;
  if (game.demo) return;  // the demo plays silently
  if (ev & EV_MULTIBALL) {
    sfx(10, {{523, 60, W_PULSE}, {784, 60, W_PULSE}, {1047, 60, W_PULSE}, {523, 60, W_PULSE},
             {784, 60, W_PULSE}, {1047, 60, W_PULSE}, {1568, 90, W_PULSE}, {1319, 90, W_PULSE},
             {1568, 90, W_PULSE}, {2093, 400, W_SAW, 2600}});
    led(true, false, true, 1500);
    lampFlash(1500, 70);
  } else if (ev & EV_EXTRA) {
    sfx(9, {{392, 80, W_PULSE}, {523, 80, W_PULSE}, {659, 80, W_PULSE}, {784, 80, W_PULSE},
            {1047, 120, W_PULSE}, {784, 80, W_PULSE}, {1047, 500, W_SAW, 1568}});
    led(true, true, true, 1200);
    lampFlash(1200, 60);
  } else if (ev & EV_JACKPOT) {
    sfx(9, {{523, 70, W_PULSE}, {659, 70, W_PULSE}, {784, 70, W_PULSE}, {1047, 120, W_PULSE},
            {784, 60, W_PULSE}, {1047, 60, W_PULSE}, {1319, 350, W_SAW, 1760}});
    led(true, true, true, 900);
    lampFlash(900, 60);
  } else if (ev & EV_GAMEOVER) {
    sfx(8, {{392, 200, W_TRI}, {330, 200, W_TRI}, {262, 200, W_TRI}, {196, 700, W_SAW, 98}});
    led(true, false, false, 1200);
  } else if (ev & EV_DRAIN) {
    sfx(7, {{440, 650, W_SAW, 70}});
    led(true, false, false, 500);
    lampFlash(1000, 250);
  } else if (ev & (EV_SAVE | EV_KICKBACK)) {
    sfx(6, {{880, 70, W_TRI}, {1175, 70, W_TRI}, {1760, 160, W_TRI}});
    led(false, true, false, 400);
    lampFlash(400, 100);
  } else if (ev & (EV_SYN | EV_ACK)) {
    sfx(5, {{784, 60, W_PULSE}, {988, 60, W_PULSE}, {1175, 60, W_PULSE}, {1568, 200, W_PULSE}});
    led(true, true, false, 400);
  } else if (ev & EV_SKILL) {
    sfx(6, {{400, 220, W_SAW, 2000}, {2093, 90, W_TRI}, {2637, 160, W_TRI}});
    led(true, false, true, 400);
    lampFlash(400, 50);
  } else if (ev & EV_RAMP) {
    sfx(5, {{300, 260, W_SAW, 1500}, {1760, 160, W_TRI}});
    led(false, true, true, 300);
  } else if (ev & EV_START) {
    sfx(5, {{262, 90, W_TRI}, {392, 90, W_TRI}, {523, 200, W_TRI}});
  } else if (ev & EV_BONUS_LINE) {
    sfx(4, {{1047, 50, W_TRI}, {1568, 90, W_TRI}});
  } else if (ev & EV_LANE) {
    sfx(3, {{1319, 45, W_TRI}, {1760, 80, W_TRI}});
    led(true, true, false, 120);
  } else if (ev & EV_TARGET) {
    sfx(3, {{988, 70, W_SQUARE, 1480}});
    led(false, true, false, 120);
  } else if (ev & EV_ENTRY) {
    sfx(9, {{784, 70, W_PULSE}, {1047, 70, W_PULSE}, {1319, 70, W_PULSE}, {1568, 220, W_TRI}});
  } else if (ev & EV_LETTER_OK) {
    sfx(4, {{1319, 50, W_TRI}, {1760, 80, W_TRI}});
  } else if (ev & EV_LETTER) {
    sfx(3, {{988, 25, W_PULSE}});
  } else if (ev & EV_BALL_HIT) {  // clack: a click, brighter for a hard hit
    if (game.clackHard) sfx(2, {{4200, 8, W_NOISE}, {1500, 24, W_TRI, 900}});
    else sfx(1, {{3600, 5, W_NOISE}, {1700, 12, W_TRI}});
  } else if (ev & EV_FLIP_HIT) {
    sfx(2, {{2800, 10, W_NOISE}, {190, 45, W_SQUARE, 90}});  // thwack
  } else if (ev & EV_BUMPER) {
    sfx(2, {{3200, 18, W_NOISE}, {260, 55, W_SQUARE, 110}});
    led(true, false, true, 70);
  } else if (ev & EV_BONUS_TICK) {
    sfx(1, {{2093, 12, W_PULSE}});
  } else if (ev & EV_SPIN) {
    sfx(1, {{2600, 6, W_PULSE}});
  } else if (ev & EV_SLING) {
    sfx(1, {{2400, 14, W_NOISE}, {520, 28, W_SQUARE, 300}});
    led(true, false, false, 50);
  } else if (ev & EV_LAUNCH) {
    sfx(2, {{1500, 170, W_NOISE, 6000}});
  } else if (ev & EV_FLIP) {
    sfx(0, {{140, 28, W_TRI, 70}});
  }
}

// one step of the volume (off, low, medium, high, off...), saved, with a sample
void stepVolume() {
  volume = (volume + 1) % board::VOLUME_LEVELS;
  board::setVolume(volume);
  prefs.putUChar("vol", volume);
  static const char* names[] = {"SOUND OFF", "VOLUME LOW", "VOLUME MED", "VOLUME HIGH"};
  game.notify(names[volume]);
  sfx(9, {{1047, 70, W_TRI}, {1568, 120, W_TRI}});
}

// the high-score table lives in NVS as one blob; a board with only the old
// single high score ("hi") starts its table from that
void loadScores() {
  if (prefs.getBytesLength("hs") == sizeof game.hs) {
    prefs.getBytes("hs", game.hs, sizeof game.hs);
  } else {
    memset(game.hs, 0, sizeof game.hs);
    uint32_t hi = prefs.getUInt("hi", 0);
    if (hi) {
      game.hs[0].score = hi;
      memcpy(game.hs[0].ini, "---", pb::HS_LEN + 1);
    }
  }
  game.syncHi();
}
void saveScores() {
  prefs.putBytes("hs", game.hs, sizeof game.hs);
  prefs.putUInt("hi", game.hiscore);
  game.hsChanged = false;
}

// ---------------------------------------------------------------- idle
// No touch or button for a while: dim the backlight, then switch it off and
// stop drawing. Any touch wakes it; the touch that wakes a dark screen is
// swallowed so it doesn't also start a game.
constexpr uint32_t DIM_AFTER_MS = 2 * 60 * 1000;
constexpr uint32_t SLEEP_AFTER_MS = 10 * 60 * 1000;
constexpr uint8_t BRIGHT_DIM = 30;
uint32_t lastInputMs = 0;
enum Power : uint8_t { AWAKE, DIMMED, ASLEEP } power = AWAKE;

// ---------------------------------------------------------------- input
bool touching = false;
int releaseCount = 0;
board::Touch lastPts[board::MAX_TOUCH];
int lastN = 0;

pb::Input readInput(bool* tapEdge) {
  board::Touch pts[board::MAX_TOUCH];
  int n = board::readTouch(pts);
  if (!board::MULTITOUCH) {
    // a resistive panel drops out for a sample now and then; need two misses
    if (n) { lastPts[0] = pts[0]; lastN = 1; releaseCount = 0; }
    else if (++releaseCount >= 2) lastN = 0;
  } else {
    memcpy(lastPts, pts, sizeof(board::Touch) * n);  // the board debounces
    lastN = n;
  }
  bool now = lastN > 0;
  *tapEdge = false;  // touches tap on release, in loop()
  touching = now;

  pb::Input in{false, false, now};
  for (int i = 0; i < lastN; i++) {
    int x = lastPts[i].x;
    if (board::MULTITOUCH) {
      (x < pb::SCR_W / 2 ? in.left : in.right) = true;
    } else if (x < 125) {
      in.left = true;
    } else if (x > 195) {
      in.right = true;
    } else {
      in.left = in.right = true;
    }
  }
  bool bl = board::btnLeft(), br = board::btnRight(), bx = board::btnLaunch();
  in.left |= bl;
  in.right |= br;
  in.touch |= bx;  // the launch button charges the plunger like a touch
  in.launch = bx;
  lastInputMs = (n || bl || br || bx) ? millis() : lastInputMs;
  // pressing a flipper button counts as a tap: starts a game, leaves the
  // demo, takes over from the serial bot. (BOOT is handled in loop(): off the
  // table a short press is a tap and a long press changes the volume.)
  static bool btnPrev = false;
  bool anyBtn = bl || br;
  if (anyBtn && !btnPrev) *tapEdge = true;
  btnPrev = anyBtn;
  return in;
}

// ---------------------------------------------------------------- screenshot
// Serial 'p': reads the screen back and streams it as raw RGB888 rows
// (host/shot.py turns it into a PNG). On the CYD that runs at 921600 baud,
// then returns to 115200; over native USB the baud rate is moot.
void dumpScreen() {
  static uint8_t row[pb::SCR_W * 3];
  Serial.printf("[pinball] SCREEN %d %d\n", pb::SCR_W, pb::SCR_H);
  board::serialFast(true);
  delay(400);
  for (int y = 0; y < pb::SCR_H; y++) {
    board::readRowRGB(y, row);
    Serial.write(row, sizeof row);
  }
  Serial.flush();
  delay(100);
  board::serialFast(false);
}

// ---------------------------------------------------------------- bot
// The bot plays the attract-mode demo. Serial 'b' also hands it a real game
// (for soak-testing frame rate on hardware).
pb::Bot bot;
bool botOn = false;

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(115200);
  bool displayOk = board::init();

  for (auto& b : bufs) b = (uint16_t*)heap_caps_malloc(BUF_PIX * 2, MALLOC_CAP_DMA);
  if (!bufs[0] || !bufs[1]) {
    Serial.println("[pinball] render buffer alloc failed");
    while (true) delay(1000);
  }

  prefs.begin("pinball", false);
  loadScores();
  volume = prefs.getUChar("vol", board::DEFAULT_VOLUME);
  board::setVolume(volume);
  lastInputMs = millis();

  board::beginFrame();
  paint(0, 0, pb::SCR_W, pb::SCR_H);  // the first frame whole, before the backlight
  board::endFrame();
  game.fullRedraw = false;
  Serial.printf("[pinball] ready on %s, display %s, heap %u, hi %u, volume %u\n", board::NAME,
                displayOk ? "ok" : "FAILED", ESP.getFreeHeap(), game.hiscore, volume);
}

void loop() {
  static uint32_t last = micros();
  static uint32_t fpsT = millis(), frames = 0;

  // serial debug: 'b' toggles the bot, 'm' starts multiball, 'p' screenshot,
  // 'h' prints the high-score table and 'h0' clears it, 'o<n>' shows the
  // game-over screen with score n (the saved high score is untouched),
  // 'v' prints the volume and 'v<0-3>' sets it (saved, with a sample)
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'b') {
      botOn = !botOn;
      // a game the bot touches is a demo: silent, and never a high score
      if (botOn && game.state == pb::PLAY) game.demo = true;
      Serial.printf("[pinball] bot %s\n", botOn ? "on" : "off");
    } else if (c == 'p') {
      dumpScreen();
    } else if (c == 'm') {
      game.startMultiball();
    } else if (c == 'o') {
      game.debugGameOver((uint32_t)Serial.parseInt());
    } else if (c == 'h') {
      // 'h' prints the high-score table, 'h0' clears it
      uint32_t t0 = millis();
      while (!Serial.available() && millis() - t0 < 50) delay(1);
      if (Serial.available() && Serial.peek() == '0') {
        Serial.read();
        memset(game.hs, 0, sizeof game.hs);
        game.syncHi();
        saveScores();
        game.markHud();
        game.fullRedraw = true;
      }
      for (int i = 0; i < pb::HS_N; i++)
        Serial.printf("[pinball] hs %d %s %u\n", i + 1, game.hs[i].ini, game.hs[i].score);
    } else if (c == 'v') {
      // a digit right after it sets the level; 'v' alone just reports it
      uint32_t t0 = millis();
      while (!Serial.available() && millis() - t0 < 50) delay(1);
      int d = Serial.available() ? Serial.peek() : -1;
      if (d >= '0' && d < '0' + board::VOLUME_LEVELS) {
        Serial.read();
        volume = d - '0';
        board::setVolume(volume);
        prefs.putUChar("vol", volume);
        sfx(9, {{1047, 70, W_TRI}, {1568, 120, W_TRI}});  // a sample at the new level
      }
      Serial.printf("[pinball] volume %u of %d\n", volume, board::VOLUME_LEVELS - 1);
    }
  }
  uint32_t now = micros();
  float dt = (now - last) * 1e-6f;
  last = now;
  static float worstDt = 0;
  if (dt > worstDt) worstDt = dt;  // anything over 50 ms plays in slow motion

  bool tap;
  pb::Input in = readInput(&tap);
  if (botOn) lastInputMs = millis();  // a soak test isn't idle

  // ---- idle power: dim, then dark; wake on input
  uint32_t idle = millis() - lastInputMs;
  static bool swallow = false;  // the touch that woke a dark screen
  if (power == ASLEEP) {
    if (idle < 100) {
      power = AWAKE;
      board::setBrightness(board::BRIGHT_FULL);
      game.fullRedraw = true;
      game.stateT = 0;  // no demo straight away
      swallow = true;
      last = micros();
      Serial.println("[pinball] wake");
    } else {
      delay(30);
      return;
    }
  }
  if (swallow) {
    if (in.touch || in.left || in.right) { tap = false; in = {false, false, false}; }
    else swallow = false;
  }
  if (power == DIMMED && idle < 100) {
    power = AWAKE;
    board::setBrightness(board::BRIGHT_FULL);
  } else if (power == AWAKE && idle > DIM_AFTER_MS) {
    power = DIMMED;
    board::setBrightness(BRIGHT_DIM);
    Serial.println("[pinball] dim");
  }
  if (power == DIMMED && idle > SLEEP_AFTER_MS) {
    power = ASLEEP;
    if (game.demo) game.endDemo();
    for (int v = 0; v < board::VOICES; v++) board::tone(v, 0);
    board::led(false, false, false);
    board::buttonLamps(0, 0);
    board::setBrightness(0);
    Serial.println("[pinball] sleep");
    return;
  }

  // ---- BOOT off the table: short press = tap, long press = volume
  {
    static bool bootPrev = false, bootLong = false;
    static uint32_t bootDown = 0;
    bool boot = board::btnLaunch();
    bool offTable = (game.state != pb::PLAY || game.demo) && !game.entering;
    if (boot && !bootPrev) { bootDown = millis(); bootLong = false; }
    if (boot && offTable && !bootLong && millis() - bootDown > 800) {
      bootLong = true;
      stepVolume();
    }
    if (!boot && bootPrev && offTable && !bootLong) tap = true;
    bootPrev = boot;
  }

  // ---- the screen off the table, the same way: a short touch is a tap (on
  // release), one held 0.8 s steps the volume. On the table a touch is a
  // flipper or the plunger, and while initials are entered it picks letters.
  {
    static bool scrPrev = false, scrLong = false;
    static uint32_t scrDown = 0;
    bool scr = touching && !swallow;
    bool offTable = (game.state != pb::PLAY || game.demo) && !game.entering;
    if (scr && !scrPrev) { scrDown = millis(); scrLong = false; }
    if (scr && offTable && !scrLong && millis() - scrDown > 800) {
      scrLong = true;
      stepVolume();
    }
    if (!scr && scrPrev && offTable && !scrLong) tap = true;
    scrPrev = scr;
  }

  if (botOn && tap) {
    // a touch always takes the game back from the serial-debug bot
    botOn = false;
    Serial.println("[pinball] bot off (touch)");
  }
  if (botOn) {
    if (game.state != pb::PLAY) game.startDemo();
    in = bot.step(game, dt);
  } else {
    // a touch on the title / game-over screen, or anywhere during the demo,
    // starts a real game
    if (tap && (game.state != pb::PLAY || game.demo)) game.tap();
    else if (game.demoDue()) game.startDemo();
    if (game.demo) in = bot.step(game, dt);
  }
  static bool wasDemo = false;
  if (game.demo != wasDemo) {
    wasDemo = game.demo;
    Serial.printf("[pinball] demo %s\n", wasDemo ? "start" : "end");
  }

  game.frame(dt, in);
  if (game.events) {
    handleEvents(game.events);
    game.events = 0;
  }
  if (game.hsChanged) saveScores();

  renderFrame();
  sfxUpdate();
  ledUpdate();
  lampsUpdate(in, dt, power == DIMMED);

  frames++;
  if (millis() - fpsT >= 5000) {
    Serial.printf("[pinball] %.1f fps  worst frame %.1f ms  heap %u  score %u\n",
                  frames * 1000.0f / (millis() - fpsT), worstDt * 1000, ESP.getFreeHeap(),
                  game.score);
    worstDt = 0;
    fpsT = millis();
    frames = 0;
  }

  // cap at ~90 fps; physics is time-based so the rate just sets smoothness
  while (micros() - now < 11000) sfxUpdate();
}
