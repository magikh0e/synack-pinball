// Host-side harness for game.hpp / render.hpp.
//   g++ -O2 -std=c++17 -I../src sim.cpp -o sim && ./sim
// Renders the table to table.ppm (with ball traces) and runs physics tests:
// plunger launches, flipper shots, and a long bot-played soak looking for
// escapes, NaNs and stuck balls.

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cmath>
#include "game.hpp"
#include "render.hpp"
#include "bot.hpp"

using namespace pb;

struct HostCanvas {
  std::vector<uint32_t> px = std::vector<uint32_t>(SCR_W * SCR_H, 0);
  int ox = 0, oy = 0, w = SCR_W, h = SCR_H;

  void put(int x, int y, uint32_t c) {
    if (x < ox || y < oy || x >= ox + w || y >= oy + h) return;
    if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
    px[y * SCR_W + x] = c;
  }
  bool vis(float x0, float y0, float x1, float y1) const {
    return x1 >= ox && x0 < ox + w && y1 >= oy && y0 < oy + h;
  }
  void clear(uint32_t c) { rect(ox, oy, w, h, c); }
  void rect(int x, int y, int rw, int rh, uint32_t c) {
    for (int j = y; j < y + rh; j++)
      for (int i = x; i < x + rw; i++) put(i, j, c);
  }
  void hline(int x, int y, int n, uint32_t c) { rect(x, y, n, 1, c); }
  void vline(int x, int y, int n, uint32_t c) { rect(x, y, 1, n, c); }
  void wedge(float x0, float y0, float x1, float y1, float r0, float r1, uint32_t c) {
    float R = fmaxf(r0, r1);
    int bx0 = (int)floorf(fminf(x0, x1) - R), bx1 = (int)ceilf(fmaxf(x0, x1) + R);
    int by0 = (int)floorf(fminf(y0, y1) - R), by1 = (int)ceilf(fmaxf(y0, y1) + R);
    V a{x0, y0}, d{x1 - x0, y1 - y0};
    float L2 = dot(d, d);
    for (int y = by0; y <= by1; y++)
      for (int x = bx0; x <= bx1; x++) {
        V q{(float)x, (float)y};
        float t = L2 > 0 ? clampf(dot(q - a, d) / L2, 0, 1) : 0;
        V cpt = a + d * t;
        float r = r0 + (r1 - r0) * t;
        if (dot(q - cpt, q - cpt) <= r * r) put(x, y, c);
      }
  }
  void wide(float x0, float y0, float x1, float y1, float r, uint32_t c) { wedge(x0, y0, x1, y1, r, r, c); }
  void disc(float x, float y, float r, uint32_t c) { wedge(x, y, x, y, r, r, c); }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
    int bx0 = (int)fminf(x0, fminf(x1, x2)), bx1 = (int)fmaxf(x0, fmaxf(x1, x2));
    int by0 = (int)fminf(y0, fminf(y1, y2)), by1 = (int)fmaxf(y0, fmaxf(y1, y2));
    auto e = [](float ax, float ay, float bx, float by, float px, float py) {
      return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    for (int y = by0; y <= by1; y++)
      for (int x = bx0; x <= bx1; x++) {
        float a = e(x0, y0, x1, y1, x, y), b = e(x1, y1, x2, y2, x, y), cc = e(x2, y2, x0, y0, x, y);
        if ((a >= 0 && b >= 0 && cc >= 0) || (a <= 0 && b <= 0 && cc <= 0)) put(x, y, c);
      }
  }
  void text(const char* s, int x, int y, Datum d, Font f, uint32_t c) {
    // no font rasteriser on the host: draw a box roughly where the text goes
    int cw = f == F_SMALL ? 7 : (f == F_HUD ? 14 : 16), ch = f == F_SMALL ? 12 : 22;
    int tw = (int)strlen(s) * cw;
    int x0 = d == D_TL || d == D_ML ? x : (d == D_TR || d == D_MR ? x - tw : x - tw / 2);
    int y0 = d <= D_TR ? y : y - ch / 2;
    for (int i = 0; i < tw; i++) { put(x0 + i, y0, c); put(x0 + i, y0 + ch - 1, c); }
    for (int j = 0; j < ch; j++) { put(x0, y0 + j, c); put(x0 + tw - 1, y0 + j, c); }
  }
  void textFit(const char* s, int x, int y, Font f, int, uint32_t c) { text(s, x, y, D_MC, f, c); }
  void save(const char* path) {
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (uint32_t c : px) { fputc(c >> 16, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); }
    fclose(f);
  }
};


