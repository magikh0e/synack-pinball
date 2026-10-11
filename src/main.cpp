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

void renderFrame() {
  board::beginFrame();
  if (game.fullRedraw) {
    game.fullRedraw = false;
    game.ndirty = 0;
    paint(0, 0, pb::SCR_W, pb::SCR_H);
  } else {
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
  board::endFrame();
}

// ---------------------------------------------------------------- sound
struct Note { uint16_t hz, ms; };
Note sq[12];
int sqLen = 0, sqPos = 0, sqPrio = -1;
uint32_t noteEnd = 0;

void sfx(int prio, std::initializer_list<Note> notes) {
  if (!SOUND_ON) return;
  if (sqPos < sqLen && prio < sqPrio) return;  // don't cut off something bigger
  sqLen = 0;
  for (const Note& n : notes) if (sqLen < 12) sq[sqLen++] = n;
  sqPos = 0;
  sqPrio = prio;
  noteEnd = 0;
}

void sfxUpdate() {
  uint32_t now = millis();
  if (now < noteEnd) return;
  if (sqPos < sqLen) {
    board::tone(sq[sqPos].hz);
    noteEnd = now + sq[sqPos].ms;
    sqPos++;
  } else if (noteEnd) {
    board::tone(0);
    noteEnd = 0;
    sqPrio = -1;
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

void handleEvents(uint32_t ev) {
  using namespace pb;
  if (game.demo) return;  // the demo plays silently
  if (ev & EV_MULTIBALL) {
    sfx(10, {{523, 60}, {784, 60}, {1047, 60}, {523, 60}, {784, 60}, {1047, 60},
             {1568, 90}, {1319, 90}, {1568, 90}, {2093, 260}});
    led(true, false, true, 1500);
  } else if (ev & EV_JACKPOT) {
    sfx(9, {{523, 70}, {659, 70}, {784, 70}, {1047, 120}, {784, 60}, {1047, 60}, {1319, 220}});
    led(true, true, true, 900);
  } else if (ev & EV_GAMEOVER) {
    sfx(8, {{392, 160}, {330, 160}, {262, 160}, {196, 400}});
    led(true, false, false, 1200);
  } else if (ev & EV_DRAIN) {
    sfx(7, {{330, 120}, {247, 120}, {165, 260}});
    led(true, false, false, 500);
  } else if (ev & (EV_SAVE | EV_KICKBACK)) {
    sfx(6, {{880, 60}, {1175, 60}, {1760, 120}});
    led(false, true, false, 400);
  } else if (ev & (EV_SYN | EV_ACK)) {
    sfx(5, {{784, 60}, {988, 60}, {1175, 60}, {1568, 140}});
    led(true, true, false, 400);
  } else if (ev & EV_RAMP) {
    sfx(5, {{660, 50}, {880, 50}, {1100, 50}, {1320, 50}, {1760, 120}});
    led(false, true, true, 300);
  } else if (ev & EV_START) {
    sfx(5, {{262, 80}, {392, 80}, {523, 140}});
  } else if (ev & EV_LANE) {
    sfx(3, {{1319, 40}, {1760, 60}});
    led(true, true, false, 120);
  } else if (ev & EV_TARGET) {
    sfx(3, {{988, 30}, {1480, 50}});
    led(false, true, false, 120);
  } else if (ev & EV_BUMPER) {
    sfx(2, {{1568, 25}, {1175, 25}});
    led(true, false, true, 70);
  } else if (ev & EV_SPIN) {
    sfx(1, {{2400, 6}});
  } else if (ev & EV_SLING) {
    sfx(1, {{700, 20}, {500, 20}});
    led(true, false, false, 50);
  } else if (ev & EV_LAUNCH) {
    sfx(2, {{180, 30}, {260, 30}, {360, 40}});
  } else if (ev & EV_FLIP) {
    sfx(0, {{110, 12}});
  }
}

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
  *tapEdge = now && !touching;
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
  // pressing any button counts as a tap: starts a game, leaves the demo,
  // takes over from the serial bot
  static bool btnPrev = false;
  bool anyBtn = bl || br || bx;
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
  game.hiscore = prefs.getUInt("hi", 0);

  game.fullRedraw = true;
  renderFrame();
  Serial.printf("[pinball] ready on %s, display %s, heap %u, hi %u\n", board::NAME,
                displayOk ? "ok" : "FAILED", ESP.getFreeHeap(), game.hiscore);
}

void loop() {
  static uint32_t last = micros();
  static uint32_t fpsT = millis(), frames = 0;

  // serial debug: 'b' toggles the bot, 'm' starts multiball, 'p' screenshot,
  // 'h<n>' sets the high score (e.g. "h0" clears it)
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
    } else if (c == 'h') {
      game.hiscore = (uint32_t)Serial.parseInt();
      prefs.putUInt("hi", game.hiscore);
      game.markHud();
      Serial.printf("[pinball] hi set to %u\n", game.hiscore);
    }
  }
  uint32_t now = micros();
  float dt = (now - last) * 1e-6f;
  last = now;
  static float worstDt = 0;
  if (dt > worstDt) worstDt = dt;  // anything over 50 ms plays in slow motion

  bool tap;
  pb::Input in = readInput(&tap);
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

  uint32_t hiBefore = game.hiscore;
  game.frame(dt, in);
  if (game.events) {
    handleEvents(game.events);
    game.events = 0;
  }
  if (game.hiscore != hiBefore) prefs.putUInt("hi", game.hiscore);

  renderFrame();
  sfxUpdate();
  ledUpdate();

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
