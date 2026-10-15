// SYN/ACK pinball: tabletop cabinet for the LCDwiki ES3C35P (concept A).
//
// The board sits face-up under the sloped deck, mounted upside down (build the
// firmware with env es3c35p-cabinet, which turns the image and touch 180
// degrees). That puts its USB-C at the back, its speaker socket near the
// backbox and its UART socket (the flipper buttons) near the front.
//
// Parts (set PART, or use export.sh to write every STL):
//   body     - walls, floor, button holes, deck screw bosses, USB-C panel socket
//   deck     - the sloped plate: screen window, board standoffs (M3 heat-set)
//   backbox  - speaker grille, marquee window, and a shelf the speaker stops under
//   marquee  - the SYN/ACK card that drops into the backbox's top
//   assembly - everything in place, for checking (not for printing)
//   section  - the assembly cut down the middle, with the hardware
//   clash    - the USB-C plug, the speaker and the card (in place, and on their
//              way in) against every printed part: must render empty
//
// Hardware: 2x 24 mm arcade buttons (flippers; solder or right-angle spades give
// the most room inside), 1x 16 mm momentary button
// (launch), one 40.4 x 28.4 x 9.4 mm cavity speaker (8 ohm), 4x M3 heat-set inserts +
// 4x M3x6 (board), 4x M3 heat-set inserts + 4x M3x8 countersunk (deck),
// 4 rubber feet (10 mm), a USB-C panel-mount extension (30 cm, straight plug).
//
// Board numbers: LCDwiki ES3C35P LCM outline drawing V1.0.

PART = "assembly";  // body | deck | backbox | marquee | assembly | section

$fn = 64;
eps = 0.01;

// ---------------------------------------------------------------- board
PCB_W = 54.5;  PCB_L = 101.5;  PCB_T = 1.6;
HOLE_DX = 47.9; HOLE_DY = 94.5; HOLE_D = 3.2;
ABOVE_PCB = 3.7;   // touch glass 1.0 + LCD 2.2 + glue 0.5
BELOW_PCB = 4.7;   // parts on the back
VA_W = 49.96; VA_L = 74.44; VA_TOP = 10.9;  // visible area, from the board's top edge

// ---------------------------------------------------------------- cabinet
W = 92;            // outside width: two facing 24 mm buttons need ~82 mm inside (see below)
WALL = 2.4;
FLOOR = 2.4;
DECK_D = 110;      // deck depth (horizontal)
REAR_D = 26;       // flat section under the backbox
D = DECK_D + REAR_D;
Z_FRONT = 30;      // deck height at the front
Z_REAR = 95;       // ... and at the back
THETA = atan((Z_REAR - Z_FRONT) / DECK_D);   // ~30.6 degrees
SLOPE_L = sqrt(DECK_D * DECK_D + (Z_REAR - Z_FRONT) * (Z_REAR - Z_FRONT));

DECK_T = 2.4;
BOARD_S0 = 12;     // board's front edge, measured along the deck

// Launch button: LAUNCH_BTN = 16 for any 16 mm momentary panel button (sized
// from a typical one: 19 mm bezel, ~24 mm deep with wires on), or 24 for a
// third arcade button matching the flippers (~41 mm deep, which pushes the
// flipper buttons back to 60 mm).
LAUNCH_BTN = 16;
LAUNCH_D = LAUNCH_BTN == 24 ? 24.4 : 16.2;
LAUNCH_BEZEL = LAUNCH_BTN == 24 ? 27.6 : 19;
LAUNCH_BEHIND = LAUNCH_BTN == 24 ? 41 : 24;
LAUNCH_X = W - 20; LAUNCH_Z = LAUNCH_BTN == 24 ? 15.5 : 15;

// 24 mm arcade buttons (as ordered): 24 mm thread, 27.6 mm bezel, 19.6 mm cap,
// 33.2 mm long with 2.8 mm spade tabs on the back. Behind the panel that's
// ~30 mm of body plus ~11 mm for push-on spade connectors.
BTN_BEHIND = 30;   BTN_CONN = 11;   BTN_BEZEL = 27.6;
FLIP_D = 24.4;     // arcade button hole
FLIP_Y = LAUNCH_BTN == 24 ? 60 : 45;  FLIP_Z = 17;   // further back if the launch button is long
INSERT_D = 4.0;  INSERT_L = 5;   // M3 heat-set insert hole
DECK_SCREW_INSET = 6;