static bool badBall(const Ball& b) {
  return b.live && (!std::isfinite(b.p.x) || !std::isfinite(b.p.y) || b.p.x < 0 ||
                    b.p.x > SCR_W || b.p.y < 20);
}
static bool bad(const Game& g) {
  for (const Ball& b : g.balls)
    if (badBall(b)) return true;
  return false;
}
// single-ball tests drive the first ball directly
static Ball& B(Game& g) { return g.balls[0]; }

// ---------------------------------------------------------------- tests
static void testLaunch() {
  printf("== plunger launch ==\n");
  for (float c = 0; c <= 1.001f; c += 0.1f) {
    Game g;
    g.startGame();
    Input in{false, false, true};
    float hold = c * PLUNGE_FILL_S;
    for (float tt = 0; tt < hold; tt += 1 / 60.f) g.frame(1 / 60.f, in);
    in.touch = false;
    float minY = 999, minX = 999;
    bool left = false;
    int drained = 0;
    float lastX = 0;
    uint32_t lanes0 = g.nLane;
    for (int f = 0; f < 60 * 4; f++) {
      g.frame(1 / 60.f, in);
      if (B(g).p.y > 440 && B(g).p.y < 470) lastX = B(g).p.x;
      if (g.events & (EV_DRAIN | EV_SAVE)) { drained = f; break; }
      g.events = 0;
      minY = fminf(minY, B(g).p.y);
      if (B(g).p.x < LANE_X - 4) { left = true; minX = fminf(minX, B(g).p.x); }
    }
    printf("  charge %.1f  peak y %5.1f  left lane %-3s  min x %5.1f  lanes %u  %s%.0f\n",
           c, minY, left ? "yes" : "no", minX, g.nLane - lanes0,
           drained ? "drained <4s at x=" : "", drained ? lastX : 0.0f);
  }
}

// drop a ball onto the left flipper and flip at a range of timings
static void testFlipperShot(HostCanvas& cv) {
  printf("== left flipper shots (drop at x, flip when ball reaches y) ==\n");
  int good = 0, total = 0;
  for (float x = 100; x <= 135; x += 5)
    for (float trig = 395; trig <= 420; trig += 5) {
      Game g;
      g.startGame();
      B(g).p = {x, 300};
      B(g).v = {0, 0};
      g.everLeftLane = true;
      bool fire = false;
      float minY = 999;
      for (int f = 0; f < 60 * 3; f++) {
        if (B(g).p.y > trig) fire = true;
        g.frame(1 / 60.f, {fire, false, false});
        if (fire) minY = fminf(minY, B(g).p.y);
        if (fire && f % 1 == 0 && total % 7 == 0) cv.put((int)B(g).p.x, (int)B(g).p.y, 0xffff00);
        if (!B(g).live || bad(g)) break;
      }
      total++;
      if (minY < 200) good++;
      printf("  x=%3.0f trig=%3.0f  peak y after flip %5.1f\n", x, trig, minY);
    }
  printf("  -> %d / %d shots reached the upper playfield (y<200)\n", good, total);
}

