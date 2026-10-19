# Tabletop cabinet (ES3C35P)

<img src="../docs/cabinet.jpg" alt="the finished cabinet mid-game, with the left flipper button lit" width="360">

![assembly](renders/assembly.png) ![cutaway](renders/section.png)

A small pinball cabinet for the LCDwiki ES3C35P: the screen lies on a 30° deck,
with a backbox and marquee above it, 24 mm arcade buttons on the sides for the
flippers, and a launch button on the front. 92 × 136 × 166 mm.

The board mounts **upside down**: its USB-C, RESET and BOOT end faces the back
of the cabinet. That keeps the speaker socket next to the backbox and the
flipper-button socket near the buttons. Flash the matching firmware, which
turns the image and touch 180°:

```
pio run -e es3c35p-cabinet -t upload --upload-port COM6
```

## Parts

| Part | STL | Print orientation | Notes |
|---|---|---|---|
| Body | `stl/body.stl` | upright, floor down | walls, button holes, deck bosses, USB-C panel socket |
| Deck | `stl/deck.stl` | face down | screen window with a chamfered edge; board standoffs face up |
| Backbox | `stl/backbox.stl` | upside down (top on the bed, open bottom up) | marquee window and card slot, speaker grille and shelf; the window bridges 70 mm, so turn bridging on |
| Marquee | `stl/marquee.stl` | flat | the SYN/ACK card; print the text in a second colour if you can |

None of them need supports. 0.2 mm layers and 3 walls are plenty; PETG or PLA.

Regenerate the STLs after changing `cabinet.scad` (OpenSCAD 2021.01+):

```
sh export.sh                  # 16 mm launch button
LAUNCH_BTN=24 sh export.sh    # a third 24 mm arcade button for launch
```

Rendering the model also prints its fit checks: the gap between the two facing
flipper buttons, the room their inside nuts have to turn above the floor, and
the launch button's clearance. A change that makes the
parts collide fails with an assertion instead of exporting. `PART="clash"`
renders the USB-C plug and socket, the speaker, the marquee card (in place,
and on their way in) and the flipper buttons with their nuts against the
printed parts and the board's underside, and it must come out empty
("Current top level object is empty").

## Hardware

| Qty | Part |
|---|---|
| 2 | 24 mm arcade button (flippers): 24 mm thread, ~30 mm body behind the panel, 2.8 mm tabs |
| 10 | 2.8 mm (0.110") female spade connectors for the button tabs (4 per LED flipper button, 2 for the launch button). Use them rather than soldering: heat on an LED button's lamp tabs can burn out the LED |
| 1 | 16 mm momentary panel button (launch), ≤3.5 mm panel; or a third 24 mm button with `LAUNCH_BTN=24` |
| 1 | cavity speaker, 40.4 × 28.4 × 9.4 mm (8 Ω; the board drives up to 1.5 W at 8 Ω) |
| 8 | M3 heat-set inserts (OD ~4 mm, 4–5 mm long) |
| 4 | M3 × 6 screws (board to deck) |
| 4 | M3 × 8 countersunk screws (deck to body) |
| 4 | 10 mm rubber feet |
| 2 | NPN transistors (2N2222, 2N3904…) and 2 × 1 kΩ, if the flipper buttons have 5 V LEDs |
| 1 | USB-C panel-mount extension, 30 cm: female socket with a two-screw flange, straight male plug (comes with M3 × 11 screws) |
| 1 | 90° USB-C adapter, male to female, socket on the broad face (12 mm body, 7.1 mm rise) |

The board's speaker and button sockets are **MX1.25** (1.25 mm pitch); a kit
of pre-crimped MX1.25 4-pin leads fits them. JST GH is also 1.25 mm but doesn't
mate, so check the family, not just the pitch.

## Assembly

The deck goes in **before** the backbox: the speaker lead runs up from the
board through the body's top, behind the deck.

1. Press the M3 inserts into the deck's four board standoffs and the body's
   four deck bosses.
2. Screw the board to the deck, **USB-C end toward the backbox end**, glass
   against the window. The glass should sit flat on the underside of the deck.
3. Mount the arcade buttons in the side walls and the launch button in the
   front wall.
4. Fit the USB-C socket to the back wall: flange **inside**, socket face to the
   opening, screws in from outside. The listing doesn't give the screw
   spacing, so the holes are slots covering 18–22 mm; measure yours, and if it
   falls outside that, change `USBC_SPACING_MIN/MAX` and re-export. If the
   flange holes aren't threaded, add M3 nuts inside.
5. Plug the 90° adapter into the board, socket facing **away from the
   screen** (USB-C goes in either way up, so turn it over if it faces the
   glass), and plug the extension's male end into it. The plug then points
   down toward the back wall, and the cable runs down the wall to the socket
   in one loose loop; without the adapter it has to double back hard inside
   the backbox. Plug the speaker lead into the board's speaker socket, and
   wire the buttons (below).
6. Lower the deck in, guiding the speaker lead up through the opening in the
   body's top behind the deck. Coil the spare USB cable at the bottom of the
   body. Screw the deck down.
7. Slide the speaker up into the backbox from its open bottom, face against
   the grille, until it stops under the shelf. Centre it on the grille and run
   a bead of hot glue along its sides to hold it.
8. Push the backbox's pegs into the body.
9. Drop the marquee card into the slot in the backbox's top, text facing out;
   it stands on the shelf behind the window. To take it out, lift the backbox
   off and tip it over.

## Wiring

| From | To |
|---|---|
| Left flipper button | serial socket (`UART`) TXD, pin 3, and GND, pin 2 |
| Right flipper button | serial socket (`UART`) RXD, pin 4, and GND, pin 2 |
| Launch button | across the board's BOOT switch (GPIO 0 and GND) |
| Speaker | the board's 2-pin speaker socket |
| Flipper button lamps (LED buttons) | through a transistor each from IO45 (left) and IO46 (right) on the expansion socket; see [lit flipper buttons](../README.md#physical-buttons) |

The serial socket's pin 1 (marked with a triangle, labelled 5V) is power:
never wire a button to it. Don't hold the launch button while the board powers
up or resets, because it then starts in flashing mode.
