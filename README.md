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
- Pop bumpers 100, slingshots 10. The high score is kept in NVS.
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

**Volume:** off the table (title, game over, demo) hold **BOOT** for a second
to cycle off / low / medium / high; a short press still starts a game. Kept in
NVS. The ES3C35P synthesizes its sounds on three mixed voices (square, pulse,
triangle, saw, noise, pitch sweeps), so a flipper thwack doesn't cut off a
bumper; the CYD's single buzzer plays the newest sound as plain beeps.

**Idle:** after 2 minutes with no touch or button the backlight dims, and after
10 minutes it goes dark and stops drawing. Any touch wakes it (that touch is
ignored, so it doesn't also start a game).

## Build

```
pio run -t upload --upload-port COM4                 # CYD
pio run -e es3c35p -t upload --upload-port COM6      # ES3C35P (native USB)
pio device monitor        # fps, worst frame and heap every 5 s
                          # serial: 'b' bot player, 'm' multiball, 'p' screenshot,
                          # 'h<n>' set high score, 'o<n>' game-over screen with score n
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

## Physical buttons

Each is a momentary switch from the pin to GND; internal pull-ups, no resistors.
Touch keeps working alongside them.

On the **CYD**:

| Button | Pin | Where |
|---|---|---|
| Left flipper | IO22 | "Extended IO" header, silkscreened P3 (`GND IO35 IO22 IO21`): 4-pin 1.25 mm per Sunton's spec; JST **GH** fits, PicoBlade doesn't |
| Right flipper | IO21 | P3 header |
| Launch | GPIO 0 | the board's **BOOT** button: hold to charge, release to fire |

On the **ES3C35P** the buttons go on the serial-port header (1.25 mm 4-pin; the
board ships with a 1.25 mm to 2.54 mm adapter cable):

| Button | Pin | Where |
|---|---|---|
| Left flipper | IO43 | serial header pin 3 (TXD0) |
| Right flipper | IO44 | serial header pin 4 (RXD0) |
| Launch | GPIO 0 | the board's **BOOT** button |
| (common) | GND | serial header pin 1 (pin 2 is power: leave it alone) |

Don't hold the left button while the ES3C35P resets: the ROM prints its boot
log on IO43.

Any button press also starts a game or leaves the demo. Don't hold BOOT while
the board resets (it enters flash mode). Pins are set at the top of `main.cpp`.
