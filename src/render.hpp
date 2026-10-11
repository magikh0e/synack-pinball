// SYN/ACK pinball: draws the whole world into a "G" canvas.
//
// G is a tiny drawing interface (see DevCanvas in main.cpp and HostCanvas
// in host/sim.cpp). It is always positioned over some rectangle of the
// screen; drawWorld() paints everything that intersects it. Partial redraws
// of dirty rectangles and full-screen band redraws both go through here, so
// a region always looks identical however it was painted.

#pragma once
#include "game.hpp"

namespace pb {

// palette (RGB888) - synthwave terminal, shares Sprout's phosphor + amber
namespace col {
constexpr uint32_t BG = 0x0a0e14, HUD = 0x05080c, GRID = 0x111b27;
constexpr uint32_t WALL_GLOW = 0x0c3446, WALL = 0x3ad7ff;
constexpr uint32_t GREEN = 0x39ff6a, GREEN_DIM = 0x17432a;
constexpr uint32_t AMBER = 0xffb454, AMBER_DIM = 0x4a3518;
constexpr uint32_t MAG = 0xff3df0, MAG_DIM = 0x3a0f3a, MAG_DARK = 0x7a1474;
constexpr uint32_t WHITE = 0xf2f6ff, BALL = 0xc4ccd8, TEXT_DIM = 0x2a3a4c;
constexpr uint32_t PANEL = 0x0d1320, MUTED = 0x4a6a8c;
constexpr uint32_t RAIL = 0x9ab0c6, RAIL_GLOW = 0x1b2838, RAIL_TIE = 0x243246, SHADOW = 0x04060a;
constexpr uint32_t AMBER_MID = 0x8a6230, GREEN_MID = 0x2f8a4c, APRON = 0x0c111b;
}  // namespace col

enum Datum : uint8_t { D_TL, D_TC, D_TR, D_ML, D_MC, D_MR };
enum Font : uint8_t { F_SMALL, F_HUD, F_TITLE, F_BIG };

inline void fmtScore(char* out, size_t n, uint32_t v) {
  char tmp[16];
  int len = snprintf(tmp, sizeof tmp, "%lu", (unsigned long)v);
  size_t o = 0;
  for (int i = 0; i < len && o + 1 < n; i++) {
    out[o++] = tmp[i];
    int rem = len - 1 - i;
    if (rem > 0 && rem % 3 == 0 && o + 1 < n) out[o++] = ',';
  }
  out[o] = 0;
}

template <class G>
void drawSeg(G& g, V a, V b, float r, uint32_t glow, uint32_t core) {
  float pad = r + 3;
  if (!g.vis(fminf(a.x, b.x) - pad, fminf(a.y, b.y) - pad,
             fmaxf(a.x, b.x) + pad, fmaxf(a.y, b.y) + pad)) return;
  g.wide(a.x, a.y, b.x, b.y, r + 1.5f, glow);
  g.wide(a.x, a.y, b.x, b.y, r > 2.5f ? r - 1.0f : 1.0f, core);
}

template <class G>
void drawWorld(G& g, const Game& s) {
  using namespace col;
  g.clear(BG);

  // grid
  for (int x = 16; x < SCR_W; x += 32)
    if (g.vis(x, HUD_H, x + 1, SCR_H)) g.vline(x, HUD_H, SCR_H - HUD_H, GRID);
  for (int y = HUD_H + 18; y < SCR_H; y += 32)
    if (g.vis(0, y, SCR_W, y + 1)) g.hline(0, y, SCR_W, GRID);

  // dim logo / message
  if (g.vis(MSG_X, MSG_Y, MSG_X + MSG_W, MSG_Y + MSG_H)) {
    int cx = MSG_X + MSG_W / 2, cy = MSG_Y + MSG_H / 2;
    if (s.msg[0]) g.textFit(s.msg, cx, cy, F_TITLE, MSG_W - 4, AMBER);
    else g.text("SYN/ACK", cx, cy, D_MC, F_TITLE, TEXT_DIM);
  }

  // lane lamps under the top rollovers: lit = solid amber, unlit = ring
  static const char* laneTxt[3] = {"S", "Y", "N"};
  for (int i = 0; i < 3; i++) {
    float cx = laneCx(i);
    if (!g.vis(cx - 12, 96, cx + 12, 118)) continue;
    bool lit = s.lane[i];
    g.disc(cx, 107, 8, lit ? AMBER : AMBER_DIM);
    if (!lit) g.disc(cx, 107, 6, BG);
    g.text(laneTxt[i], cx, 108, D_MC, F_SMALL, lit ? BG : AMBER_MID);
  }

  // spinner lane arrow lamp (magenta while the spinner is worth 2x)
  if (g.vis(SPIN_X0, SPIN_Y + 6, SPIN_X1, SPIN_Y + 24)) {
    float cx = (OUTER_L + SPIN_GUIDE_X) * 0.5f, y = SPIN_Y + 9;
    g.tri(cx, y, cx - 6, y + 10, cx + 6, y + 10, s.synArmed ? MAG : AMBER_DIM);
  }

  // apron: SHOOT AGAIN lamp and the multiplier ladder
  if (g.vis(OUTER_L, APRON_Y - 2, LANE_X, SCR_H)) {
    for (int side = 0; side < 2; side++) {
      int x0 = side ? APRON_R0 : APRON_L0, x1 = side ? APRON_R1 : APRON_L1;
      g.rect(x0, APRON_Y, x1 - x0, SCR_H - APRON_Y, APRON);
      g.hline(x0, APRON_Y, x1 - x0, WALL_GLOW);
    }
    int cy = APRON_Y + (SCR_H - APRON_Y) / 2;
    bool sa = s.saveLit && s.state == PLAY;
    if (sa) g.rect(APRON_L0 + 1, cy - 7, APRON_L1 - APRON_L0 - 2, 15, AMBER);
    g.text("SHOOT AGAIN", (APRON_L0 + APRON_L1) / 2, cy + 1, D_MC, F_SMALL, sa ? BG : AMBER_DIM);
    static const char* mt[4] = {"2x", "3x", "4x", "5x"};
    int w = (APRON_R1 - APRON_R0 - 8) / 4;
    for (int k = 0; k < 4; k++) {
      int x = APRON_R0 + 4 + k * w;
      bool on = s.state == PLAY && s.mult >= k + 2;
      if (on) g.rect(x + 1, cy - 7, w - 2, 15, AMBER);
      g.text(mt[k], x + w / 2, cy + 1, D_MC, F_SMALL, on ? BG : AMBER_DIM);
    }
  }

  // kickback indicator: chevron in the left outlane
  if (g.vis(OUTER_L + 2, KICK_Y - 34, KICK_X, KICK_Y - 8)) {
    float cx = (OUTER_L + KICK_X) * 0.5f;
    uint32_t c = s.kickback ? GREEN : GREEN_DIM;
    for (int k = 0; k < 2; k++) {
      float y = KICK_Y - 28 + k * 9;
      g.tri(cx, y, cx - 5, y + 6, cx + 5, y + 6, c);
    }
  }

  // slingshots: dark body, magenta kick face
  for (int side = 0; side < 2; side++) {
    const V* t = side ? s.slingR : s.slingL;
    float x0 = fminf(t[0].x, t[2].x) - 5, x1 = fmaxf(t[0].x, t[2].x) + 5;
    if (!g.vis(x0, t[0].y - 5, x1, t[2].y + 5)) continue;
    g.tri(t[0].x, t[0].y, t[1].x, t[1].y, t[2].x, t[2].y, MAG_DIM);
  }

  // walls (everything that isn't drawn specially)
  for (int i = 0; i < s.nseg; i++) {
    const Seg& sg = s.seg[i];
    switch (sg.kind) {
      case K_WALL: drawSeg(g, sg.a, sg.b, sg.r, WALL_GLOW, WALL); break;
      case K_POST: drawSeg(g, sg.a, sg.b, sg.r, WALL_GLOW, WALL); break;
      case K_GATE: drawSeg(g, sg.a, sg.b, 1, AMBER_DIM, AMBER); break;
      case K_SLING: {
        bool lit = s.slingFlash[sg.id] > 0;
        drawSeg(g, sg.a, sg.b, lit ? 3 : 2, MAG_DARK, lit ? WHITE : MAG);
        break;
      }
      case K_TARGET: {
        int id = sg.id;
        float y0 = bankTy(id) + 3, y1 = bankTy(id + 1) - 3;
        if (!g.vis(BANK_X - 22, bankTy(id) - 6, LANE_X, bankTy(id + 1) + 6)) break;
        bool flash = s.targetFlash[id] > 0, lit = s.target[id];
        uint32_t c = flash ? WHITE : (lit ? GREEN : GREEN_DIM);
        drawSeg(g, sg.a, sg.b, 2, WALL_GLOW, WALL);
        if (lit || flash) g.rect(BANK_X - 8, y0 - 1, 9, y1 - y0 + 2, GREEN_DIM);  // glow
        g.rect(BANK_X - 6, y0, 6, y1 - y0, c);
        static const char* tTxt[3] = {"A", "C", "K"};
        g.text(tTxt[id], BANK_X - 15, (y0 + y1) / 2, D_MC, F_SMALL, lit || flash ? GREEN : GREEN_MID);
        break;
      }
    }
  }

  // pop bumpers
  for (int i = 0; i < NBUMP; i++) {
    V c = BUMP_C[i];
    if (!g.vis(c.x - BUMP_R - 3, c.y - BUMP_R - 3, c.x + BUMP_R + 3, c.y + BUMP_R + 3)) continue;
    bool lit = s.bumpFlash[i] > 0;
    g.disc(c.x, c.y, BUMP_R + 2, lit ? MAG : WALL_GLOW);
    g.disc(c.x, c.y, BUMP_R - 1, lit ? WHITE : WALL);
    g.disc(c.x, c.y, BUMP_R - 4, lit ? MAG : BG);
    g.disc(c.x, c.y, 6, lit ? WHITE : (s.synArmed ? AMBER : WALL));
  }

  // plunger + power meter
  if (g.vis(LANE_X + 2, 230, OUTER_R, SCR_H)) {
    int pull = (int)(s.charge * 7);
    g.rect(BALL_HOME_X - 6, PLUNGER_Y + 1 + pull, 13, 3, AMBER);
    g.rect(BALL_HOME_X - 1, PLUNGER_Y + 4 + pull, 3, SCR_H - PLUNGER_Y, AMBER_DIM);
    if (s.charging && s.charge > 0) {
      int h = (int)(s.charge * 190);
      g.rect(BALL_HOME_X - 1, BALL_HOME_Y - 10 - h, 3, h, s.charge > 0.85f ? MAG : AMBER);
    }
  }

  // flippers
  const Flipper* fs[2] = {&s.fl, &s.fr};
  for (const Flipper* f : fs) {
    Rect b = s.flipperBox(*f);
    if (!g.vis(b.x, b.y, b.x + b.w, b.y + b.h)) continue;
    V tp = f->tip();
    g.wedge(f->piv.x, f->piv.y, tp.x, tp.y, FLIP_RB, FLIP_RT, MAG);
    g.wedge(f->piv.x, f->piv.y, tp.x, tp.y, FLIP_RB - 2.5f, FLIP_RT - 1.5f, MAG_DARK);
    g.disc(f->piv.x, f->piv.y, 2, WHITE);
  }

  // ramp entry arrow, in front of the mouth (magenta = jackpot lit)
  {
    V c = s.rampArrowAt(), d = s.rampDir, n = perp(d);
    if (g.vis(c.x - 9, c.y - 9, c.x + 9, c.y + 9)) {
      uint32_t col = s.multiball ? MAG : AMBER;
      V tip = c + d * 7, b0 = c - d * 4 + n * 6, b1 = c - d * 4 - n * 6;
      g.tri(tip.x, tip.y, b0.x, b0.y, b1.x, b1.y, col);
    }
  }

  // balls on the playfield (ramp balls are drawn later, above the rails)
  for (const Ball& b : s.balls) {
    if (!b.live || b.onRamp || !g.vis(b.p.x - BALL_R - 1, b.p.y - BALL_R - 1, b.p.x + BALL_R + 3, b.p.y + BALL_R + 3))
      continue;
    g.disc(b.p.x + 1, b.p.y + 2, BALL_R, SHADOW);
    g.disc(b.p.x, b.p.y, BALL_R, BALL);
    g.disc(b.p.x - 2, b.p.y - 2, 2, WHITE);
  }

  // spinner: a flat plate seen from above, foreshortened as it turns,
  // drawn over the ball (the ball runs underneath it)
  if (g.vis(SPIN_X0, SPIN_Y - 8, SPIN_X1, SPIN_Y + 8)) {
    float c = cosf(s.spinAng);
    int hh = (int)(fabsf(c) * 4 + 0.5f);
    bool face = c > 0;  // which side of the plate is up
    uint32_t hi = s.synArmed ? MAG : AMBER, lo = s.synArmed ? MAG_DARK : AMBER_DIM;  // magenta = 2x
    g.hline(SPIN_X0 + 2, SPIN_Y, SPIN_X1 - SPIN_X0 - 4, MUTED);  // axle
    g.rect(SPIN_X0 + 6, SPIN_Y - hh, SPIN_X1 - SPIN_X0 - 12, hh * 2 + 1,
           face ? hi : lo);
    if (hh >= 2) g.hline(SPIN_X0 + 8, SPIN_Y, SPIN_X1 - SPIN_X0 - 16, face ? lo : hi);
    g.disc(SPIN_X0 + 3, SPIN_Y, 2, WALL);
    g.disc(SPIN_X1 - 3, SPIN_Y, 2, WALL);
  }

  // UPLINK ramp: sparse, dim cross-ties so it reads as a track
  for (int i = 0; i + 1 < RAMP_N; i++) {
    V l0 = s.rampL[i], l1 = s.rampL[i + 1], r0 = s.rampR[i], r1 = s.rampR[i + 1];
    float x0 = fminf(fminf(l0.x, l1.x), fminf(r0.x, r1.x)), x1 = fmaxf(fmaxf(l0.x, l1.x), fmaxf(r0.x, r1.x));
    float y0 = fminf(fminf(l0.y, l1.y), fminf(r0.y, r1.y)), y1 = fmaxf(fmaxf(l0.y, l1.y), fmaxf(r0.y, r1.y));
    if (!g.vis(x0 - 2, y0 - 2, x1 + 2, y1 + 2)) continue;
    int n = (int)(len(RAMP_PATH[i + 1] - RAMP_PATH[i]) / 32) + 1;
    for (int k = 0; k < n; k++) {
      float t = (k + 0.5f) / n;
      V a = l0 + (l1 - l0) * t, c = r0 + (r1 - r0) * t;
      g.wide(a.x, a.y, c.x, c.y, 0.6f, RAIL_TIE);
    }
  }
  // UPLINK ramp: wire rails over everything on the playfield
  for (int i = 0; i + 1 < RAMP_N; i++) {
    const V* rails[2] = {s.rampL, s.rampR};
    for (const V* r : rails) {
      V a = r[i], c = r[i + 1];
      if (!g.vis(fminf(a.x, c.x) - 3, fminf(a.y, c.y) - 3, fmaxf(a.x, c.x) + 3, fmaxf(a.y, c.y) + 3))
        continue;
      g.wide(a.x, a.y, c.x, c.y, 2.0f, RAIL_GLOW);
      g.wide(a.x, a.y, c.x, c.y, 0.8f, RAIL);
    }
  }
  // balls riding the ramp, with a drop shadow for height
  for (const Ball& b : s.balls) {
    if (!b.live || !b.onRamp || !g.vis(b.p.x - BALL_R - 1, b.p.y - BALL_R - 1, b.p.x + BALL_R + 5, b.p.y + BALL_R + 5))
      continue;
    g.disc(b.p.x + 3, b.p.y + 4, BALL_R, SHADOW);
    g.disc(b.p.x, b.p.y, BALL_R, BALL);
    g.disc(b.p.x - 2, b.p.y - 2, 2, WHITE);
  }

  // HUD strip
  if (g.vis(0, 0, SCR_W, HUD_H)) {
    g.rect(0, 0, SCR_W, HUD_H, HUD);
    g.hline(0, HUD_H - 1, SCR_W, WALL_GLOW);
    char b[24];
    fmtScore(b, sizeof b, s.score);
    g.text(b, 8, HUD_H / 2, D_ML, F_HUD, GREEN);
    if (s.demo) {
      g.text("DEMO", 196, HUD_H / 2, D_MC, F_SMALL, MAG);
      g.text("TAP TO PLAY", SCR_W - 8, HUD_H / 2, D_MR, F_SMALL, AMBER);
    } else if (s.state == PLAY) {
      for (int i = 0; i < BALLS_PER_GAME; i++)
        g.disc(SCR_W - 14 - i * 13, HUD_H / 2, 4, i < BALLS_PER_GAME - s.ball + 1 ? BALL : TEXT_DIM);
      if (s.multiball) g.text("FLOOD 2x", 196, HUD_H / 2, D_MC, F_SMALL, MAG);  // (not in demo)
    } else {
      char h[20];
      fmtScore(h, sizeof h, s.hiscore);
      snprintf(b, sizeof b, "HI %s", h);
      g.text(b, SCR_W - 8, HUD_H / 2, D_MR, F_SMALL, AMBER);
    }
  }

  // attract / game-over panel
  if (s.state != PLAY) {
    const int px = PANEL_X, py = PANEL_Y, pw = PANEL_W, ph = PANEL_H;
    if (g.vis(px, py, px + pw, py + ph)) {
      g.rect(px, py, pw, ph, PANEL);
      g.rect(px, py, pw, 2, MAG);
      g.rect(px, py + ph - 2, pw, 2, MAG);
      int cx = px + pw / 2;
      if (s.state == ATTRACT) {
        g.text("SYN/ACK", cx, py + 38, D_MC, F_BIG, GREEN);
        g.text("p i n b a l l", cx, py + 70, D_MC, F_SMALL, WALL);
        g.text("SYN lanes > ACK targets", cx, py + 100, D_MC, F_SMALL, MUTED);
        g.text("= HANDSHAKE jackpot", cx, py + 118, D_MC, F_SMALL, MUTED);
      } else {
        g.text("GAME OVER", cx, py + 38, D_MC, F_TITLE, MAG);
        char b[24];
        fmtScore(b, sizeof b, s.score);
        g.text(b, cx, py + 76, D_MC, F_BIG, GREEN);
        if (s.score > 0 && s.score >= s.hiscore) g.text("NEW HIGH SCORE", cx, py + 108, D_MC, F_SMALL, AMBER);
      }
      if ((s.state == ATTRACT || s.stateT > 1.2f))
        g.text("TAP TO START", cx, py + ph - 24, D_MC, F_SMALL, AMBER);
    }
  }
}

}  // namespace pb
