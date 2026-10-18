// SYN/ACK pinball: table geometry, ball physics and rules.
//
// Pure C++ (no Arduino, no LovyanGFX), so the same file runs on the ESP32
// and in the host test harness (host/sim.cpp).
//
// Coordinates are screen pixels, portrait 320x480, y grows downward.
// Physics runs in fixed substeps of 1/600 s. The ball is a circle; every
// collider is a capsule (segment + radius) or a circle (bumper), so one
// closest-point routine covers walls, posts, slings, targets and flippers.

#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace pb {

// ---------------------------------------------------------------- vectors
struct V { float x, y; };
inline V operator+(V a, V b) { return {a.x + b.x, a.y + b.y}; }
inline V operator-(V a, V b) { return {a.x - b.x, a.y - b.y}; }
inline V operator*(V a, float s) { return {a.x * s, a.y * s}; }
inline float dot(V a, V b) { return a.x * b.x + a.y * b.y; }
inline float len(V a) { return sqrtf(dot(a, a)); }
inline V perp(V a) { return {-a.y, a.x}; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Xtensa GCC 8.4 hits an internal compiler error ("insn does not satisfy its
// constraints") when the bonus/blink code is inlined into Game::frame().
// Keeping those helpers out of line avoids it.
#define PB_NOINLINE __attribute__((noinline))

// ---------------------------------------------------------------- tuning
constexpr int   SCR_W = 320, SCR_H = 480;
constexpr int   HUD_H = 30;          // score strip at the top
constexpr float BALL_R = 6.0f;
constexpr float GRAVITY = 680.0f;    // px/s^2 down the (virtually tilted) table
constexpr float MAX_V = 1500.0f;
constexpr float SUB_DT = 1.0f / 600.0f;
constexpr float E_WALL = 0.45f;      // restitution
constexpr float E_FLIP = 0.30f;
constexpr float BUMPER_KICK = 430.0f;
constexpr float SLING_KICK = 330.0f;
constexpr float FLIP_UP_W = 29.0f;   // rad/s (was 24; bumped after first play)
constexpr float FLIP_DN_W = 14.0f;
constexpr float PLUNGE_MIN = 480.0f, PLUNGE_MAX = 1250.0f, PLUNGE_FILL_S = 1.1f;
constexpr float BALL_SAVE_S = 6.0f;
constexpr int   BALLS_PER_GAME = 3;
constexpr int   MAXBALLS = 3;         // on the table at once (multiball)
// end-of-ball bonus, per item collected on that ball (then x the multiplier)
constexpr uint32_t BONUS_RAMP = 1000, BONUS_LANE = 250, BONUS_TARGET = 250,
                   BONUS_SPIN = 25;     // per spinner half turn
constexpr float BONUS_STEP_S = 0.55f;   // per bonus line (fast-forward: 0.08)
constexpr float BLINK_HZ = 3.0f;        // lamps that say "shoot here"
constexpr float SAVE_WARN_S = 2.0f;     // SHOOT AGAIN blinks for its last 2 s
// skill shot: release the plunger with the power inside a zone on the meter
// (the zone moves each ball). Launches can't aim at the lanes on this table -
// strong ones all orbit to the spinner - so the skill is in the timing.
constexpr uint32_t SKILL_PTS = 5000;
constexpr float SKILL_W = 0.12f;        // zone width, as a fraction of full power
// attract light show: lamps chase around the table between games
constexpr float SHOW_HZ = 8.0f;
constexpr int SHOW_LAMPS = 10;          // S Y N, spinner arrow, kickback, SHOOT AGAIN, 2x-5x
constexpr float DEMO_IDLE_S = 15.0f;  // idle on title / game over before the demo
constexpr float DEMO_MAX_S = 90.0f;   // longest demo before returning to the title
constexpr float MB_SAVE_S = 10.0f;    // multiball ball save
constexpr float AUTO_PLUNGE_V = 1050; // multiball auto-launch, rides the orbit

// ---------------------------------------------------------------- table layout
// Playfield: arc top centred at (160,190) r=152, outer walls x=8 / x=312,
// plunger lane x=288..312 on the right. Lower half is mirrored about x=148
// (centre of the playfield proper, excluding the lane).
constexpr float ARC_CX = 160, ARC_CY = 190, ARC_R = 152;
constexpr float LANE_X = 288, OUTER_R = 312, OUTER_L = 8;
constexpr float MIRROR = 296;        // x' = MIRROR - x
constexpr float PLUNGER_Y = 470;     // lane floor; ball rests at y=462
constexpr float BALL_HOME_X = 300, BALL_HOME_Y = PLUNGER_Y - 2 - BALL_R;
constexpr float DRAIN_Y = SCR_H + 12;

// top rollover lanes S Y N, between 4 posts
constexpr float LANE_POST_X[4] = {106, 136, 166, 196};
constexpr float LANE_POST_Y0 = 72, LANE_POST_Y1 = 94;
constexpr float LANE_SENSE_Y = 84;
inline float laneCx(int i) { return (LANE_POST_X[i] + LANE_POST_X[i + 1]) * 0.5f; }

// pop bumpers
constexpr int NBUMP = 3;
constexpr float BUMP_R = 17;
constexpr V BUMP_C[NBUMP] = {{116, 152}, {186, 152}, {151, 206}};

// right-side bank: A C K standup targets on a vertical face at x=266
constexpr float BANK_X = 266, BANK_Y0 = 240, BANK_Y1 = 312;
inline float bankTy(int i) { return BANK_Y0 + (BANK_Y1 - BANK_Y0) * i / 3.0f; }

// flippers
constexpr V     FLIP_PIV_L = {94, 420};
constexpr V     FLIP_PIV_R = {MIRROR - 94, 420};
constexpr float FLIP_LEN = 46, FLIP_RB = 7, FLIP_RT = 3.5f;
constexpr float FLIP_REST = 0.49f, FLIP_UP = -0.45f;  // left flipper, radians

// outlane/inlane separator: x of the left one (mirrored for the right) and
// the y where it starts (per side: a shorter left guide sends fewer balls
// down the left outlane, which the orbit return otherwise feeds). Outlane clearance = SEP_X - OUTER_L - 4 (wall radii)
// against a 12 px ball.
#ifndef SEP_X
#define SEP_X 25.0f
#endif
#ifndef SEP_TOP_L
#define SEP_TOP_L 375.0f
#endif
#ifndef SEP_TOP_R
#define SEP_TOP_R 340.0f
#endif

// spinner in the left orbit lane (between the outer wall and a guide at
// SPIN_GUIDE_X). Passing through spins it; every half turn scores.
constexpr float SPIN_Y = 205, SPIN_X0 = 10, SPIN_X1 = 44, SPIN_GUIDE_X = 46;
constexpr float SPIN_PER_PX = 0.06f;  // rad/s of spin per px/s of ball speed
constexpr uint32_t SPIN_PTS = 100;    // per half turn

// UPLINK ramp: a see-through wireform. The mouth sits right of centre facing
// the left flipper; the ramp climbs the right side, crosses over the top
// lanes and drops down the left into the left inlane. While on it a ball is
// on its own layer: no playfield collisions, it just follows the path.
constexpr int RAMP_N = 15;
constexpr V RAMP_PATH[RAMP_N] = {
    {226, 230}, {250, 190}, {264, 145}, {258, 100}, {232, 72}, {192, 56}, {148, 52}, {104, 58},
    {76, 82},   {64, 120},  {62, 170},  {62, 225},  {54, 266}, {38, 292}, {37, 321}};
// (the last stretch points straight down: angled left, the drop carried the
// ball over the separator into the left outlane)
constexpr int RAMP_TOP_IDX = 3;        // climbs up to this point
constexpr int RAMP_DOWN_IDX = 8;       // descends from this point
constexpr float RAMP_UP_DEC = 1100;    // px/s^2 lost climbing
constexpr float RAMP_DRAG = 40;        // px/s^2 lost on the flat
constexpr float RAMP_DOWN_ACC = 900;   // px/s^2 gained descending
constexpr float RAMP_MIN_V = 300;      // past the crest it never crawls slower
// (was 80 / 350 / 80: a shot that only just crested took ~3.5 s to come
// down, which read as the game slowing down)
constexpr float RAMP_EXIT_MAX = 650;   // speed cap dropping into the inlane
constexpr float RAMP_MOUTH_HALF = 14;  // mouth post offset from the centreline
constexpr float RAMP_HALF = 8;         // rail offset from the centreline
constexpr uint32_t RAMP_SCORE = 2500, RAMP_JACKPOT = 15000;

// left outlane kickback trigger zone and its indicator
constexpr float KICK_X = 25, KICK_Y = 425;
// the kick: straight up the middle of the outlane, then hooked right at
// KICK_HOOK_Y, below the left orbit's exit wall (which a straight kick hits
// from underneath, dropping the ball back into the outlane)
constexpr float STILL_R = 8;  // stuck watchdog: a ball staying this close is "still"
constexpr float KICK_LANE_X = (OUTER_L + SEP_X) / 2, KICK_V = 1000;
constexpr float KICK_HOOK_Y = 330, KICK_OUT_VX = 500, KICK_OUT_VY = 700;

// apron under the flippers: SHOOT AGAIN lamp (left), multiplier ladder (right)
constexpr int APRON_Y = 458;
constexpr int APRON_L0 = 28, APRON_L1 = 118, APRON_R0 = 178, APRON_R1 = 268;

// message panel (also where the dim logo sits) - kept clear of all geometry
constexpr int MSG_X = 64, MSG_Y = 250, MSG_W = 192, MSG_H = 40;  // right of the ramp
// attract / game-over panel
constexpr int PANEL_X = 44, PANEL_Y = 150, PANEL_W = 232, PANEL_H = 170;

// ---------------------------------------------------------------- types
enum Kind : uint8_t { K_WALL, K_SLING, K_GATE, K_TARGET, K_POST };

struct Seg {
  V a, b;
  float r;
  Kind kind;
  uint8_t id;
  V n;  // gate: the side that blocks; sling: kick direction
};

struct Rect {
  int16_t x, y, w, h;
};

struct Flipper {
  V piv;
  float rest, up;  // absolute angles
  float ang, w;    // current angle, angular velocity
  bool held;
  V tip() const { return piv + V{cosf(ang), sinf(ang)} * FLIP_LEN; }
};

struct Ball {
  V p{BALL_HOME_X, BALL_HOME_Y}, v{0, 0};
  bool live = false;
  bool inLaneBand[3] = {};  // per-ball rollover edge detection
  float stillT = 0;         // stuck watchdog
  bool onRamp = false;      // riding the UPLINK ramp (its own layer)
  float rs = 0, ru = 0;     // ramp: distance along the path, speed along it
  float rideT = 0;          // time on the ramp this ride
  bool kick = false;        // fired by the kickback, not yet hooked into play
  V anchor{BALL_HOME_X, BALL_HOME_Y};  // stuck watchdog: where it's been sitting
};

struct Input {
  bool left, right;  // flipper buttons
  bool touch;        // any touch (plunger)
};

enum State : uint8_t { ATTRACT, PLAY, GAMEOVER };

// events raised for sound / LED; cleared by the caller each frame
enum : uint32_t {
  EV_BUMPER = 1 << 0, EV_SLING = 1 << 1, EV_LANE = 1 << 2, EV_TARGET = 1 << 3,
  EV_SYN = 1 << 4, EV_ACK = 1 << 5, EV_JACKPOT = 1 << 6, EV_DRAIN = 1 << 7,
  EV_SAVE = 1 << 8, EV_LAUNCH = 1 << 9, EV_FLIP = 1 << 10, EV_GAMEOVER = 1 << 11,
  EV_START = 1 << 12, EV_NUDGE = 1 << 13, EV_KICKBACK = 1 << 14,
  EV_SPIN = 1 << 15, EV_MULTIBALL = 1 << 16, EV_RAMP = 1 << 17,
  EV_BONUS_LINE = 1 << 18, EV_BONUS_TICK = 1 << 19, EV_FLIP_HIT = 1 << 20,
  EV_SKILL = 1 << 21, EV_EXTRA = 1 << 22,
};

// ---------------------------------------------------------------- game
class Game {
 public:
  // geometry (read by the renderer)
  static constexpr int MAXSEG = 96;
  Seg seg[MAXSEG];
  int nseg = 0;
  int arcFirst = 0, arcCount = 0;     // arc segments, drawn as one curve
  V slingL[3], slingR[3];             // triangle vertices (kick face = [0]-[2])

  // dynamic state
  State state = ATTRACT;
  Ball balls[MAXBALLS];
  float rampLen[RAMP_N];              // cumulative length at each path point
  V rampL[RAMP_N], rampR[RAMP_N];     // rails
  V rampDir;                          // into the ramp at the mouth
  bool multiball = false;             // SYN FLOOD running: scoring doubled
  bool demo = false;                  // attract-mode demo: the bot is playing
  bool saveLit = false;               // SHOOT AGAIN lamp (a ball save is running)
  bool blinkOn = false;               // shared phase for every blinking lamp
  // what this ball collected, for the end-of-ball bonus
  uint32_t ballRamps = 0, ballLanes = 0, ballTargets = 0, ballSpins = 0;
  // end-of-ball bonus count-up: it runs between a drain and the next ball
  bool bonusActive = false, bonusFast = false;
  Input prevIn{false, false, false};  // last frame's input, for press edges
  // false from a serve until a touch starts (or the held one is let go), so
  // a touch held over from before, through a drain and the bonus, can't
  // charge the new ball
  bool touchReady = false;
  int bonusStep = 0;
  float bonusT = 0;
  uint32_t bonusTotal = 0, bonusShown = 0;
  bool bonusThenOver = false;
  // a short note in the top bar (volume changes and the like)
  char hudNote[16] = "";
  float hudNoteT = 0;
  int pendingAdd = 0;                 // balls waiting to be auto-plunged
  float addT = 0;                     // delay before the next auto-plunge
  float mbSaveT = 0;                  // multiball ball-save time remaining
  Flipper fl, fr;
  bool lane[3] = {};
  bool target[3] = {};
  bool synArmed = false;              // SYN complete, waiting for ACK
  bool kickback = true;               // left outlane kickback lit
  float spinAng = 0, spinW = 0;       // spinner plate angle, rad/s
  float bumpFlash[NBUMP] = {};        // seconds remaining
  float slingFlash[2] = {};
  float targetFlash[3] = {};
  uint32_t score = 0, hiscore = 0;
  int ball = 0, mult = 1;
  float charge = 0;                   // plunger 0..1
  bool charging = false;
  float saveT = 0;                    // ball-save time remaining
  bool everLeftLane = false;
  bool saveSpent = false;             // this ball number's save is used up
  char msg[28] = "";
  float msgT = 0;
  float t = 0;                        // game clock, seconds
  float acc = 0;                      // physics time not yet simulated
  float stateT = 0;                   // time in current state
  uint32_t events = 0;

  // dirty rectangles for the renderer
  static constexpr int MAXDIRTY = 48;
  Rect dirty[MAXDIRTY];
  int ndirty = 0;
  bool fullRedraw = true;

  // stats (host harness)
  uint32_t nBumper = 0, nSling = 0, nTarget = 0, nLane = 0, nNudge = 0, nKick = 0, nSpin = 0, nMulti = 0, nRampIn = 0, nRamp = 0;
  float rampRideSum = 0, rampRideMax = 0;  // completed rides, seconds
  uint32_t nFlipHit = 0;
  int gameSyns = 0, gameHandshakes = 0;  // this game, for extra-ball rules
  uint32_t nSkill = 0, nExtra = 0;
  // skill shot zone for the ball on the plunger
  bool skillArmed = false;
  float skillLo = 0, skillHi = 0;
  // extra balls earned and not yet played (SHOOT AGAIN lit)
  int extraBalls = 0;
  int showKey = -1;  // attract light show: last state drawn
  uint32_t rng = 0x2F6E2B1u;
  float rnd() {
    rng = rng * 1664525u + 1013904223u;
    return (rng >> 8) / 16777216.0f;
  }
  float flipHitT[2] = {-1, -1};  // last contact sound per flipper (game time)

  Game() { build(); resetFlippers(); }

  // ------------------------------------------------------------ setup
  void build() {
    nseg = 0;
    // arc: 20 segments from the left wall top over to the right wall top
    arcFirst = nseg;
    const int N = 20;
    for (int i = 0; i < N; i++) {
      float a0 = (float)M_PI + (float)M_PI * i / N;
      float a1 = (float)M_PI + (float)M_PI * (i + 1) / N;
      addSeg({ARC_CX + ARC_R * cosf(a0), ARC_CY + ARC_R * sinf(a0)},
             {ARC_CX + ARC_R * cosf(a1), ARC_CY + ARC_R * sinf(a1)}, 2, K_WALL);
    }
    arcCount = N;
    // outer walls and plunger lane
    addSeg({OUTER_L, ARC_CY}, {OUTER_L, SCR_H + 40}, 2, K_WALL);
    addSeg({OUTER_R, ARC_CY}, {OUTER_R, SCR_H + 40}, 2, K_WALL);
    addSeg({LANE_X, 214}, {LANE_X, SCR_H + 40}, 2, K_WALL);
    addSeg({LANE_X + 2, PLUNGER_Y}, {OUTER_R - 2, PLUNGER_Y}, 2, K_WALL);
    // one-way gate over the lane exit: ball passes upward, returning balls
    // roll off it to the left into the playfield
    {
      V a{LANE_X, 214}, b{OUTER_R, 192};
      V d = b - a;
      V n = perp(d) * (1.0f / len(d));       // (22,24)/|..| ... pick upper side
      if (n.y > 0) n = n * -1.0f;
      Seg& s = addSeg(a, b, 2, K_GATE);
      s.n = n;
    }
    // top lane posts
    for (int i = 0; i < 4; i++)
      addSeg({LANE_POST_X[i], LANE_POST_Y0}, {LANE_POST_X[i], LANE_POST_Y1}, 3, K_POST);
    // right bank wedge with three targets
    addSeg({LANE_X, 222}, {BANK_X, BANK_Y0}, 2, K_WALL);
    for (int i = 0; i < 3; i++)
      addSeg({BANK_X, bankTy(i)}, {BANK_X, bankTy(i + 1)}, 2, K_TARGET).id = i;
    addSeg({BANK_X, BANK_Y1}, {LANE_X, BANK_Y1 + 20}, 2, K_WALL);
    // ramp: cumulative path length, rails, and two posts flanking the mouth
    rampLen[0] = 0;
    for (int i = 1; i < RAMP_N; i++)
      rampLen[i] = rampLen[i - 1] + len(RAMP_PATH[i] - RAMP_PATH[i - 1]);
    for (int i = 0; i < RAMP_N; i++) {
      V a = RAMP_PATH[i > 0 ? i - 1 : 0], c = RAMP_PATH[i < RAMP_N - 1 ? i + 1 : i];
      V tg = (c - a) * (1.0f / len(c - a));
      float w = i == 0 ? RAMP_MOUTH_HALF : RAMP_HALF;
      rampL[i] = RAMP_PATH[i] + perp(tg) * w;
      rampR[i] = RAMP_PATH[i] - perp(tg) * w;
    }
    rampDir = (RAMP_PATH[1] - RAMP_PATH[0]) * (1.0f / len(RAMP_PATH[1] - RAMP_PATH[0]));
    addSeg(rampL[0], rampL[0], 3, K_POST);
    addSeg(rampR[0], rampR[0], 3, K_POST);
    // left orbit lane guide (the spinner hangs between it and the wall)
    addSeg({SPIN_GUIDE_X, 182}, {SPIN_GUIDE_X, 228}, 2, K_WALL);
    // left orbit exit: a ball riding the arc down the left wall gets thrown
    // back into play instead of straight down the outlane
    addSeg({OUTER_L, 236}, {34, 272}, 2, K_WALL);
    // lower playfield: separators (outlane | inlane) and slingshots, mirrored
    for (int side = 0; side < 2; side++) {
      auto X = [&](float x) { return side ? MIRROR - x : x; };
      V piv = side ? FLIP_PIV_R : FLIP_PIV_L;
      // the inlane guide runs flush into the top surface of the flipper's
      // base; aimed at the pivot centre, the fat pivot would stick up above
      // it and trap a rolling ball in the crook
      V corner{X(SEP_X), 384};
      V dir = (piv - corner) * (1.0f / len(piv - corner));
      V up = perp(dir);
      if (up.y > 0) up = up * -1.0f;
      float lift = FLIP_RB - 2;
      V cornerUp{corner.x, corner.y - lift / fabsf(dir.x)};
      addSeg({X(SEP_X), side ? SEP_TOP_R : SEP_TOP_L}, cornerUp, 2, K_WALL);
      addSeg(cornerUp, piv + up * lift, 2, K_WALL);
      V A{X(46), 326}, B{X(46), 360}, C{X(80), 384};
      V* tri = side ? slingR : slingL;
      tri[0] = A; tri[1] = B; tri[2] = C;
      addSeg(A, B, 2, K_WALL);
      addSeg(B, C, 2, K_WALL);
      V d = C - A;
      V n = perp(d) * (1.0f / len(d));
      // kick direction points away from the triangle's third vertex
      if (dot(n, B - A) > 0) n = n * -1.0f;
      Seg& s = addSeg(A, C, 2, K_SLING);
      s.id = side;
      s.n = n;
    }
  }

  void resetFlippers() {
    fl = {FLIP_PIV_L, FLIP_REST, FLIP_UP, FLIP_REST, 0, false};
    fr = {FLIP_PIV_R, (float)M_PI - FLIP_REST, (float)M_PI - FLIP_UP,
          (float)M_PI - FLIP_REST, 0, false};
  }

  // ------------------------------------------------------------ control
  void startGame() {
    demo = false;
    bonusActive = false;
    resetBallCounts();
    score = 0;
    gameSyns = gameHandshakes = 0;
    extraBalls = 0;
    rng ^= (uint32_t)(t * 1000.0f);  // a different skill zone each game
    ball = 1;
    mult = 1;
    memset(lane, 0, sizeof lane);
    memset(target, 0, sizeof target);
    synArmed = false;
    markSpinner();
    kickback = true;
    state = PLAY;
    stateT = 0;
    serveBall();
    say("BALL 1", 1.5f);
    events |= EV_START;
    fullRedraw = true;
  }

  // clear the table and put one ball on the plunger. `saved`: the same ball
  // again after a ball save, which gets no second save or skill shot
  void serveBall(bool saved = false) {
    for (Ball& b : balls)
      if (b.live) { markBallAt(b.p); b.live = false; }
    pendingAdd = 0;
    mbSaveT = 0;
    endMultiball(false);
    Ball& b = balls[0];
    b = Ball{};
    b.live = true;
    everLeftLane = false;
    saveSpent = saved;
    charge = 0;
    charging = false;
    touchReady = false;
    skillArmed = !saved;
    if (!saved) {
      float c = 0.45f + rnd() * 0.35f;
      skillLo = c - SKILL_W / 2;
      skillHi = c + SKILL_W / 2;
    }
    markBallAt(b.p);
    markPlunger();
  }

  // SYN FLOOD: top the table up to MAXBALLS, auto-plunged one at a time
  void startMultiball() {
    if (state != PLAY || multiball) return;
    multiball = true;
    nMulti++;
    pendingAdd = MAXBALLS - liveCount();
    addT = 0.6f;
    mbSaveT = MB_SAVE_S;
    say("SYN FLOOD!", 2.5f);
    events |= EV_MULTIBALL;
    markRampArrow();  // ramp lights for jackpots
    markHud();
  }

  int liveCount() const {
    int n = 0;
    for (const Ball& b : balls) n += b.live;
    return n;
  }
  static bool inLane(const Ball& b) { return b.p.x > LANE_X + 2 && b.p.y > 214; }
  Ball* laneBall() {
    for (Ball& b : balls)
      if (b.live && inLane(b)) return &b;
    return nullptr;
  }
  bool ballInLane() { return laneBall() != nullptr; }
  // live ball nearest the drain, outside the lane (what a player watches)
  const Ball* lowest() const {
    const Ball* best = nullptr;
    for (const Ball& b : balls)
      if (b.live && !b.onRamp && !inLane(b) && (!best || b.p.y > best->p.y)) best = &b;
    return best;
  }

  // advance one rendered frame of `dt` seconds
  void frame(float dt, Input in) {
    if (dt > 0.05f) dt = 0.05f;
    t += dt;
    stateT += dt;
    Input fresh{in.left && !prevIn.left, in.right && !prevIn.right, in.touch && !prevIn.touch};
    prevIn = in;
    if (!in.touch || fresh.touch) touchReady = true;
    V p0[MAXBALLS];
    bool live0[MAXBALLS];
    for (int i = 0; i < MAXBALLS; i++) { p0[i] = balls[i].p; live0[i] = balls[i].live; }

    if (msgT > 0 && (msgT -= dt) <= 0) { msg[0] = 0; markMsg(); }
    if (hudNoteT > 0 && (hudNoteT -= dt) <= 0) { hudNote[0] = 0; markHud(); }
    updateBlink();
    decay(bumpFlash, NBUMP, dt, [&](int i) { markCircle(BUMP_C[i], BUMP_R + 3); });
    decay(slingFlash, 2, dt, [&](int i) { markSling(i); });
    decay(targetFlash, 3, dt, [&](int i) { markTarget(i); });

    if (state != PLAY) {
      updateShow();
      // "TAP TO START" appears once the game-over screen has been up a moment
      if (state == GAMEOVER && stateT >= 1.2f && stateT - dt < 1.2f)
        mark(PANEL_X, PANEL_Y, PANEL_X + PANEL_W, PANEL_Y + PANEL_H);
      // attract / game over: flippers idle, wait for a tap
      updateFlipper(fl, false, dt);
      updateFlipper(fr, false, dt);
      return;
    }

    if (demo && stateT > DEMO_MAX_S) { endDemo(); return; }

    if (bonusActive) {
      updateFlipper(fl, in.left, dt);
      updateFlipper(fr, in.right, dt);
      // a new press hurries it; one held through the drain doesn't
      if (fresh.left || fresh.right || fresh.touch) bonusFast = true;
      updateBonus(dt);
      return;
    }

    // plunger: when a ball sits in the lane, a touch charges and release fires
    Ball* lb = laneBall();
    bool resting = lb && fabsf(lb->v.y) < 30 && lb->p.y > BALL_HOME_Y - 3;
    if (resting && in.touch && touchReady) {
      charging = true;
      charge = clampf(charge + dt / PLUNGE_FILL_S, 0, 1);
      markPlunger();
      in.left = in.right = false;  // the same touch shouldn't flip
    } else if (charging) {
      charging = false;
      if (resting) {
        lb->v.y = -(PLUNGE_MIN + (PLUNGE_MAX - PLUNGE_MIN) * charge);
        events |= EV_LAUNCH;
        if (skillArmed) {  // first launch of this ball only
          skillArmed = false;
          if (charge >= skillLo && charge <= skillHi) {
            addScore(SKILL_PTS);
            say("SKILL SHOT!", 1.8f);
            events |= EV_SKILL;
            nSkill++;
          }
        }
      }
      charge = 0;
      markPlunger();
    }

    // multiball: feed queued balls through the lane, launched automatically
    if (pendingAdd > 0 && (addT -= dt) <= 0 && !lb) {
      for (Ball& b : balls)
        if (!b.live) {
          b = Ball{};
          b.v = {0, -AUTO_PLUNGE_V};
          b.live = true;
          pendingAdd--;
          addT = 0.8f;
          events |= EV_LAUNCH;
          break;
        }
    }

    // lane change: a flipper press rotates the lit S/Y/N lanes
    if (in.left && !fl.held) rotateLanes(-1);
    if (in.right && !fr.held) rotateLanes(+1);
    if ((in.left && !fl.held) || (in.right && !fr.held)) events |= EV_FLIP;

    // physics substeps (flippers move inside the loop so the balls see them sweep)
    acc += dt;
    while (acc >= SUB_DT) {
      acc -= SUB_DT;
      updateFlipper(fl, in.left, SUB_DT);
      updateFlipper(fr, in.right, SUB_DT);
      for (Ball& b : balls)
        if (b.live) substep(b, SUB_DT);
      ballBallCollide();
    }

    if (!everLeftLane)
      for (Ball& b : balls)
        if (b.live && b.p.y < 200) {
          everLeftLane = true;
          if (!saveSpent) saveT = BALL_SAVE_S;
        }
    if (saveT > 0) saveT -= dt;
    if (mbSaveT > 0) mbSaveT -= dt;
    bool lit = saveT > 0 || mbSaveT > 0;
    if (lit != saveLit) { saveLit = lit; markApron(); }

    for (Ball& b : balls) {
      if (!b.live) continue;
      if (b.onRamp) continue;
      stuckWatchdog(b, dt, in);
      // left outlane kickback: fires the ball back up the outlane when lit
      if (kickback && b.p.x < KICK_X && b.p.y > KICK_Y) {
        kickback = false;
        b.p.x = KICK_LANE_X;
        b.v = {0, -KICK_V};
        b.kick = true;
        markKickback();
        say("KICKBACK", 1.2f);
        events |= EV_KICKBACK;
        nKick++;
      }
    }

    updateSpinner(dt);

    for (Ball& b : balls)
      if (b.live && b.p.y > DRAIN_Y) drainBall(b);

    // ball dirty rects: old + new positions
    for (int i = 0; i < MAXBALLS; i++) {
      if (live0[i]) markBallAt(p0[i]);
      if (balls[i].live) markBallAt(balls[i].p);
    }
  }

  // attract light show. newHigh: the game-over screen of a record score,
  // where every lamp flashes instead
  bool newHigh() const { return state == GAMEOVER && score > 0 && score >= hiscore; }
  bool showLamp(int i) const {
    if (newHigh()) return blinkOn;
    int step = (int)(stateT * SHOW_HZ) % SHOW_LAMPS;
    return i == step || i == (step + SHOW_LAMPS - 1) % SHOW_LAMPS;  // a lamp and its tail
  }
  PB_NOINLINE void updateShow() {
    int key = newHigh() ? (blinkOn ? 100 : 101) : (int)(stateT * SHOW_HZ) % SHOW_LAMPS;
    if (key == showKey) return;
    showKey = key;
    for (int i = 0; i < 3; i++) markLane(i);
    markSpinner();
    markKickback();
    markApron();
  }

  // serial debug: jump to the game-over screen with a given score, without
  // touching the saved high score (for checking that screen on hardware)
  void debugGameOver(uint32_t sc) {
    for (Ball& b : balls) b.live = false;
    endMultiball(false);
    demo = false;
    bonusActive = false;
    score = sc;
    state = GAMEOVER;
    stateT = 2;  // past the TAP TO START delay
    fullRedraw = true;
  }

  // a short note in the top bar, for a couple of seconds
  void notify(const char* s) {
    snprintf(hudNote, sizeof hudNote, "%s", s);
    hudNoteT = 2.0f;
    markHud();
  }

  void tap() {  // touch on attract / game-over screen, or during the demo
    if (demo || state == ATTRACT || (state == GAMEOVER && stateT > 1.2f)) startGame();
  }

  // attract-mode demo: an ordinary game played by the bot, which never
  // touches the high score and goes back to the title screen when it ends
  void startDemo() {
    startGame();
    demo = true;
    msg[0] = 0;
    msgT = 0;
  }

  bool demoDue() const { return !demo && state != PLAY && stateT > DEMO_IDLE_S; }

  void endDemo() {
    for (Ball& b : balls) b.live = false;
    endMultiball(false);
    demo = false;
    state = ATTRACT;
    stateT = 0;
    msg[0] = 0;
    msgT = 0;
    fullRedraw = true;
  }

  // ------------------------------------------------------------ geometry queries
  Rect flipperBox(const Flipper& f) const {
    V tp = f.tip();
    float x0 = fminf(f.piv.x - FLIP_RB, tp.x - FLIP_RT) - 2;
    float y0 = fminf(f.piv.y - FLIP_RB, tp.y - FLIP_RT) - 2;
    float x1 = fmaxf(f.piv.x + FLIP_RB, tp.x + FLIP_RT) + 2;
    float y1 = fmaxf(f.piv.y + FLIP_RB, tp.y + FLIP_RT) + 2;
    return {(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0 + 1), (int16_t)(y1 - y0 + 1)};
  }

  void mark(float x0, float y0, float x1, float y1) {
    int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0);
    int ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > SCR_W) ix1 = SCR_W;
    if (iy1 > SCR_H) iy1 = SCR_H;
    if (ix1 <= ix0 || iy1 <= iy0) return;
    if (ndirty >= MAXDIRTY) {
      // list full (multiball can mark a lot in one frame): fold this rect
      // into whichever existing one grows least. A full-screen redraw
      // instead costs ~170 ms, and the game plays that frame in slow motion.
      int best = 0;
      long bestGrow = 0x7fffffff;
      for (int i = 0; i < ndirty; i++) {
        const Rect& r = dirty[i];
        int ux0 = ix0 < r.x ? ix0 : r.x, uy0 = iy0 < r.y ? iy0 : r.y;
        int ux1 = ix1 > r.x + r.w ? ix1 : r.x + r.w, uy1 = iy1 > r.y + r.h ? iy1 : r.y + r.h;
        long grow = (long)(ux1 - ux0) * (uy1 - uy0) - (long)r.w * r.h;
        if (grow < bestGrow) { bestGrow = grow; best = i; }
      }
      Rect& r = dirty[best];
      int ux0 = ix0 < r.x ? ix0 : r.x, uy0 = iy0 < r.y ? iy0 : r.y;
      int ux1 = ix1 > r.x + r.w ? ix1 : r.x + r.w, uy1 = iy1 > r.y + r.h ? iy1 : r.y + r.h;
      r = {(int16_t)ux0, (int16_t)uy0, (int16_t)(ux1 - ux0), (int16_t)(uy1 - uy0)};
      return;
    }
    dirty[ndirty++] = {(int16_t)ix0, (int16_t)iy0, (int16_t)(ix1 - ix0), (int16_t)(iy1 - iy0)};
  }
  void markR(Rect r) { mark(r.x, r.y, r.x + r.w, r.y + r.h); }
  void markCircle(V c, float r) { mark(c.x - r, c.y - r, c.x + r, c.y + r); }
  void markBallAt(V q) { markCircle(q, BALL_R + 5); }  // covers a ramp ball's shadow
  PB_NOINLINE void updateBlink() {
    bool on = fmodf(t * BLINK_HZ, 1.0f) < 0.5f;
    if (on == blinkOn) return;
    blinkOn = on;
    if (synArmed) {  // ACK targets say "finish me for the HANDSHAKE"
      for (int i = 0; i < 3; i++)
        if (!target[i]) markTarget(i);
      markSpinner();
    }
    if (multiball) markRampArrow();  // jackpot lit
    if (saveLit && saveBlinking()) markApron();
  }
  bool saveBlinking() const {
    float left = saveT > mbSaveT ? saveT : mbSaveT;
    return left > 0 && left < SAVE_WARN_S;
  }

  void markRampArrow() { V c = rampArrowAt(); markCircle(c, 9); }
  V rampArrowAt() const { return RAMP_PATH[0] - rampDir * 22; }
  void markHud() { mark(0, 0, SCR_W, HUD_H); }
  void markMsg() { mark(MSG_X, MSG_Y, MSG_X + MSG_W, MSG_Y + MSG_H); }
  void markPlunger() { mark(LANE_X + 2, 230, OUTER_R - 1, SCR_H); }
  void markSpinner() { mark(SPIN_X0, SPIN_Y - 8, SPIN_X1 + 1, SPIN_Y + 24); }  // + arrow lamp
  void markApron() { mark(OUTER_L + 2, APRON_Y - 2, LANE_X - 1, SCR_H); }
  void markKickback() { mark(OUTER_L + 2, KICK_Y - 34, KICK_X - 1, KICK_Y - 8); }
  void markLane(int i) { mark(LANE_POST_X[i], 96, LANE_POST_X[i + 1], 118); }
  void markTarget(int i) { mark(BANK_X - 22, bankTy(i), LANE_X - 2, bankTy(i + 1)); }  // + letter
  void markSling(int s) {
    const V* tri = s ? slingR : slingL;
    // pad covers the lit kick face's glow (r 3 + 1.5)
    float x0 = fminf(tri[0].x, tri[2].x) - 6, x1 = fmaxf(tri[0].x, tri[2].x) + 6;
    mark(x0, tri[0].y - 6, x1, tri[2].y + 6);
  }

  // host tests: an extra post, to build a trap a ball can rest in
  void addPost(V p, float r) { addSeg(p, p, r, K_POST); }

 private:
  Seg& addSeg(V a, V b, float r, Kind k) {
    Seg& s = seg[nseg++];
    s = {a, b, r, k, 0, {0, 0}};
    return s;
  }

  template <class F>
  void decay(float* arr, int n, float dt, F onEnd) {
    for (int i = 0; i < n; i++)
      if (arr[i] > 0 && (arr[i] -= dt) <= 0) { arr[i] = 0; onEnd(i); }
  }

  void say(const char* s, float secs) {
    snprintf(msg, sizeof msg, "%s", s);
    msgT = secs;
    markMsg();
  }

  void addScore(uint32_t pts) {
    score += pts * mult * (multiball ? 2 : 1);
    markHud();
  }

  void updateFlipper(Flipper& f, bool held, float dt) {
    f.held = held;
    float target = held ? f.up : f.rest;
    float speed = held ? FLIP_UP_W : FLIP_DN_W;
    float d = target - f.ang;
    float step = speed * dt;
    float old = f.ang;
    if (fabsf(d) <= step) f.ang = target;
    else f.ang += d > 0 ? step : -step;
    f.w = (f.ang - old) / dt;
    if (f.ang != old) {
      // dirty: union of before/after poses
      Flipper tmp = f;
      tmp.ang = old;
      markR(flipperBox(tmp));
      markR(flipperBox(f));
    }
  }

  void updateSpinner(float dt) {
    if (spinW <= 0) return;
    float old = spinAng;
    spinAng += spinW * dt;
    spinW = spinW * expf(-1.1f * dt) - 3.0f * dt;  // bearing drag + friction
    int halves = (int)floorf(spinAng / (float)M_PI) - (int)floorf(old / (float)M_PI);
    if (halves > 0 && state == PLAY) {
      ballSpins += halves;
      addScore(SPIN_PTS * halves * (synArmed ? 2 : 1));  // doubled while SYN is armed
      events |= EV_SPIN;
    }
    if (spinW <= 0) {
      // settles flat, as gravity would leave it
      spinW = 0;
      spinAng = roundf(spinAng / (float)M_PI) * (float)M_PI;
    }
    if (spinAng > 2 * (float)M_PI) spinAng -= 2 * (float)M_PI;
    markSpinner();
  }

  void rotateLanes(int dir) {
    bool l0 = lane[0], l1 = lane[1], l2 = lane[2];
    if (dir > 0) { lane[0] = l2; lane[1] = l0; lane[2] = l1; }
    else         { lane[0] = l1; lane[1] = l2; lane[2] = l0; }
    for (int i = 0; i < 3; i++) markLane(i);
  }

  // capsule (segment a-b, radius ra at a, rb at b) vs ball.
  // piv/w give the surface velocity of a rotating flipper.
  bool collide(Ball& b, V a, V bb, float ra, float rb, float e, const V* piv, float w,
               V* nOut, float* vnOut) {
    V d = bb - a;
    float L2 = dot(d, d);
    float tt = L2 > 0 ? clampf(dot(b.p - a, d) / L2, 0, 1) : 0;
    V c = a + d * tt;
    float rr = ra + (rb - ra) * tt + BALL_R;
    V dv = b.p - c;
    float dist2 = dot(dv, dv);
    if (dist2 >= rr * rr) return false;
    float dist = sqrtf(dist2);
    V n = dist > 1e-4f ? dv * (1.0f / dist) : V{0, -1};
    b.p = c + n * rr;
    V vs = piv ? perp(c - *piv) * w : V{0, 0};
    V rel = b.v - vs;
    float vn = dot(rel, n);
    if (vn < 0) {
      rel = rel - n * ((1 + e) * vn);
      // a touch of friction on real impacts only. A ball rolling along a
      // guide or flipper "hits" it every substep as gravity presses it in, and
      // braking each of those (600/s) capped rolling at ~50 px/s: a ball took
      // 2.6 s to roll down the inlane guide instead of ~0.7 s
      if (vn < -60) {
        V tg = perp(n);
        rel = rel - tg * (dot(rel, tg) * 0.015f);
      }
      b.v = rel + vs;
    }
    if (nOut) *nOut = n;
    if (vnOut) *vnOut = vn;
    return true;
  }

  void substep(Ball& b, float dt) {
    if (b.onRamp) { rampStep(b, dt); return; }
    if (b.kick) {  // checked every substep, so a slow frame can't skip it
      if (b.v.y > 0) b.kick = false;  // fell back without reaching the hook
      else if (b.p.y < KICK_HOOK_Y) {
        b.kick = false;
        b.v = {KICK_OUT_VX, fmaxf(b.v.y, -KICK_OUT_VY)};
      }
    }
    b.v.y += GRAVITY * dt;
    float sp = len(b.v);
    if (sp > MAX_V) b.v = b.v * (MAX_V / sp);
    V pPrev = b.p;
    float y0 = b.p.y;
    b.p = b.p + b.v * dt;
    // ramp mouth: crossing the mouth line between the posts, heading in
    {
      V m = RAMP_PATH[0];
      float f0 = dot(pPrev - m, rampDir), f1 = dot(b.p - m, rampDir);
      float lat = dot(b.p - m, perp(rampDir));
      if (f0 < 0 && f1 >= 0 && fabsf(lat) < RAMP_MOUTH_HALF - 3 && dot(b.v, rampDir) > 0) {
        b.onRamp = true;
        b.rideT = 0;
        b.rs = f1;
        b.ru = dot(b.v, rampDir);
        nRampIn++;
        return;
      }
    }
    // spinner: a sensor, not a collider - the ball passes under the plate
    if ((y0 - SPIN_Y) * (b.p.y - SPIN_Y) <= 0 && y0 != b.p.y && b.p.x > SPIN_X0 && b.p.x < SPIN_X1) {
      spinW = fmaxf(spinW, fabsf(b.v.y) * SPIN_PER_PX);
      b.v = b.v * 0.94f;  // a little drag from the flap
      nSpin++;
    }

    for (int i = 0; i < nseg; i++) {
      Seg& s = seg[i];
      if (s.kind == K_GATE) {
        // one-way: only blocks a ball whose centre is on the playfield side
        if (dot(b.p - s.a, s.n) < 0) continue;
      }
      V n;
      float vn;
      if (!collide(b, s.a, s.b, s.r, s.r, E_WALL, nullptr, 0, &n, &vn)) continue;
      if (s.kind == K_SLING && vn < -40 && slingFlash[s.id] <= 0) {
        b.v = b.v + s.n * SLING_KICK;
        slingFlash[s.id] = 0.12f;
        markSling(s.id);
        addScore(10);
        nSling++;
        events |= EV_SLING;
      } else if (s.kind == K_TARGET && vn < -60 && targetFlash[s.id] <= 0) {
        hitTarget(s.id);
      }
    }

    for (int i = 0; i < NBUMP; i++) {
      V dv = b.p - BUMP_C[i];
      float rr = BUMP_R + BALL_R;
      float d2 = dot(dv, dv);
      if (d2 >= rr * rr) continue;
      float d = sqrtf(d2);
      V n = d > 1e-4f ? dv * (1.0f / d) : V{0, -1};
      b.p = BUMP_C[i] + n * rr;
      float vn = dot(b.v, n);
      // strip the inbound component, fire outward at kick speed
      b.v = b.v - n * vn + n * fmaxf(BUMPER_KICK, -vn * 0.6f);
      if (bumpFlash[i] <= 0) {
        bumpFlash[i] = 0.10f;
        markCircle(BUMP_C[i], BUMP_R + 3);
        addScore(100);
        nBumper++;
        events |= EV_BUMPER;
      }
    }

    flipperCollide(b, fl);
    flipperCollide(b, fr);
    laneSensors(b);
  }

  void flipperCollide(Ball& b, Flipper& f) {
    float vn;
    if (!collide(b, f.piv, f.tip(), FLIP_RB, FLIP_RT, E_FLIP, &f.piv, f.w, nullptr, &vn)) return;
    // a real hit (not a ball resting or rolling on it) makes a sound; one per
    // flipper per 80 ms, since a hit spans several substeps
    int side = &f == &fr;
    if (vn < -120 && t - flipHitT[side] > 0.08f) {
      flipHitT[side] = t;
      nFlipHit++;
      events |= EV_FLIP_HIT;
    }
  }

  // equal-mass, slightly lossy collisions between balls
  void ballBallCollide() {
    for (int i = 0; i < MAXBALLS; i++)
      for (int j = i + 1; j < MAXBALLS; j++) {
        Ball &a = balls[i], &c = balls[j];
        if (!a.live || !c.live || a.onRamp || c.onRamp) continue;
        V d = c.p - a.p;
        float d2 = dot(d, d), rr = 2 * BALL_R;
        if (d2 >= rr * rr || d2 < 1e-6f) continue;
        float dist = sqrtf(d2);
        V n = d * (1.0f / dist);
        float push = (rr - dist) * 0.5f;
        a.p = a.p - n * push;
        c.p = c.p + n * push;
        float vn = dot(c.v - a.v, n);
        if (vn < 0) {
          V imp = n * (-(1 + 0.9f) * vn * 0.5f);
          a.v = a.v - imp;
          c.v = c.v + imp;
        }
      }
  }

  // point on the ramp centreline at distance s, and the direction there
  V rampPoint(float s, V* tg) const {
    int i = 1;
    while (i < RAMP_N - 1 && rampLen[i] < s) i++;
    V a = RAMP_PATH[i - 1], c = RAMP_PATH[i];
    float segLen = rampLen[i] - rampLen[i - 1];
    float f = clampf((s - rampLen[i - 1]) / segLen, 0, 1);
    *tg = (c - a) * (1.0f / segLen);
    return a + (c - a) * f;
  }

  void rampStep(Ball& b, float dt) {
    b.rideT += dt;
    float a = b.rs < rampLen[RAMP_TOP_IDX]    ? -RAMP_UP_DEC
              : b.rs < rampLen[RAMP_DOWN_IDX] ? -RAMP_DRAG
                                              : RAMP_DOWN_ACC;
    b.ru += a * dt;
    // past the crest it never stalls; before it, it can roll back
    if (b.rs >= rampLen[RAMP_TOP_IDX] && b.ru < RAMP_MIN_V) b.ru = RAMP_MIN_V;
    b.rs += b.ru * dt;
    if (b.rs <= 0) {
      // didn't make it: rolls back out of the mouth onto the playfield
      b.onRamp = false;
      b.p = RAMP_PATH[0] - rampDir * 0.5f;
      b.v = rampDir * b.ru;
      return;
    }
    V tg;
    if (b.rs >= rampLen[RAMP_N - 1]) {
      // made it: drops off the end into the left inlane
      b.onRamp = false;
      rampPoint(rampLen[RAMP_N - 1], &tg);
      b.p = RAMP_PATH[RAMP_N - 1];
      b.v = tg * fminf(b.ru, RAMP_EXIT_MAX);
      nRamp++;
      ballRamps++;
      rampRideSum += b.rideT;
      rampRideMax = fmaxf(rampRideMax, b.rideT);
      // the uplink also lights the next unlit S/Y/N lane
      for (int i = 0; i < 3; i++)
        if (!lane[i]) { lightLane(i); break; }
      if (multiball) {
        addScore(RAMP_JACKPOT);
        say("JACKPOT!", 2.0f);
        events |= EV_JACKPOT;
      } else {
        addScore(RAMP_SCORE);
        say("UPLINK", 1.2f);
        events |= EV_RAMP;
      }
      return;
    }
    b.p = rampPoint(b.rs, &tg);
    b.v = tg * b.ru;
  }

  void laneSensors(Ball& b) {
    for (int i = 0; i < 3; i++) {
      bool in = fabsf(b.p.x - laneCx(i)) < 13 && fabsf(b.p.y - LANE_SENSE_Y) < 6;
      if (in && !b.inLaneBand[i] && !lane[i]) lightLane(i);
      b.inLaneBand[i] = in;
    }
  }

  void lightLane(int i) {
    lane[i] = true;
    markLane(i);
    addScore(500);
    nLane++;
    ballLanes++;
    events |= EV_LANE;
    if (lane[0] && lane[1] && lane[2]) synComplete();
  }

  void synComplete() {
    gameSyns++;
    addScore(2000);
    if (mult < 5) mult++;
    markApron();
    synArmed = true;
    markSpinner();  // spinner shows its doubled value while armed
    memset(lane, 0, sizeof lane);
    for (int i = 0; i < 3; i++) markLane(i);
    markHud();
    char b[20];
    snprintf(b, sizeof b, "SYN SENT  %dx", mult);
    say(b, 2.0f);
    events |= EV_SYN;
  }

  void hitTarget(int i) {
    targetFlash[i] = 0.15f;
    markTarget(i);
    nTarget++;
    ballTargets++;
    events |= EV_TARGET;
    if (target[i]) { addScore(100); return; }
    target[i] = true;
    addScore(750);
    if (target[0] && target[1] && target[2]) {
      memset(target, 0, sizeof target);
      bool relit = !kickback;
      if (relit) { kickback = true; markKickback(); }
      for (int k = 0; k < 3; k++) markTarget(k);
      if (synArmed) {
        synArmed = false;
        markSpinner();
        addScore(25000);
        say("HANDSHAKE!", 2.5f);
        gameHandshakes++;
        events |= EV_JACKPOT;
        startMultiball();  // no-op if already flooding
        if (gameHandshakes == 2) {  // the second HANDSHAKE of a game: extra ball
          extraBalls++;
          nExtra++;
          markApron();
          say("EXTRA BALL!", 2.5f);
          events |= EV_EXTRA;
        }
      } else {
        addScore(5000);
        say(relit ? "KICKBACK LIT" : "ACK", 1.5f);
        events |= EV_ACK;
      }
    }
  }

  // a ball resting on a flipper that's held up (the player is cradling it)
  bool cradled(const Ball& b, Input in) const {
    for (int side = 0; side < 2; side++) {
      const Flipper& f = side ? fr : fl;
      if (!(side ? in.right : in.left)) continue;
      V d = f.tip() - f.piv;
      float u = clampf(dot(b.p - f.piv, d) / dot(d, d), 0, 1);
      if (len(b.p - (f.piv + d * u)) < BALL_R + FLIP_RB + 4) return true;
    }
    return false;
  }

  void stuckWatchdog(Ball& b, float dt, Input in) {
    // a ball that hasn't left a small circle for 3 s is wedged (or rattling
    // in a trap) and gets a nudge. Only the lane and a cradle are exempt: one
    // held flipper mustn't switch rescue off for every ball on the table.
    if (inLane(b) || cradled(b, in) || len(b.p - b.anchor) > STILL_R) {
      b.anchor = b.p;
      b.stillT = 0;
      return;
    }
    if ((b.stillT += dt) > 3.0f) {
      b.stillT = 0;
      b.anchor = b.p;
      b.v = {(b.p.x < 148 ? 1 : -1) * 120.0f, -320.0f};
      nNudge++;
      events |= EV_NUDGE;
    }
  }

  void endMultiball(bool announce) {
    if (!multiball) return;
    multiball = false;
    markRampArrow();
    markHud();
    if (announce) say("FLOOD OVER", 1.5f);
  }

  PB_NOINLINE void drainBall(Ball& b) {
    b.live = false;
    markBallAt(b.p);
    if (liveCount() > 0 || pendingAdd > 0) {
      // one of several balls: costs nothing, and comes back while the
      // multiball save is running
      if (mbSaveT > 0) {
        pendingAdd++;
        say("BALL SAVED", 1.2f);
        events |= EV_SAVE;
      }
      if (liveCount() + pendingAdd <= 1) endMultiball(true);
      return;
    }
    // the last ball is gone
    if (saveT > 0 && everLeftLane) {
      saveT = 0;
      say("BALL SAVED", 1.5f);
      events |= EV_SAVE;
      serveBall(true);
      return;
    }
    events |= EV_DRAIN;
    endMultiball(false);
    startBonus(ball >= BALLS_PER_GAME);
  }

  void resetBallCounts() { ballRamps = ballLanes = ballTargets = ballSpins = 0; }

  // ---- end-of-ball bonus: one line per thing collected, then the multiplier,
  // then the total counting into the score
  struct BonusLine {
    const char* what;
    uint32_t count, each;
  };
  int bonusLines(BonusLine* out) const {
    int n = 0;
    if (ballRamps) out[n++] = {"RAMPS", ballRamps, BONUS_RAMP};
    if (ballLanes) out[n++] = {"LANES", ballLanes, BONUS_LANE};
    if (ballTargets) out[n++] = {"TARGETS", ballTargets, BONUS_TARGET};
    if (ballSpins) out[n++] = {"SPINS", ballSpins, BONUS_SPIN};
    return n;
  }

  PB_NOINLINE void startBonus(bool thenOver) {
    BonusLine L[4];
    int n = bonusLines(L);
    uint32_t base = 0;
    for (int i = 0; i < n; i++) base += L[i].count * L[i].each;
    bonusThenOver = thenOver;
    if (base == 0) { finishBall(); return; }
    bonusTotal = base * (uint32_t)mult;
    bonusShown = 0;
    bonusActive = true;
    bonusFast = false;
    bonusStep = -1;
    bonusT = 0.6f;  // a beat after the drain before the first line
    say("BONUS", 0.8f);
  }

  PB_NOINLINE void updateBonus(float dt) {
    BonusLine L[4];
    int n = bonusLines(L);
    float step = bonusFast ? 0.08f : BONUS_STEP_S;
    // lines: 0..n-1 items, n multiplier (only if > 1x), n+1 total count-up
    if (bonusStep == n + 1) {
      // count the total into the score, ~1 s (instantly when hurried)
      uint32_t left = bonusTotal - bonusShown;
      uint32_t chunk = bonusFast ? left : (bonusTotal / 40 + 1);
      if (chunk > left) chunk = left;
      if ((bonusT -= dt) <= 0 || bonusFast) {
        bonusT = 0.025f;
        bonusShown += chunk;
        score += chunk;
        markHud();
        events |= EV_BONUS_TICK;
        char b[24], v[16];
        fmtNum(v, sizeof v, bonusShown);
        snprintf(b, sizeof b, "+%s", v);
        say(b, 5);
        if (bonusShown >= bonusTotal) {
          bonusStep++;
          bonusT = bonusFast ? 0.3f : 1.0f;
        }
      }
      return;
    }
    if ((bonusT -= dt) > 0) return;
    bonusT = step;
    bonusStep++;
    if (bonusStep == n && mult <= 1) bonusStep++;  // no multiplier line at 1x
    char b[28], v[16];
    if (bonusStep < n) {
      fmtNum(v, sizeof v, L[bonusStep].count * L[bonusStep].each);
      snprintf(b, sizeof b, "%u %s  %s", (unsigned)L[bonusStep].count, L[bonusStep].what, v);
      say(b, 5);
      events |= EV_BONUS_LINE;
    } else if (bonusStep == n) {
      snprintf(b, sizeof b, "BONUS x%d", mult);
      say(b, 5);
      events |= EV_BONUS_LINE;
    } else if (bonusStep == n + 1) {
      bonusT = 0;  // count-up starts now
    } else {
      bonusActive = false;
      msg[0] = 0;
      msgT = 0;
      markMsg();
      finishBall();
    }
  }

  static void fmtNum(char* out, size_t n, uint32_t v) {
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

  // the bonus is done (or there was none): next ball, or the game is over
  void finishBall() {
    resetBallCounts();
    bool again = extraBalls > 0;  // SHOOT AGAIN: same ball number
    if (again) {
      extraBalls--;
      markApron();
    }
    if (bonusThenOver && !again) {
      if (demo) { endDemo(); return; }
      state = GAMEOVER;
      stateT = 0;
      if (score > hiscore) hiscore = score;
      events |= EV_GAMEOVER;
      fullRedraw = true;
      return;
    }
    if (!again) ball++;
    mult = 1;
    markApron();
    synArmed = false;
    markSpinner();
    markHud();
    char buf[20];
    if (again) snprintf(buf, sizeof buf, "SHOOT AGAIN");
    else snprintf(buf, sizeof buf, "BALL %d", ball);
    say(buf, 1.5f);
    serveBall();
  }
};

}  // namespace pb