// USB-C panel-mount extension (as ordered: 30 cm, straight 33 x 12 x 7 mm male
// plug, female end with a 25 x 8 mm two-screw flange, M3 x 11 screws). The
// flange goes INSIDE the back wall with the socket face at a 12 x 6 opening;
// the screws come in from outside. Hole spacing isn't published, so the holes
// are slots covering USBC_SPACING_MIN..MAX.
USBC_CUT_W = 12;  USBC_CUT_H = 6;
USBC_SPACING_MIN = 18;  USBC_SPACING_MAX = 22;
USBC_Z = 16;
PLUG_L = 33;  PLUG_W = 12;  PLUG_T = 7;
PORT_N = -(3.7 + 1.6 + 1.6);   // board's USB-C centre, below the deck's underside

// backbox

BB_WALL = 2.0;
// the speaker (as measured: 40.40 x 28.36 x 9.36 mm cavity speaker), lying
// landscape behind the grille, with 0.3 mm clearance all round
SPK_L = 40.40; SPK_WID = 28.36; SPK_THK = 9.36;
SPK_W = SPK_L + 0.6; SPK_H = SPK_WID + 0.6; SPK_T = SPK_THK + 0.4;
SPK_Z0 = 7;        // speaker's space starts this far above the backbox's base (the USB plug's tail is below)
MARQ_W = 70;  MARQ_H = 24;                  // window
CARD_W = 74;  CARD_H = 29;  CARD_T = 1.6;   // marquee card (plus 0.8 mm raised text)
SHELF_T = 1.6;
// the shelf across the inside of the front wall: the speaker slides up from
// below until it stops under it, and the marquee card stands on top of it
SHELF_Z = SPK_Z0 + 1.6 + SPK_H + SHELF_T;   // top of the shelf
SLOT_D = CARD_T + 0.8 + 0.3;                // the card's channel, front to back
// tall enough for the speaker with the marquee card above it
BB_H = ceil(SHELF_Z + CARD_H + 0.5 + BB_WALL);
PEG_D = 5;    PEG_L = 5;

// the two flipper buttons face each other across the inside
FLIP_GAP = (W - 2 * WALL) - 2 * (BTN_BEHIND + BTN_CONN);
echo(str("flipper buttons: ", FLIP_GAP, " mm between the connectors (inside ", W - 2 * WALL, " mm)"));
assert(FLIP_GAP >= 3, "flipper buttons would collide inside the cabinet");
assert(FLIP_Z - BTN_BEZEL / 2 > 1, "button bezel hits the table");
// the launch button's body runs back from the front wall toward the flipper buttons
LAUNCH_GAP = (FLIP_Y - 12) - (WALL + LAUNCH_BEHIND);
echo(str("launch button: ", LAUNCH_GAP, " mm clear of the flipper buttons"));
assert(LAUNCH_GAP >= 3, "launch button would hit a flipper button");
assert(LAUNCH_Z - LAUNCH_BEZEL / 2 > 1 && LAUNCH_Z + LAUNCH_BEZEL / 2 < Z_FRONT, "launch bezel doesn't fit the front wall");
assert(WALL <= 3.5, "walls too thick for the button nuts");
echo(str("backbox height ", BB_H, " mm; cabinet ", Z_REAR + BB_H, " mm tall"));
assert(SPK_W <= W - 2 * BB_WALL, "speaker wider than the backbox");
assert(SPK_T + 2 < REAR_D - 2 * BB_WALL, "speaker too thick for the backbox");
assert(SPK_T > SLOT_D + 1.6 + 1, "shelf too shallow to stop the speaker behind the card channel");

// ---------------------------------------------------------------- helpers
// deck-local frame: x across, s up the slope, n out of the plate (n = 0 is
// the underside, n = DECK_T the top face)
module deck_frame() {
  translate([0, 0, Z_FRONT]) rotate([THETA, 0, 0]) children();
}

function board_hole(i) = [W / 2 + (i % 2 == 0 ? -1 : 1) * HOLE_DX / 2,
                          BOARD_S0 + PCB_L / 2 + (i < 2 ? -1 : 1) * HOLE_DY / 2];
function deck_screw(i) = [(i % 2 == 0) ? WALL + 3.5 : W - WALL - 3.5,   // boss (d 8) overlaps the wall 0.5
                          (i < 2) ? DECK_SCREW_INSET + 2 : SLOPE_L - DECK_SCREW_INSET - 2];

