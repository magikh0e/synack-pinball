// SYN/ACK pinball: a simple automatic player.
//
// Drives the attract-mode demo on the device and the soak tests in
// host/sim.cpp. Pure C++, like game.hpp.

#pragma once
#include "game.hpp"

namespace pb {

struct Bot {
  float holdLeft = -1, holdT = 0, trigY = 400, slowT = 0, waitFor = 0.5f;
  bool holdLeftSide = true;
  uint32_t rng = 0x2545F491;

  float rnd() {  // 0..1
    rng = rng * 1664525u + 1013904223u;
    return (rng >> 8) / 16777216.0f;
  }

  Input step(Game& g, float dt) {
    Input in{false, false, false};
    Ball* lb = g.laneBall();
    const Ball* b = g.lowest();
    // launch: hold for a random time once nothing else is in play
    if (lb && fabsf(lb->v.y) < 1 && !b) {
      if (holdLeft < 0) holdLeft = 0.5f + rnd() * 0.6f;
      holdLeft -= dt;
      in.touch = holdLeft > 0;
      if (holdLeft <= 0) holdLeft = -1;
    }
    // timed flips: when the lowest ball drops past a randomised trigger line
    // near a flipper, hold that flipper for 0.2s. Slow balls on the flippers
    // get flipped after a random wait, like a player cradling then shooting.
    if (holdT > 0) {
      holdT -= dt;
      (holdLeftSide ? in.left : in.right) = true;
    } else if (b && b->v.y > 0 && b->p.y > trigY && b->p.y < 440) {
      holdT = 0.2f;
      holdLeftSide = b->p.x < 148;
      trigY = 385 + rnd() * 30;
    } else if (b && b->p.y > 380 && len(b->v) < 40) {
      if ((slowT += dt) > waitFor) {
        slowT = 0;
        waitFor = 0.2f + rnd();
        holdT = 0.2f;
        holdLeftSide = b->p.x < 148;
      }
    } else {
      slowT = 0;
    }
    return in;
  }
};

}  // namespace pb