static void soak(HostCanvas& cv, int balls) {
  printf("== soak: %d balls, bot on the flippers ==\n", balls);
  Game g;
  pb::Bot bot;
  long frames = 0, escapes = 0, lives = 0, outL = 0, outR = 0, centre = 0;
  long mbFrames = 0, maxLive = 0;
  double totalLife = 0;
  uint32_t traceColor[4] = {0x00ff00, 0xff8800, 0x00ffff, 0xff00ff};
  int played = 0;
  float lifeT = 0;
  float lastX[MAXBALLS] = {};
  while (played < balls) {
    if (g.state != PLAY) { g.startGame(); g.hiscore = 0; }
    Input in = bot.step(g, 1 / 60.f);
    for (int k = 0; k < MAXBALLS; k++)
      if (g.balls[k].live && g.balls[k].p.y > 440 && g.balls[k].p.y < 470) lastX[k] = g.balls[k].p.x;
    bool live0[MAXBALLS];
    for (int k = 0; k < MAXBALLS; k++) live0[k] = g.balls[k].live;
    g.frame(1 / 60.f, in);
    frames++;
    lifeT += 1 / 60.f;
    if (g.multiball) mbFrames++;
    maxLive = std::max<long>(maxLive, g.liveCount());
    if (played < 4) cv.put((int)B(g).p.x, (int)B(g).p.y, traceColor[played]);
    if (bad(g)) {
      escapes++;
      for (const Ball& b : g.balls)
        if (badBall(b)) printf("  !! bad ball state p=(%.1f,%.1f) v=(%.1f,%.1f)\n", b.p.x, b.p.y, b.v.x, b.v.y);
      g.serveBall();
    }
    // count every ball that leaves the table (multiball extras included)
    for (int k = 0; k < MAXBALLS; k++)
      if (live0[k] && !g.balls[k].live && g.balls[k].p.y > DRAIN_Y) {
        if (lastX[k] < 30) outL++; else if (lastX[k] > 262) outR++; else centre++;
      }
    if (g.events & (EV_DRAIN | EV_SAVE)) {
      played++;
      lives++;
      totalLife += lifeT;
      lifeT = 0;
    }
    g.events = 0;
    g.ndirty = 0;
    if (lifeT > 600) {
      printf("  !! ball alive >10 min, p=(%.1f,%.1f)\n", B(g).p.x, B(g).p.y);
      g.serveBall();
      lifeT = 0;
    }
  }
  printf("  frames %ld (%.1f min sim)  avg ball life %.1fs\n", frames, frames / 3600.0, totalLife / lives);
  printf("  drains: left outlane %ld  centre %ld  right outlane %ld\n", outL, centre, outR);
  printf("  bumper %u  sling %u  target %u  lane %u  nudges %u  kickbacks %u  spins %u  escapes %ld\n",
         g.nBumper, g.nSling, g.nTarget, g.nLane, g.nNudge, g.nKick, g.nSpin, escapes);
  printf("  flipper hits (sound events) %u\n", g.nFlipHit);
  printf("  ramp: entered %u, completed %u, ride avg %.2fs max %.2fs\n", g.nRampIn, g.nRamp,
         g.nRamp ? g.rampRideSum / g.nRamp : 0.0f, g.rampRideMax);
  printf("  multiballs %u  (%.1f%% of play time, max %ld balls live)\n", g.nMulti,
         100.0 * mbFrames / frames, maxLive);
}

// force multiball repeatedly and check it feeds, plays and winds down
static void testMultiball() {
  printf("== multiball ==\n");
  int runs = 40, reached3 = 0, ended = 0, escapes = 0;
  double dur = 0;
  for (int r = 0; r < runs; r++) {
    Game g;
    pb::Bot bot;
    g.startGame();
    B(g).v = {0, -1000};  // launch the first ball
    for (int f = 0; f < 120; f++) g.frame(1 / 60.f, bot.step(g, 1 / 60.f));
    g.startMultiball();
    int maxLive = 0;
    float t = 0;
    bool done = false;
    for (int f = 0; f < 60 * 180 && g.state == PLAY; f++) {
      g.frame(1 / 60.f, bot.step(g, 1 / 60.f));
      t += 1 / 60.f;
      maxLive = std::max(maxLive, g.liveCount());
      if (bad(g)) { escapes++; break; }
      if (!g.multiball) { done = true; break; }
      g.events = 0;
      g.ndirty = 0;
    }
    reached3 += maxLive == MAXBALLS;
    ended += done;
    dur += t;
  }
  printf("  %d runs: reached %d balls in %d, ended cleanly in %d, avg %.1fs, escapes %d\n",
         runs, MAXBALLS, reached3, ended, dur / runs, escapes);
}

static void testDirtyConsistency() {
  // a dirty-rect redraw must match a full redraw pixel for pixel
  printf("== dirty-rect consistency ==\n");
  Game g;
  g.startGame();
  HostCanvas full, inc;
  drawWorld(full, g);
  drawWorld(inc, g);
  int mismatches = 0;
  Input in{false, false, false};
  for (int f = 0; f < 600; f++) {
    in.touch = f < 50;
    in.left = (f / 37) % 2;
    in.right = (f / 23) % 2;
    g.ndirty = 0;
    if (f == 120) g.startMultiball();  // as main.cpp's serial 'm' does
    g.frame(1 / 60.f, in);
    for (int i = 0; i < g.ndirty; i++) {
      Rect r = g.dirty[i];
      inc.ox = r.x; inc.oy = r.y; inc.w = r.w; inc.h = r.h;
      drawWorld(inc, g);
    }
    full = HostCanvas();
    drawWorld(full, g);
    if (g.fullRedraw && f > 0) { inc = full; g.fullRedraw = false; continue; }
    g.fullRedraw = false;
    int diff = 0;
    for (int k = 0; k < SCR_W * SCR_H; k++) diff += full.px[k] != inc.px[k];
    if (diff) {
      int mx = 0, my = 0;
      for (int k = 0; k < SCR_W * SCR_H; k++)
        if (full.px[k] != inc.px[k]) { mx = k % SCR_W; my = k / SCR_W; }
      if (mismatches < 5)
        printf("  frame %d: %d px differ, e.g. (%d,%d), ball (%.0f,%.0f)\n", f, diff, mx, my, B(g).p.x, B(g).p.y);
      mismatches++;
      inc = full;
    }
  }
  printf("  %d / 600 frames mismatched\n", mismatches);
}