// ---------------------------------------------------------------- body
module side_profile() {   // y-z outline of the cabinet side
  polygon([[0, 0], [D, 0], [D, Z_REAR], [DECK_DD(), Z_REAR], [0, Z_FRONT]]);
}
function DECK_DD() = DECK_D;

module body() {
  difference() {
    union() {
      // side walls
      for (x = [0, W - WALL])
        translate([x, 0, 0]) rotate([90, 0, 90]) linear_extrude(WALL) side_profile();
      // front and rear walls, floor
      cube([W, WALL, Z_FRONT]);
      translate([0, D - WALL, 0]) cube([W, WALL, Z_REAR]);
      cube([W, D, FLOOR]);
      // flat top under the backbox
      translate([0, DECK_D, Z_REAR - WALL]) cube([W, REAR_D, WALL]);
      // deck screw bosses, hanging off the side walls under the deck, with
      // a 45-degree gusset below so they print without support
      for (i = [0 : 3]) deck_boss(i);
    }
    // flipper button holes, one each side
    for (x = [-eps, W - WALL - eps])
      translate([x, FLIP_Y, FLIP_Z]) rotate([0, 90, 0]) cylinder(d = FLIP_D, h = WALL + 2 * eps);
    // launch button hole in the front
    translate([LAUNCH_X, -eps, LAUNCH_Z]) rotate([-90, 0, 0]) cylinder(d = LAUNCH_D, h = WALL + 2 * eps);
    // USB-C panel socket in the back wall: the socket face, and slotted screw holes
    translate([W / 2, D + eps, USBC_Z]) rotate([90, 0, 0]) linear_extrude(WALL + 2 * eps)
      offset(r = 1.5) square([USBC_CUT_W - 3, USBC_CUT_H - 3], center = true);
    for (sx = [-1, 1])
      translate([W / 2, D + eps, USBC_Z]) rotate([90, 0, 0]) linear_extrude(WALL + 2 * eps)
        hull() for (sp = [USBC_SPACING_MIN, USBC_SPACING_MAX]) translate([sx * sp / 2, 0]) circle(d = 3.4);
    // backbox pegs and a pass-through for the speaker wire
    for (sx = [-1, 1])
      translate([W / 2 + sx * (W / 2 - 10), DECK_D + REAR_D / 2, Z_REAR - WALL - eps])
        cylinder(d = PEG_D + 0.3, h = WALL + 2 * eps);
    // opening for the board's USB-C plug, which rises into the backbox (it also
    // passes the speaker wire)
    translate([W / 2 - PLUG_W / 2 - 2, DECK_D + 0.5, Z_REAR - WALL - eps]) cube([PLUG_W + 4, REAR_D - 0.5 - WALL - 1, WALL + 2 * eps]);
    // rubber-foot recesses
    for (x = [12, W - 12], y = [12, D - 12])
      translate([x, y, -eps]) cylinder(d = 10.4, h = 1 + eps);
  }
}

module deck_boss(i) {
  p = deck_screw(i);
  wallx = (i % 2 == 0) ? WALL : W - WALL;
  difference() {
    hull() {
      deck_frame() translate([p[0], p[1], -8]) cylinder(d = 8, h = 8);
      // foot of the gusset on the side wall, well below the boss
      deck_frame() translate([wallx + (i % 2 == 0 ? -0.6 : 0.6), p[1], -18]) cube([0.5, 0.5, 0.5], center = true);
    }
    deck_frame() translate([p[0], p[1], -INSERT_L]) cylinder(d = INSERT_D, h = INSERT_L + eps);
    // keep the bosses out of the deck plate itself
    deck_frame() translate([-1, -1, 0]) cube([W + 2, SLOPE_L + 2, 20]);
  }
}

