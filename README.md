# SYN/ACK pinball

[![build](https://github.com/magikh0e/synack-pinball/actions/workflows/build.yml/badge.svg)](https://github.com/magikh0e/synack-pinball/actions/workflows/build.yml)

![multiball](docs/screenshot-play.png) ![title](docs/screenshot-attract.png) ![game over](docs/screenshot-gameover.png)

*Read straight off the CYD's panel: SYN FLOOD multiball in the attract demo,
the title screen mid light show, and a new high score.*

A pinball table for 3.5" 320x480 ESP32 touch boards. Two are supported, from
one codebase:

| Board | PlatformIO env | Display / touch | Flippers |
|---|---|---|---|
| Sunton **ESP32-3248S035R** (CYD 3.5") | `esp32-3248s035r` (default) | ST7796 SPI, XPT2046 resistive | thirds of the screen, middle = both |
| LCDwiki **ES3C35P** (ESP32-S3) | `es3c35p` | ST77922 quad SPI, capacitive multi-touch | each half of the screen, two thumbs at once |

![both boards](docs/both-boards.jpg)

*The same game on both boards: the CYD on the left, the ES3C35P on the right.*

How it was made, from the physics to the printed cabinet:
[the making of SYN/ACK](docs/MAKING-OF.md).

## Play

| Touch | Does |
|---|---|
| left third of the screen | left flipper |
| right third | right flipper |
| middle (or a thumb on each side) | both flippers |
| hold while the ball sits in the plunger lane | charge plunger, release to launch |

The panel is single-touch, but a resistive panel *averages* two presses, so a
thumb on each side lands in the middle zone and fires both flippers.

**Rules:** 3 balls, 6 s ball save after launch.

- **S Y N** top lanes: light all three for +2,000 and the next score
  multiplier (up to 5x, reset per ball). Flipper presses rotate the lit lanes.
- **A C K** targets on the right bank: complete them for +5,000. Completed
  while SYN is armed it's a **HANDSHAKE** jackpot instead: +25,000, and it
  starts **SYN FLOOD** multiball.
- **SYN FLOOD multiball:** two extra balls are auto-launched (3 on the table).
  All scoring is doubled while more than one ball is live, and a 10 s ball save
  relaunches early drains. Losing extra balls is free; it ends at one ball.
- **Kickback** in the left outlane fires the ball back once per game and is
  relit by completing ACK.
- **UPLINK ramp:** a wire ramp from right of centre (aim with the left flipper)
  over the top and down into the left inlane. 2,500 and it lights the next
  S/Y/N lane; during SYN FLOOD every ramp is a 15,000 **JACKPOT**. Weak shots
  roll back out.
- **Spinner** in the left orbit lane: 100 per half turn, doubled (and the plate
  turns magenta) while SYN is armed.
- Pop bumpers 100, slingshots 10.
- **Skill shot:** a magenta zone sits beside the plunger's power meter (it
  moves each ball). Release with the power inside it for 5,000; the meter
  turns white while you're in the zone.
- **Extra ball:** the second HANDSHAKE of a game. SHOOT AGAIN stays lit
  until it's played, and the same ball number goes again.
- **End-of-ball bonus:** each ramp 1,000, lane 250, target 250 and spinner
  half-turn 25, times the multiplier, counted into the score after every
  drain. Any press hurries it along.
- **Blinking lamps** mark the shot that matters: the A/C/K targets (and the
  spinner arrow) once SYN is armed, the ramp arrow while the jackpot is lit,
  and SHOOT AGAIN for the last 2 s of a ball save.

**Attract mode:** after 15 s idle on the title or game-over screen the table
plays itself (silently, never touching the high score) with **DEMO · TAP TO
PLAY** in the top bar. Any tap starts a real game; a demo returns to the title
when it ends or after 90 s.

**Between games** the lamps chase around the table; on a new high score they
all flash.

**High scores:** a score in the top five asks for three initials. A flipper
steps the letter back or forward when you let go of it; both flippers
together (or the launch button) take the letter. Left alone for 30 s, the
initials are kept as they are. The title screen takes turns with the
high-score table. The table is kept in NVS.

**Volume:** off the table (title, game over, demo) hold the screen, or
**BOOT**, for a second to cycle off / low / medium / high (high by default); a
short tap still starts a game. Kept in NVS. The ES3C35P synthesizes its sounds on three mixed voices (square, pulse,
triangle, saw, noise, pitch sweeps), so a flipper thwack doesn't cut off a
bumper, and in multiball the balls clack when they hit each other; the CYD's
single buzzer plays the newest sound as plain beeps.

**Idle:** after 2 minutes with no touch or button the backlight dims, and after
10 minutes it goes dark and stops drawing. Any touch wakes it (that touch is
ignored, so it doesn't also start a game).

## Build

```
pio run -t upload --upload-port COM4                 # CYD
pio run -e es3c35p -t upload --upload-port COM6      # ES3C35P (native USB)
pio device monitor        # fps, worst frame and heap every 5 s
                          # serial: 'b' bot player, 'm' multiball, 'p' screenshot,
                          # 'h' high-score table, 'h0' clear it, 'o<n>' game-over screen with score n,
                          # 'v' volume, 'v<0-3>' set it (0 off, 3 high)
python host/shot.py shot.png --reset --pre bm --wait 8   # real screenshot off the panel ('p')
sh host/run.sh            # PC tests (Docker g++); exits non-zero on any failed check
```

## Layout

- `src/game.hpp`: table geometry, physics (1/600 s substeps, capsule
  colliders), rules. Pure C++, no Arduino.
- `src/render.hpp`: draws the world into any rectangle of the screen.
- `src/bot.hpp`: the automatic player behind the demo and the host soak tests.
- `src/main.cpp`: main loop, rendering, sound sequencing, input mapping. Only
  dirty rectangles are redrawn, through two 30 KB buffers (~90 fps).
- `src/board.hpp` + `board_cyd.cpp` / `board_es3c35p.cpp`: the hardware per
  board (display push, touch, sound, LED, buttons). `src/s3/` has the ST77922
  quad-SPI driver and the vendor's ES8311 codec driver. The ST77922 only
  accepts windows whose start column and width are multiples of 4, so dirty
  rects are widened to match (`board::ALIGN_X`).

Hardware references are the manufacturers' own: Sunton's *3.5 inch ESP32 module
ESP32-3248S035R/C* (no GPIO map, so the CYD pins were verified on the board),
and LCDwiki's ES3C35P pack (`lcdwiki.com/res/ES3C35P/`): IO allocation table,
schematic, ST77922 specification and example code.
- `host/sim.cpp`: PC test harness (`sh host/run.sh`, needs Docker). It renders
  the table to PPM, sweeps plunger power and flipper shots, soaks 300 balls
  with a bot, and checks that partial redraws match a full redraw pixel for pixel.

## Cabinet

<img src="docs/cabinet.jpg" alt="the printed cabinet running the demo" width="360">

[`case/`](case/README.md) has a printable tabletop cabinet for the ES3C35P (OpenSCAD, with
ready STLs): a 30° deck, a backbox with marquee and speaker, and arcade buttons on
the sides. It uses the `es3c35p-cabinet` firmware build, which turns the image
180° because the board mounts upside down.

## Physical buttons

Each is a momentary switch from the pin to GND; internal pull-ups, no resistors.
Touch keeps working alongside them.

On the **CYD**:

| Button | Pin | Where |
|---|---|---|
| Left flipper | IO22 | "Extended IO" header, silkscreened P3 (`GND IO35 IO22 IO21`): 4-pin **MX1.25** |
| Right flipper | IO21 | P3 header |
| Launch | GPIO 0 | the board's **BOOT** button: hold to charge, release to fire |

On the **ES3C35P** the buttons go on the serial-port header (4-pin **MX1.25**;
JST GH doesn't fit. The board ships with a 1.25 mm to 2.54 mm adapter cable):

| Button | Pin | Where |
|---|---|---|
| Left flipper | IO43 | serial header **TXD** (pin 3) |
| Right flipper | IO44 | serial header **RXD** (pin 4) |
| Launch | GPIO 0 | the board's **BOOT** button |
| (common) | GND | serial header **GND** (pin 2) |

The header is silkscreened `UART` with `RXD TXD GND 5V`; pin 1 (the triangle)
is **5V**, so never put a button on it.

Don't hold the left button while the ES3C35P resets: the ROM prints its boot
log on IO43.

**Lit flipper buttons (ES3C35P):** 5 V LED arcade buttons light up with the
game: they glow during play, flare while pressed, flash on multiball, jackpots
and drains, and breathe in turn on the title screen and in the demo. Each
lamp is switched by an NPN transistor (2N2222, 2N3904 or similar) from the
expansion socket (P3, 1.25 mm 4-pin):

| From | To |
|---|---|
| IO45 (expansion pin 4) | 1 kΩ, then the left lamp's transistor base |
| IO46 (expansion pin 3) | 1 kΩ, then the right lamp's transistor base |
| both emitters | GND (expansion pin 2) |
| each lamp's − | its transistor's collector |
| each lamp's + | 5V (serial header pin 1) |

The expansion socket is silkscreened `IO45 IO46 GND 3.3V`; its pin 1 (the
triangle) is 3.3V and isn't used. IO45 and IO46 must be low when the board
starts; a transistor base keeps them there, but a lamp wired straight to the
pin could stop it booting.
On the CYD the lamps aren't driven.

Any button press also starts a game or leaves the demo. Don't hold BOOT while
the board resets (it enters flash mode). Pins are set at the top of `main.cpp`.