// demo: idles into a bot game, which never sets the high score and returns
// to the title; a tap mid-demo starts a real game
static void testDemo() {
  printf("== attract demo ==\n");
  Game g;
  pb::Bot bot;
  g.hiscore = 1;
  int started = 0, returned = 0, frames = 0;
  for (int f = 0; f < 60 * 60 * 10; f++) {  // 10 minutes
    if (g.demoDue()) { g.startDemo(); started++; }
    bool wasDemo = g.demo;
    g.frame(1 / 60.f, g.demo ? bot.step(g, 1 / 60.f) : Input{false, false, false});
    if (wasDemo && !g.demo && g.state == ATTRACT) returned++;
    g.events = 0;
    g.ndirty = 0;
    frames++;
  }
  printf("  10 min idle: %d demos started, %d returned to title, hiscore %u (should be 1)\n",
         started, returned, g.hiscore);
  // tap during a demo
  if (!g.demo) g.startDemo();
  for (int f = 0; f < 300; f++) g.frame(1 / 60.f, bot.step(g, 1 / 60.f));
  g.tap();
  printf("  tap mid-demo: state %s, demo %d, score %u, ball %d\n",
         g.state == PLAY ? "PLAY" : "other", g.demo, g.score, g.ball);
}

// bonus: drain with known counts and check the count-up adds exactly
// (counts x values) x multiplier, then serves the next ball; and that a
// press fast-forwards it
static void testBonus() {
  printf("== end-of-ball bonus ==\n");
  for (int fast = 0; fast < 2; fast++) {
    Game g;
    g.startGame();
    B(g).p = {148, 300};
    B(g).v = {0, 0};
    g.everLeftLane = true;
    g.saveT = 0;
    g.ballRamps = 2; g.ballLanes = 3; g.ballTargets = 1; g.ballSpins = 10;
    g.mult = 3;
    uint32_t expect = (2 * BONUS_RAMP + 3 * BONUS_LANE + 1 * BONUS_TARGET + 10 * BONUS_SPIN) * 3;
    // let it fall straight down the middle and drain
    int f = 0;
    while (!g.bonusActive && f++ < 600) { g.frame(1 / 60.f, {false, false, false}); g.ndirty = 0; }
    uint32_t s0 = g.score;
    float t = 0;
    while (g.bonusActive && t < 30) {
      g.frame(1 / 60.f, {fast && t > 0.7f, false, false});
      g.ndirty = 0;
      t += 1 / 60.f;
    }
    printf("  %s: bonus %u (expected %u) in %.1fs, now ball %d, live %d, mult %d\n",
           fast ? "hurried" : "normal ", g.score - s0, expect, t, g.ball, g.liveCount(), g.mult);
  }
}

// rolling: drop a ball gently onto the lowered left flipper near the pivot
// and onto the left inlane guide, and time how long it takes to roll off
static void testRoll() {
  printf("== rolling speed ==\n");
  struct Case { const char* what; V start; } cases[] = {
      {"left flipper, from the pivot", {FLIP_PIV_L.x + 4, FLIP_PIV_L.y - 16}},
      {"left inlane guide, from the top", {36, 372}},
  };
  for (auto& c : cases) {
    Game g;
    g.startGame();
    B(g).p = c.start;
    B(g).v = {0, 0};
    g.everLeftLane = true;
    float t = 0, maxV = 0, sumV = 0;
    int n = 0;
    while (t < 6 && B(g).live && B(g).p.y < 445) {
      g.frame(1 / 60.f, {false, false, false});
      g.ndirty = 0;
      t += 1 / 60.f;
      float v = len(B(g).v);
      maxV = fmaxf(maxV, v);
      sumV += v; n++;
    }
    printf("  %-34s %.2fs to leave, avg %.0f px/s, max %.0f px/s\n", c.what, t, sumV / n, maxV);
  }
}

int main() {
  HostCanvas cv;
  Game g;
  drawWorld(cv, g);  // attract screen
  cv.save("attract.ppm");
  g.startGame();
  g.msg[0] = 0;
  cv = HostCanvas();
  drawWorld(cv, g);
  testLaunch();
  testFlipperShot(cv);
  soak(cv, 600);
  testMultiball();
  testBonus();
  testRoll();
  testDemo();
  testDirtyConsistency();
  cv.save("table.ppm");
  return 0;
}