// ---------------------------------------------------------------- deck
module deck() {
  deck_frame() difference() {
    union() {
      cube([W, SLOPE_L, DECK_T]);
      // board standoffs on the underside: the glass presses against the
      // plate, the PCB rests on these
      for (i = [0 : 3]) {
        h = board_hole(i);
        translate([h[0], h[1], -ABOVE_PCB]) cylinder(d = 6.5, h = ABOVE_PCB + eps);
      }
    }
    // screen window over the visible area, chamfered on top
    va_x0 = W / 2 - VA_W / 2 - 0.3;
    va_s0 = BOARD_S0 + VA_TOP - 0.3;
    hull() {
      translate([va_x0, va_s0, -eps]) cube([VA_W + 0.6, VA_L + 0.6, eps]);
      translate([va_x0 - DECK_T, va_s0 - DECK_T, DECK_T]) cube([VA_W + 0.6 + 2 * DECK_T, VA_L + 0.6 + 2 * DECK_T, eps]);
    }
    // heat-set insert holes for the board screws
    for (i = [0 : 3]) {
      h = board_hole(i);
      translate([h[0], h[1], -ABOVE_PCB - eps]) cylinder(d = INSERT_D, h = INSERT_L);
    }
    // countersunk M3 screws into the body bosses
    for (i = [0 : 3]) {
      p = deck_screw(i);
      translate([p[0], p[1], -eps]) cylinder(d = 3.4, h = DECK_T + 2 * eps);
      translate([p[0], p[1], DECK_T - 1.7]) cylinder(d1 = 3.4, d2 = 6.6, h = 1.7 + eps);
    }
  }
}

// ---------------------------------------------------------------- backbox
module backbox() {
  y0 = DECK_D;
  plate_y = BB_WALL + SLOT_D;            // back of the marquee card's channel
  translate([0, y0, Z_REAR]) difference() {
    union() {
      difference() {
        cube([W, REAR_D, BB_H]);
        translate([BB_WALL, BB_WALL, -eps]) cube([W - 2 * BB_WALL, REAR_D - 2 * BB_WALL, BB_H - BB_WALL + eps]);
      }
      // the shelf: stops the speaker (slid up from the open bottom against the
      // grille) and carries the marquee card
      translate([BB_WALL - eps, BB_WALL - eps, SHELF_Z - SHELF_T]) cube([W - 2 * BB_WALL + 2 * eps, SPK_T + eps, SHELF_T]);
      // a block above the shelf, up to the top, that the card's channel is cut into
      translate([BB_WALL - eps, BB_WALL - eps, SHELF_Z - eps]) cube([W - 2 * BB_WALL + 2 * eps, SLOT_D + 1.6 + eps, BB_H - BB_WALL - SHELF_Z + 2 * eps]);
      // 45-degree gusset between the plate and the back of the shelf, so the
      // shelf doesn't droop when the backbox prints upside down
      translate([BB_WALL - eps, 0, 0]) rotate([90, 0, 90]) linear_extrude(W - 2 * BB_WALL + 2 * eps)
        polygon([[plate_y + 1.6 - eps, SHELF_Z - eps], [BB_WALL + SPK_T, SHELF_Z - eps],
                 [plate_y + 1.6 - eps, SHELF_Z + BB_WALL + SPK_T - plate_y - 1.6]]);
      // pegs into the body, each under a tab that spans front wall to rear wall
      for (sx = [-1, 1]) {
        translate([W / 2 + sx * (W / 2 - 10) - 4, 0, 0]) cube([8, REAR_D, 3]);
        translate([W / 2 + sx * (W / 2 - 10), REAR_D / 2, -PEG_L]) cylinder(d = PEG_D, h = PEG_L + eps);
      }
    }
    // marquee window, centred on the card
    translate([W / 2 - MARQ_W / 2, -eps, SHELF_Z + (CARD_H - MARQ_H) / 2]) cube([MARQ_W, BB_WALL + 2 * eps, MARQ_H]);
    // the card drops in through a slot in the top
    translate([W / 2 - CARD_W / 2 - 0.3, BB_WALL - eps, SHELF_Z]) cube([CARD_W + 0.6, SLOT_D + eps, BB_H - SHELF_Z + eps]);
    // speaker grille: a field of 1.8 mm holes over the speaker's face
    for (gx = [-(SPK_W / 2 - 5) : 3.4 : SPK_W / 2 - 5], gz = [-(SPK_H / 2 - 5) : 3.4 : SPK_H / 2 - 5])
      translate([W / 2 + gx, -eps, SPK_Z0 + 1.6 + SPK_H / 2 + gz]) rotate([-90, 0, 0]) cylinder(d = 1.8, h = BB_WALL + 2 * eps, $fn = 16);
  }
}

// ---------------------------------------------------------------- marquee
module marquee() {
  cube([CARD_W, CARD_H, CARD_T]);
  translate([CARD_W / 2, CARD_H / 2, CARD_T - eps])
    linear_extrude(0.8) text("SYN/ACK", size = 9, font = "Liberation Sans:style=Bold", halign = "center", valign = "center");
}

// ---------------------------------------------------------------- board (for the assembly view)
// the panel extension's straight male plug, seated in the board's port
module usbc_plug() {
  deck_frame() translate([W / 2 - PLUG_W / 2, BOARD_S0 + PCB_L - 1, PORT_N - PLUG_T / 2]) cube([PLUG_W, PLUG_L, PLUG_T]);
}
module usbc_socket() {   // the female end and flange, inside the back wall
  translate([W / 2 - 12.5, D - WALL - 3.05, USBC_Z - 4]) cube([25, 3, 8]);  // flush on the wall (0.05 gap for the clash test)
  translate([W / 2 - 6, D - WALL - 33, USBC_Z - 3.5]) cube([12, 30, 7]);
}

// the speaker, dy back from its seat against the grille and dz up
module speaker_at(dy, dz) {
  translate([W / 2 - SPK_L / 2, DECK_D + BB_WALL + 0.05 + dy, Z_REAR + SPK_Z0 + 1.6 + 0.3 + dz]) cube([SPK_L, SPK_THK, SPK_WID]);
}

// the marquee card standing in its channel, text out, dz above the shelf
module card_at(dz) {
  translate([W / 2 - CARD_W / 2, DECK_D + BB_WALL + 0.05 + CARD_T + 0.8, Z_REAR + SHELF_Z + 0.05 + dz]) rotate([90, 0, 0]) marquee();
}

module board_dummy() {
  deck_frame() translate([W / 2 - PCB_W / 2, BOARD_S0, -ABOVE_PCB - PCB_T]) {
    color("darkgreen") cube([PCB_W, PCB_L, PCB_T]);
    color("black") translate([0, 9.25, PCB_T]) cube([PCB_W, 83, ABOVE_PCB]);
    color("dimgray") translate([4, 8, -BELOW_PCB]) cube([PCB_W - 8, PCB_L - 16, BELOW_PCB]);
  }
}

// ---------------------------------------------------------------- output
if (PART == "body") body();
else if (PART == "deck")  // face down on the bed: smooth screen surround, standoffs up
  translate([0, SLOPE_L, DECK_T]) rotate([180, 0, 0]) rotate([-THETA, 0, 0]) translate([0, 0, -Z_FRONT]) deck();
else if (PART == "backbox")  // upside down, open bottom up: every wall rises straight from the bed
  translate([0, REAR_D, BB_H]) rotate([180, 0, 0]) translate([0, -DECK_D, -Z_REAR]) backbox();
else if (PART == "marquee") marquee();
else if (PART == "clash") {  // must be empty: plug, socket, speaker and card hit nothing printed
  intersection() {
    union() { usbc_plug(); usbc_socket(); speaker_at(0, 0); card_at(0); }
    union() { body(); deck(); backbox(); }
  }
  // on their way in: the speaker straight up from the open bottom (before the
  // backbox goes on), the card down through the slot in the top
  intersection() {
    union() {
      hull() { speaker_at(0, 0); speaker_at(0, -(SPK_Z0 + SPK_H + 4)); }
      hull() { card_at(0); card_at(CARD_H + 5); }
    }
    backbox();
  }
}
else if (PART == "section") {  // the assembly cut down the middle, for checking
  difference() {
    union() {
      color("#1a2636") body();
      color("#2a3a4c") deck();
      color("#16202d") backbox();
      board_dummy();
      // the arcade buttons' bodies + connectors, as cylinders
      for (sx = [0, 1])
        color("#ff3df0", 0.6) translate([sx ? W - WALL : WALL, FLIP_Y, FLIP_Z])
          rotate([0, sx ? -90 : 90, 0]) cylinder(d = 24, h = BTN_BEHIND + BTN_CONN);
      color("#ffb454", 0.6) translate([LAUNCH_X, WALL, LAUNCH_Z]) rotate([-90, 0, 0]) cylinder(d = 16, h = 30);
      color("#c0c0c0") speaker_at(0, 0);
      color("#ff3df0") card_at(0);
      color("#39ff6a") usbc_plug();
      color("#39ff6a") usbc_socket();
    }
    translate([W / 2 + 10, -50, -50]) cube([200, 300, 300]);
  }
}
else {
  color("#1a2636") body();
  color("#2a3a4c") deck();
  color("#16202d") backbox();
  board_dummy();
  color("#ff3df0") card_at(0);
}
